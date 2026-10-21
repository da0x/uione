// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one

import (
	"context"
	"crypto/hmac"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"reflect"
	"strconv"
	"strings"
	"sync"
	"time"

	"cloud.google.com/go/firestore"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

// StripeSpec sells subscriptions through Stripe, the way Google sign-in is a
// provider: uione talks to Stripe safely, and the app decides what to sell. Plans,
// seats and prices are the app's.
//
// One entity is Stripe's customer, like an organization. What Stripe knows of each,
// its customer and its subscription, is kept by the backend in stripe_customers,
// where no command reaches it; the app hears of a change through OnSubscription and
// keeps what it needs in its own fields. A command's body starts a subscription with
// StripeCheckout, opens the Customer Portal with StripePortal, and changes or ends it
// with StripeChange and StripeCancel.
//
// Stripe's webhook, at the route, is believed only when STRIPE_WEBHOOK_SECRET has
// signed it within the last five minutes. An event is handled once, and what it's
// about is asked of Stripe again rather than read from it, since Stripe sends events
// late, twice and out of order. STRIPE_SECRET_KEY is what the backend calls Stripe
// with.
type StripeSpec struct {
	route          string
	customer       reflect.Type // the entity that is Stripe's customer, like an organization
	onSubscription func(*Ctx, Subscription) error
	onInvoice      func(*Ctx, Invoice) error
}

// Stripe receives Stripe's webhook at a route, like /hooks/stripe.
func Stripe(route string) *StripeSpec { return &StripeSpec{route: route} }

// For says which entity is Stripe's customer, like For(Entity[Organization]()).
func (s *StripeSpec) For(customer Kind) *StripeSpec {
	s.customer = customer.typ
	return s
}

// OnSubscription runs when a customer's subscription changes: started, renewed,
// changed, past due or ended.
func (s *StripeSpec) OnSubscription(do func(*Ctx, Subscription) error) *StripeSpec {
	s.onSubscription = do
	return s
}

// OnInvoice runs when a customer's invoice is paid, or its payment fails.
func (s *StripeSpec) OnInvoice(do func(*Ctx, Invoice) error) *StripeSpec {
	s.onInvoice = do
	return s
}

func (s *StripeSpec) register(r *registry, ns string) {
	if s.customer == nil {
		panic("one: Stripe needs For, the entity that is its customer")
	}
	if r.stripe != nil && r.stripe != s {
		panic("one: an app takes Stripe's webhook once")
	}
	r.schema(s.customer, ns)
	r.stripe = s
}

// Subscription is a customer's subscription as Stripe says it is now.
type Subscription struct {
	Of                string    // the customer entity's id, like an organization's
	ID                string    // Stripe's, like sub_...
	Status            string    // Stripe's: active, trialing, past_due, unpaid, canceled, incomplete, ...
	Price             string    // the price's lookup key, like team_monthly
	Quantity          int       // how many, like seats
	PeriodEnd         time.Time // when the period paid for ends
	CancelAtPeriodEnd bool      // whether it ends then rather than renewing
}

// Live says whether a subscription is still one: being paid for, on trial, or
// waiting on a payment.
func (s Subscription) Live() bool {
	switch s.Status {
	case "active", "trialing", "past_due", "unpaid", "incomplete":
		return true
	}
	return false
}

// Invoice is one of a customer's invoices as Stripe says it is now.
type Invoice struct {
	Of           string // the customer entity's id
	ID           string // Stripe's, like in_...
	Subscription string // the subscription it bills, if any
	Status       string // Stripe's: paid, open, uncollectible, void, draft
	Paid         bool   // whether it was paid, rather than its payment failing
	AmountDue    int64  // in the currency's smallest unit, like cents
	AmountPaid   int64
	Currency     string // like usd
}

// Price is what a subscription is for, found in Stripe by its lookup key. When it
// isn't there and an amount is given, it's made: a price's amount never changes, so
// a new amount needs a new lookup key.
type Price struct {
	LookupKey string // like team_monthly
	Amount    int64  // in the currency's smallest unit, like cents
	Currency  string // like usd
	Interval  string // month or year
	Name      string // the product's name, shown on Checkout and invoices
}

// stripeAPI is how the backend calls Stripe: its secret key, the secret its webhook
// is signed with, and where Stripe is, which a test points elsewhere.
type stripeAPI struct {
	key, signing, base string
	client             *http.Client
	prices             sync.Map // a lookup key to its price's id, once found
}

func stripeFromEnvironment() *stripeAPI {
	base := os.Getenv("STRIPE_API")
	if base == "" {
		base = "https://api.stripe.com"
	}
	return &stripeAPI{key: os.Getenv("STRIPE_SECRET_KEY"), signing: os.Getenv("STRIPE_WEBHOOK_SECRET"), base: base, client: &http.Client{Timeout: 30 * time.Second}}
}

// call sends a request to Stripe and reads its answer. Every POST carries an
// idempotency key, so a request sent again, as a command's transaction may be,
// returns what the first one did.
func (s *stripeAPI) call(ctx context.Context, method, path string, form url.Values, idempotency string) (map[string]any, error) {
	if s.key == "" {
		return nil, errors.New("one: STRIPE_SECRET_KEY isn't set")
	}
	address := s.base + path
	var body io.Reader
	if method == http.MethodGet {
		if len(form) > 0 {
			address += "?" + form.Encode()
		}
	} else {
		body = strings.NewReader(form.Encode())
	}
	request, err := http.NewRequestWithContext(ctx, method, address, body)
	if err != nil {
		return nil, err
	}
	request.Header.Set("Authorization", "Bearer "+s.key)
	if method != http.MethodGet {
		request.Header.Set("Content-Type", "application/x-www-form-urlencoded")
		if idempotency != "" {
			request.Header.Set("Idempotency-Key", idempotency)
		}
	}
	response, err := s.client.Do(request)
	if err != nil {
		return nil, err
	}
	defer response.Body.Close()
	var answer map[string]any
	if err := json.NewDecoder(io.LimitReader(response.Body, 5<<20)).Decode(&answer); err != nil {
		return nil, fmt.Errorf("one: Stripe's answer to %s %s isn't JSON: %w", method, path, err)
	}
	if response.StatusCode >= 300 {
		return nil, fmt.Errorf("one: Stripe refused %s %s: %s", method, path, stripeText(answer, "error", "message"))
	}
	return answer, nil
}

// stripeText, stripeNumber and stripeItems read Stripe's JSON along a path of names.
func stripeText(m map[string]any, path ...string) string {
	v, _ := stripeAt(m, path...).(string)
	return v
}

func stripeNumber(m map[string]any, path ...string) int64 {
	v, _ := stripeAt(m, path...).(float64)
	return int64(v)
}

func stripeItems(m map[string]any, path ...string) []map[string]any {
	list, _ := stripeAt(m, path...).([]any)
	var out []map[string]any
	for _, item := range list {
		if each, ok := item.(map[string]any); ok {
			out = append(out, each)
		}
	}
	return out
}

func stripeAt(m map[string]any, path ...string) any {
	var v any = m
	for _, name := range path {
		next, ok := v.(map[string]any)
		if !ok {
			return nil
		}
		v = next[name]
	}
	return v
}

// subscriptionFrom reads a subscription. Newer versions of Stripe's API keep the
// period on its item rather than on itself, so both are read.
func subscriptionFrom(m map[string]any) Subscription {
	s := Subscription{ID: stripeText(m, "id"), Status: stripeText(m, "status"), CancelAtPeriodEnd: stripeAt(m, "cancel_at_period_end") == true}
	end := stripeNumber(m, "current_period_end")
	if lines := stripeItems(m, "items", "data"); len(lines) > 0 {
		s.Price, s.Quantity = stripeText(lines[0], "price", "lookup_key"), int(stripeNumber(lines[0], "quantity"))
		if own := stripeNumber(lines[0], "current_period_end"); own != 0 {
			end = own
		}
	}
	if end != 0 {
		s.PeriodEnd = time.Unix(end, 0).UTC()
	}
	return s
}

// invoiceFrom reads an invoice. Newer versions of Stripe's API name its subscription
// under its parent.
func invoiceFrom(m map[string]any) Invoice {
	i := Invoice{ID: stripeText(m, "id"), Status: stripeText(m, "status"), AmountDue: stripeNumber(m, "amount_due"), AmountPaid: stripeNumber(m, "amount_paid"), Currency: stripeText(m, "currency")}
	i.Subscription = stripeText(m, "subscription")
	if i.Subscription == "" {
		i.Subscription = stripeText(m, "parent", "subscription_details", "subscription")
	}
	i.Paid = i.Status == "paid"
	return i
}

// stripeSigned says whether a webhook's Stripe-Signature is the signing secret's
// HMAC of its time and body, made at most five minutes from now.
func stripeSigned(secret string, body []byte, header string, now time.Time) bool {
	var when int64
	var signatures [][]byte
	for _, part := range strings.Split(header, ",") {
		name, value, _ := strings.Cut(strings.TrimSpace(part), "=")
		switch name {
		case "t":
			when, _ = strconv.ParseInt(value, 10, 64)
		case "v1":
			if signature, err := hex.DecodeString(value); err == nil {
				signatures = append(signatures, signature)
			}
		}
	}
	if when == 0 || now.Sub(time.Unix(when, 0)).Abs() > 5*time.Minute {
		return false
	}
	mac := hmac.New(sha256.New, []byte(secret))
	mac.Write([]byte(strconv.FormatInt(when, 10) + "."))
	mac.Write(body)
	want := mac.Sum(nil)
	for _, signature := range signatures {
		if hmac.Equal(signature, want) {
			return true
		}
	}
	return false
}

func (a *App) serveStripe(s *StripeSpec) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if a.stripe.signing == "" || a.stripe.key == "" {
			a.log.Printf("one: a Stripe event came, but STRIPE_WEBHOOK_SECRET or STRIPE_SECRET_KEY isn't set")
			reply(w, http.StatusServiceUnavailable, map[string]any{"error": "this site doesn't take Stripe's webhook yet"})
			return
		}
		body, err := io.ReadAll(http.MaxBytesReader(w, r.Body, 5<<20))
		if err != nil {
			reply(w, http.StatusBadRequest, map[string]any{"error": "that event couldn't be read"})
			return
		}
		if !stripeSigned(a.stripe.signing, body, r.Header.Get("Stripe-Signature"), time.Now()) {
			reply(w, http.StatusUnauthorized, map[string]any{"error": "that event isn't signed with this site's secret, or is too old"})
			return
		}
		var e map[string]any
		if err := json.Unmarshal(body, &e); err != nil {
			reply(w, http.StatusBadRequest, map[string]any{"error": "that event isn't JSON Stripe sends"})
			return
		}
		id := stripeText(e, "id")
		seen := a.store.Collection("stripe_events").Doc(url.PathEscape(id))
		if _, err := seen.Get(r.Context()); err == nil {
			reply(w, http.StatusOK, map[string]any{"already": true})
			return
		} else if status.Code(err) != codes.NotFound {
			a.log.Printf("one: a Stripe event couldn't be checked: %v", err)
			reply(w, http.StatusInternalServerError, map[string]any{"error": "something went wrong on our side; Stripe will try again"})
			return
		}
		if err := a.stripeEvent(r.Context(), s, stripeText(e, "type"), stripeAt(e, "data", "object")); err != nil {
			a.log.Printf("one: a Stripe %s event failed: %v", stripeText(e, "type"), err)
			reply(w, http.StatusInternalServerError, map[string]any{"error": "something went wrong on our side; Stripe will try again"})
			return
		}
		if _, err := seen.Set(r.Context(), map[string]any{"at": time.Now().UTC(), "expires": time.Now().UTC().Add(30 * 24 * time.Hour)}); err != nil {
			a.log.Printf("one: a Stripe event couldn't be recorded: %v", err)
		}
		reply(w, http.StatusOK, map[string]any{"ok": true})
	}
}

// stripeEvent asks Stripe again for what an event is about, and hands it to the
// app. An event about anything else is let go.
func (a *App) stripeEvent(ctx context.Context, s *StripeSpec, kind string, object any) error {
	about, _ := object.(map[string]any)
	switch {
	case strings.HasPrefix(kind, "customer.subscription."), kind == "checkout.session.completed":
		id := stripeText(about, "id")
		if kind == "checkout.session.completed" {
			if id = stripeText(about, "subscription"); id == "" {
				return nil
			}
		}
		now, err := a.stripe.call(ctx, http.MethodGet, "/v1/subscriptions/"+url.PathEscape(id), url.Values{"expand[]": {"items.data.price"}}, "")
		if err != nil {
			return err
		}
		return a.stripeSubscription(ctx, s, stripeText(now, "customer"), subscriptionFrom(now))
	case kind == "invoice.paid", kind == "invoice.payment_failed":
		now, err := a.stripe.call(ctx, http.MethodGet, "/v1/invoices/"+url.PathEscape(stripeText(about, "id")), nil, "")
		if err != nil {
			return err
		}
		invoice := invoiceFrom(now)
		if kind == "invoice.paid" {
			invoice.Paid = true
		}
		return a.stripeHandle(ctx, stripeText(now, "customer"), "stripe::invoice", func(c *Ctx, of string, _ *firestore.DocumentSnapshot) error {
			if s.onInvoice == nil {
				return nil
			}
			invoice.Of = of
			return s.onInvoice(c, invoice)
		})
	}
	return nil
}

// stripeSubscription keeps a customer's subscription as Stripe says it is now, and
// tells the app. The one kept is the customer's current subscription: one that has
// ended is let go when another is live, so a late event about an old subscription
// never undoes a new one.
func (a *App) stripeSubscription(ctx context.Context, s *StripeSpec, customer string, sub Subscription) error {
	return a.stripeHandle(ctx, customer, "stripe::subscription", func(c *Ctx, of string, kept *firestore.DocumentSnapshot) error {
		current, _ := kept.Data()["subscription"].(string)
		currentStatus, _ := kept.Data()["status"].(string)
		if current != "" && current != sub.ID && !sub.Live() && (Subscription{Status: currentStatus}).Live() {
			return nil
		}
		c.writes = append(c.writes, write{ref: kept.Ref, merged: true, data: map[string]any{
			"subscription": sub.ID, "status": sub.Status, "price": sub.Price, "quantity": sub.Quantity,
			"period_end": sub.PeriodEnd, "cancel_at_period_end": sub.CancelAtPeriodEnd,
		}})
		if s.onSubscription == nil {
			return nil
		}
		sub.Of = of
		return s.onSubscription(c, sub)
	})
}

// stripeHandle runs an app's handler for one of a customer's events, in a
// transaction of its own, as "stripe". A customer that isn't one of this app's is
// let go, as is what the handler refuses, which isn't Stripe's to retry.
func (a *App) stripeHandle(ctx context.Context, customer, command string, do func(*Ctx, string, *firestore.DocumentSnapshot) error) error {
	if customer == "" {
		return nil
	}
	found, err := a.store.Collection("stripe_customers").Where("customer", "==", customer).Limit(1).Documents(ctx).GetAll()
	if err != nil {
		return err
	}
	if len(found) == 0 {
		a.log.Printf("one: Stripe's customer %s isn't one of this app's", customer)
		return nil
	}
	of, _ := found[0].Data()["of"].(string)
	now := time.Now().UTC()
	var events []event
	err = a.store.RunTransaction(ctx, func(ctx context.Context, tx *firestore.Transaction) error {
		events = nil
		kept, err := tx.Get(found[0].Ref)
		if err != nil {
			return err
		}
		body := &Ctx{Context: ctx, me: "stripe", now: now, app: a, tx: tx, counters: map[string]*counting{}, command: command}
		if err := do(body, of, kept); err != nil {
			return err
		}
		events, err = body.save()
		return err
	})
	var failure *Failure
	if errors.As(err, &failure) {
		a.log.Printf("one: a Stripe event for %s was refused: %s", of, failure.Message)
		return nil
	}
	if err != nil {
		return err
	}
	a.publish(ctx, events...)
	return nil
}

// stripeCustomer is what's kept of a customer entity's Stripe customer, read in the
// command's transaction, made at Stripe the first time it's needed. It's never made
// twice: its idempotency key is the entity's, and it's kept in the same step.
func (c *Ctx) stripeCustomer(of string) (string, map[string]any, error) {
	a := c.app
	if a.reg.stripe == nil {
		return "", nil, errors.New("one: this app doesn't use Stripe")
	}
	ref := a.store.Collection("stripe_customers").Doc(url.PathEscape(of))
	snap, err := c.tx.Get(ref)
	if err == nil {
		kept := snap.Data()
		if customer, _ := kept["customer"].(string); customer != "" {
			return customer, kept, nil
		}
	} else if status.Code(err) != codes.NotFound {
		return "", nil, err
	}
	made, err := a.stripe.call(c, http.MethodPost, "/v1/customers", url.Values{"metadata[of]": {of}}, "one-customer-"+of)
	if err != nil {
		return "", nil, err
	}
	customer := stripeText(made, "id")
	c.writes = append(c.writes, write{ref: ref, merged: true, data: map[string]any{"of": of, "customer": customer}})
	return customer, map[string]any{"of": of, "customer": customer}, nil
}

// stripePrice finds a price by its lookup key, or makes it from the amount given.
func (c *Ctx) stripePrice(p Price) (string, error) {
	a := c.app
	if id, ok := a.stripe.prices.Load(p.LookupKey); ok {
		return id.(string), nil
	}
	found, err := a.stripe.call(c, http.MethodGet, "/v1/prices", url.Values{"lookup_keys[]": {p.LookupKey}, "active": {"true"}}, "")
	if err != nil {
		return "", err
	}
	var id string
	if prices := stripeItems(found, "data"); len(prices) > 0 {
		if p.Amount != 0 && stripeNumber(prices[0], "unit_amount") != p.Amount {
			return "", fmt.Errorf("one: price %s is %d in Stripe, not %d; a new amount needs a new lookup key", p.LookupKey, stripeNumber(prices[0], "unit_amount"), p.Amount)
		}
		id = stripeText(prices[0], "id")
	} else {
		if p.Amount == 0 {
			return "", &Failure{Status: 400, Message: "there's no price " + p.LookupKey}
		}
		made, err := a.stripe.call(c, http.MethodPost, "/v1/prices", url.Values{
			"lookup_key": {p.LookupKey}, "unit_amount": {strconv.FormatInt(p.Amount, 10)}, "currency": {p.Currency},
			"recurring[interval]": {p.Interval}, "product_data[name]": {p.Name},
		}, "one-price-"+p.LookupKey)
		if err != nil {
			return "", err
		}
		id = stripeText(made, "id")
	}
	a.stripe.prices.Store(p.LookupKey, id)
	return id, nil
}

// once is an idempotency key for one request a command makes, the same each time
// its transaction is tried again, since a command's time is.
func (c *Ctx) once(what string, parts ...string) string {
	sum := sha256.Sum256([]byte(strings.Join(append(parts, c.command, c.me, strconv.FormatInt(c.now.UnixNano(), 10)), "\x00")))
	return "one-" + what + "-" + hex.EncodeToString(sum[:16])
}

// StripeCheckout starts a subscription for a customer entity, like an
// organization, through Stripe's Checkout: its answer shows Checkout's address as
// url, where the person pays. A customer with a subscription already is refused;
// that one is changed with StripeChange.
func (c *Ctx) StripeCheckout(of string, price Price, quantity int, done, canceled string) error {
	customer, kept, err := c.stripeCustomer(of)
	if err != nil {
		return err
	}
	if status, _ := kept["status"].(string); (Subscription{Status: status}).Live() {
		return &Failure{Status: 409, Message: "this already has a subscription; change it instead"}
	}
	priceID, err := c.stripePrice(price)
	if err != nil {
		return err
	}
	q := strconv.Itoa(max(quantity, 1))
	// No proration_behavior: Stripe's default charges the first period as it should,
	// and none would make it free.
	session, err := c.app.stripe.call(c, http.MethodPost, "/v1/checkout/sessions", url.Values{
		"mode": {"subscription"}, "customer": {customer}, "client_reference_id": {of},
		"line_items[0][price]": {priceID}, "line_items[0][quantity]": {q},
		"success_url": {done}, "cancel_url": {canceled},
	}, c.once("checkout", of, priceID, q, done, canceled))
	if err != nil {
		return err
	}
	c.reveal("url", stripeText(session, "url"))
	return nil
}

// StripePortal opens Stripe's Customer Portal for a customer entity, for its card,
// its billing address, its tax ID and its invoices: its answer shows the portal's
// address as url, which leads back to back.
func (c *Ctx) StripePortal(of, back string) error {
	customer, _, err := c.stripeCustomer(of)
	if err != nil {
		return err
	}
	session, err := c.app.stripe.call(c, http.MethodPost, "/v1/billing_portal/sessions", url.Values{"customer": {customer}, "return_url": {back}}, c.once("portal", of, back))
	if err != nil {
		return err
	}
	c.reveal("url", stripeText(session, "url"))
	return nil
}

// stripeSubscribed is a customer entity's subscription, as it was last heard of,
// or a refusal when it has none.
func (c *Ctx) stripeSubscribed(of string) (string, error) {
	_, kept, err := c.stripeCustomer(of)
	if err != nil {
		return "", err
	}
	id, _ := kept["subscription"].(string)
	status, _ := kept["status"].(string)
	if id == "" || !(Subscription{Status: status}).Live() {
		return "", &Failure{Status: 400, Message: "this has no subscription to change"}
	}
	return id, nil
}

// StripeChange moves a customer entity's subscription to another price, another
// quantity or both, prorated. Its item is changed in place, and its quantity always
// given, since a new price on its own would reset it to one.
func (c *Ctx) StripeChange(of string, price Price, quantity int) error {
	id, err := c.stripeSubscribed(of)
	if err != nil {
		return err
	}
	priceID, err := c.stripePrice(price)
	if err != nil {
		return err
	}
	now, err := c.app.stripe.call(c, http.MethodGet, "/v1/subscriptions/"+url.PathEscape(id), nil, "")
	if err != nil {
		return err
	}
	lines := stripeItems(now, "items", "data")
	if len(lines) == 0 {
		return fmt.Errorf("one: Stripe's subscription %s has no item", id)
	}
	q := strconv.Itoa(max(quantity, 1))
	_, err = c.app.stripe.call(c, http.MethodPost, "/v1/subscriptions/"+url.PathEscape(id), url.Values{
		"items[0][id]": {stripeText(lines[0], "id")}, "items[0][price]": {priceID}, "items[0][quantity]": {q},
		"proration_behavior": {"create_prorations"},
	}, c.once("change", of, id, priceID, q))
	return err
}

// StripeCancel ends a customer entity's subscription when its period does, or at
// once.
func (c *Ctx) StripeCancel(of string, atOnce bool) error {
	id, err := c.stripeSubscribed(of)
	if err != nil {
		return err
	}
	if atOnce {
		_, err = c.app.stripe.call(c, http.MethodDelete, "/v1/subscriptions/"+url.PathEscape(id), nil, "")
		return err
	}
	_, err = c.app.stripe.call(c, http.MethodPost, "/v1/subscriptions/"+url.PathEscape(id), url.Values{"cancel_at_period_end": {"true"}}, c.once("cancel", of, id))
	return err
}

// reveal puts something in the command's answer, shown once, like Checkout's address.
func (c *Ctx) reveal(name string, value any) {
	if c.revealed == nil {
		c.revealed = map[string]any{}
	}
	c.revealed[name] = value
}
