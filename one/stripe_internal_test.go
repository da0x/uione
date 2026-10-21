// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one

import (
	"bytes"
	"context"
	"crypto/hmac"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"net/http/httptest"
	"net/url"
	"os"
	"strings"
	"sync"
	"testing"
	"time"
)

// A lodge is Stripe's customer here, as an organization is in neotrac.
type lodge struct {
	Record
	Name string `firestore:"name" one:"required"`
	Plan string `firestore:"plan"`
}

// fakeStripe answers as much of Stripe's API as the provider uses, keeping what it
// was asked, and answering a request sent again with its idempotency key as it did
// the first time.
type fakeStripe struct {
	mu            sync.Mutex
	run           int64 // each fake's customers are its own, as the emulator outlives a test
	asked         []fakeAsk
	answered      map[string][]byte // by idempotency key
	customers     int
	prices        map[string]map[string]any // by lookup key
	pricesMade    int
	subscriptions map[string]map[string]any
	invoices      map[string]map[string]any
}

type fakeAsk struct {
	method, path, idempotency string
	form                      url.Values
}

func newFakeStripe() *fakeStripe {
	return &fakeStripe{run: time.Now().UnixNano(), answered: map[string][]byte{}, prices: map[string]map[string]any{}, subscriptions: map[string]map[string]any{}, invoices: map[string]map[string]any{}}
}

func (f *fakeStripe) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	f.mu.Lock()
	defer f.mu.Unlock()
	r.ParseForm()
	form := r.PostForm
	if r.Method == http.MethodGet {
		form = r.URL.Query()
	}
	key := r.Header.Get("Idempotency-Key")
	f.asked = append(f.asked, fakeAsk{method: r.Method, path: r.URL.Path, idempotency: key, form: form})
	if was, ok := f.answered[key]; ok && key != "" {
		w.Write(was)
		return
	}
	var answer any
	switch {
	case r.Method == http.MethodPost && r.URL.Path == "/v1/customers":
		f.customers++
		answer = map[string]any{"id": f.customer(f.customers)}
	case r.Method == http.MethodGet && r.URL.Path == "/v1/prices":
		var found []any
		if p, ok := f.prices[form.Get("lookup_keys[]")]; ok {
			found = append(found, p)
		}
		answer = map[string]any{"data": found}
	case r.Method == http.MethodPost && r.URL.Path == "/v1/prices":
		f.pricesMade++
		amount := 0.0
		fmt.Sscan(form.Get("unit_amount"), &amount)
		p := map[string]any{"id": fmt.Sprint("price_", f.pricesMade), "lookup_key": form.Get("lookup_key"), "unit_amount": amount}
		f.prices[form.Get("lookup_key")] = p
		answer = p
	case r.Method == http.MethodPost && r.URL.Path == "/v1/checkout/sessions":
		answer = map[string]any{"id": "cs_1", "url": "https://checkout.stripe.test/cs_1"}
	case r.Method == http.MethodPost && r.URL.Path == "/v1/billing_portal/sessions":
		answer = map[string]any{"id": "bps_1", "url": "https://billing.stripe.test/bps_1"}
	case strings.HasPrefix(r.URL.Path, "/v1/subscriptions/"):
		s, ok := f.subscriptions[strings.TrimPrefix(r.URL.Path, "/v1/subscriptions/")]
		if !ok {
			w.WriteHeader(http.StatusNotFound)
			answer = map[string]any{"error": map[string]any{"message": "no such subscription"}}
			break
		}
		if r.Method == http.MethodDelete {
			s["status"] = "canceled"
		}
		answer = s
	case strings.HasPrefix(r.URL.Path, "/v1/invoices/"):
		answer = f.invoices[strings.TrimPrefix(r.URL.Path, "/v1/invoices/")]
	default:
		w.WriteHeader(http.StatusNotFound)
		answer = map[string]any{"error": map[string]any{"message": "the fake doesn't know " + r.URL.Path}}
	}
	out, _ := json.Marshal(answer)
	if key != "" {
		f.answered[key] = out
	}
	w.Write(out)
}

func (f *fakeStripe) customer(n int) string { return fmt.Sprint("cus_", f.run, "_", n) }

// subscription puts a subscription in the fake as Stripe would answer it.
func (f *fakeStripe) subscription(id, customer, status, price string, quantity int) {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.subscriptions[id] = map[string]any{
		"id": id, "customer": customer, "status": status, "cancel_at_period_end": false,
		"items": map[string]any{"data": []any{map[string]any{
			"id": "si_" + id, "quantity": float64(quantity), "current_period_end": float64(1800000000),
			"price": map[string]any{"id": "price_" + price, "lookup_key": price},
		}}},
	}
}

func (f *fakeStripe) asks(method, path string) []fakeAsk {
	f.mu.Lock()
	defer f.mu.Unlock()
	var out []fakeAsk
	for _, a := range f.asked {
		if a.method == method && a.path == path {
			out = append(out, a)
		}
	}
	return out
}

func signedEvent(secret string, body []byte, at time.Time) string {
	t := fmt.Sprint(at.Unix())
	mac := hmac.New(sha256.New, []byte(secret))
	mac.Write([]byte(t + "."))
	mac.Write(body)
	return "t=" + t + ",v1=" + hex.EncodeToString(mac.Sum(nil))
}

func TestAStripeEventIsBelievedOnlyWhenSignedWithinFiveMinutes(t *testing.T) {
	body := []byte(`{"id":"evt_1"}`)
	now := time.Now()
	if !stripeSigned("whsec_x", body, signedEvent("whsec_x", body, now), now) {
		t.Error("an event signed now is believed")
	}
	if stripeSigned("whsec_y", body, signedEvent("whsec_x", body, now), now) {
		t.Error("an event signed with another secret isn't")
	}
	if stripeSigned("whsec_x", []byte(`{"id":"evt_2"}`), signedEvent("whsec_x", body, now), now) {
		t.Error("an event changed after it was signed isn't")
	}
	if stripeSigned("whsec_x", body, signedEvent("whsec_x", body, now.Add(-6*time.Minute)), now) {
		t.Error("an event signed six minutes ago isn't")
	}
	if stripeSigned("whsec_x", body, "", now) {
		t.Error("an event with no signature isn't")
	}
}

// stripeApp is an app selling clubs subscriptions, against a fake Stripe, with what
// its handlers were told.
type stripeApp struct {
	a        *App
	fake     *fakeStripe
	server   *httptest.Server
	mu       sync.Mutex
	told     []Subscription
	invoices []Invoice
	ctx      context.Context
}

func startStripe(t *testing.T) *stripeApp {
	if os.Getenv("FIRESTORE_EMULATOR_HOST") == "" {
		os.Setenv("FIRESTORE_EMULATOR_HOST", "localhost:8080")
		os.Setenv("FIREBASE_AUTH_EMULATOR_HOST", "localhost:9099")
	}
	s := &stripeApp{fake: newFakeStripe(), ctx: context.Background()}
	price := Price{LookupKey: fmt.Sprint("team_", time.Now().UnixNano()), Amount: 900, Currency: "usd", Interval: "month", Name: "Team"}
	var err error
	s.a, err = New(s.ctx, Module("guild",
		Command[lodge]("lodge::create").Allow(SignedIn),
		Command[lodge]("lodge::subscribe").Allow(SignedIn).Do(func(c *Ctx, e *lodge) error {
			return c.StripeCheckout(e.ID, price, 3, "https://guild.test/done", "https://guild.test/back")
		}),
		Command[lodge]("lodge::billing").Allow(SignedIn).Do(func(c *Ctx, e *lodge) error {
			return c.StripePortal(e.ID, "https://guild.test/settings")
		}),
		Command[lodge]("lodge::grow").Allow(SignedIn).Do(func(c *Ctx, e *lodge) error {
			return c.StripeChange(e.ID, price, 5)
		}),
		Command[lodge]("lodge::leave").Allow(SignedIn).Do(func(c *Ctx, e *lodge) error {
			return c.StripeCancel(e.ID, false)
		}),
		Stripe("/hooks/stripe").For(Entity[lodge]()).
			OnSubscription(func(c *Ctx, sub Subscription) error {
				s.mu.Lock()
				s.told = append(s.told, sub)
				s.mu.Unlock()
				return DispatchUpdate[lodge](c, sub.Of, func(e *lodge) { e.Plan = sub.Status + " " + sub.Price }, nil, nil)
			}).
			OnInvoice(func(c *Ctx, i Invoice) error {
				s.mu.Lock()
				s.invoices = append(s.invoices, i)
				s.mu.Unlock()
				return nil
			}),
	))
	if err != nil {
		t.Fatal(err)
	}
	stripe := httptest.NewServer(s.fake)
	s.a.stripe = &stripeAPI{key: "sk_test_x", signing: "whsec_x", base: stripe.URL, client: stripe.Client()}
	s.server = httptest.NewServer(s.a.Handler())
	t.Cleanup(func() { s.server.Close(); stripe.Close(); s.a.Close() })
	return s
}

func (s *stripeApp) run(t *testing.T, command, id string) (map[string]any, error) {
	t.Helper()
	input := map[string]any{"id": id}
	if id == "" {
		input = map[string]any{"name": "Chess"}
	}
	run := &call{ctx: s.ctx, me: "ada", input: input}
	made, err := s.a.reg.commands["guild::lodge::"+command](s.a, run)
	if err != nil {
		return nil, err
	}
	answer := map[string]any{"id": made}
	for k, v := range run.revealed {
		answer[k] = v
	}
	return answer, nil
}

func (s *stripeApp) send(t *testing.T, id, kind string, object map[string]any, signedAt time.Time) int {
	t.Helper()
	body, _ := json.Marshal(map[string]any{"id": id, "type": kind, "data": map[string]any{"object": object}})
	request, _ := http.NewRequest(http.MethodPost, s.server.URL+"/hooks/stripe", bytes.NewReader(body))
	request.Header.Set("Stripe-Signature", signedEvent("whsec_x", body, signedAt))
	response, err := http.DefaultClient.Do(request)
	if err != nil {
		t.Fatal(err)
	}
	response.Body.Close()
	return response.StatusCode
}

func TestACheckoutMakesItsCustomerAndPriceOnce(t *testing.T) {
	s := startStripe(t)
	made, err := s.run(t, "create", "")
	if err != nil {
		t.Fatal(err)
	}
	id := made["id"].(string)
	answer, err := s.run(t, "subscribe", id)
	if err != nil {
		t.Fatal(err)
	}
	if answer["url"] != "https://checkout.stripe.test/cs_1" {
		t.Fatalf("the answer shows %v, not Checkout's address", answer)
	}
	// A checkout again, abandoned the first time, reuses both.
	s.a.stripe.prices = sync.Map{}
	if _, err := s.run(t, "subscribe", id); err != nil {
		t.Fatal(err)
	}
	if s.fake.customers != 1 || s.fake.pricesMade != 1 {
		t.Fatalf("made %d customers and %d prices, not one of each", s.fake.customers, s.fake.pricesMade)
	}
	checkouts := s.fake.asks(http.MethodPost, "/v1/checkout/sessions")
	if len(checkouts) != 2 {
		t.Fatalf("%d checkouts", len(checkouts))
	}
	sent := checkouts[0].form
	if sent.Get("customer") != s.fake.customer(1) || sent.Get("client_reference_id") != id || sent.Get("mode") != "subscription" ||
		sent.Get("line_items[0][quantity]") != "3" || sent.Get("line_items[0][price]") != "price_1" || sent.Has("customer_email") {
		t.Fatalf("Checkout was sent %v", sent)
	}
	// Its default charges the first period; none would make it free.
	if sent.Has("proration_behavior") || sent.Has("subscription_data[proration_behavior]") {
		t.Fatalf("Checkout was told how to prorate: %v", sent)
	}
	for _, ask := range s.fake.asked {
		if ask.method == http.MethodPost && ask.idempotency == "" {
			t.Fatalf("POST %s had no idempotency key", ask.path)
		}
	}
	// Stripe's own customer is kept where no command reaches it.
	kept, err := s.a.store.Collection("stripe_customers").Doc(id).Get(s.ctx)
	if err != nil || kept.Data()["customer"] != s.fake.customer(1) || kept.Data()["of"] != id {
		t.Fatalf("the customer is kept as %v (%v)", kept.Data(), err)
	}
	if answer, err := s.run(t, "billing", id); err != nil || answer["url"] != "https://billing.stripe.test/bps_1" {
		t.Fatalf("the portal: %v %v", answer, err)
	}
}

func TestStripesEventsAreHandledOnceAsStripeSaysThingsAreNow(t *testing.T) {
	s := startStripe(t)
	made, _ := s.run(t, "create", "")
	id := made["id"].(string)
	if _, err := s.run(t, "subscribe", id); err != nil {
		t.Fatal(err)
	}
	now := time.Now()
	sub := fmt.Sprint("sub_", now.UnixNano())
	s.fake.subscription(sub, s.fake.customer(1), "active", "team_monthly", 3)
	if code := s.send(t, "evt_a"+sub, "checkout.session.completed", map[string]any{"id": "cs_1", "subscription": sub}, now); code != http.StatusOK {
		t.Fatalf("the event was answered %d", code)
	}
	// Sent again, it changes nothing.
	s.send(t, "evt_a"+sub, "checkout.session.completed", map[string]any{"id": "cs_1", "subscription": sub}, now)
	if len(s.told) != 1 || s.told[0].Of != id || s.told[0].Status != "active" || s.told[0].Price != "team_monthly" || s.told[0].Quantity != 3 ||
		!s.told[0].PeriodEnd.Equal(time.Unix(1800000000, 0)) {
		t.Fatalf("the app was told %+v", s.told)
	}
	if plan := s.lodge(t, id).Plan; plan != "active team_monthly" {
		t.Fatalf("the lodge's plan is %q", plan)
	}
	// An event that comes late says what was, but Stripe is asked how it is now.
	s.fake.subscription(sub, s.fake.customer(1), "past_due", "team_monthly", 3)
	s.send(t, "evt_b"+sub, "customer.subscription.updated", map[string]any{"id": sub, "status": "active"}, now)
	if last := s.told[len(s.told)-1]; last.Status != "past_due" {
		t.Fatalf("the late event told the app %q, not how it is now", last.Status)
	}
	// A subscription that ended, heard of after another started, doesn't undo it.
	newer := sub + "_2"
	s.fake.subscription(newer, s.fake.customer(1), "active", "team_yearly", 4)
	s.send(t, "evt_c"+sub, "customer.subscription.created", map[string]any{"id": newer}, now)
	s.fake.subscription(sub, s.fake.customer(1), "canceled", "team_monthly", 3)
	s.send(t, "evt_d"+sub, "customer.subscription.deleted", map[string]any{"id": sub}, now)
	if plan := s.lodge(t, id).Plan; plan != "active team_yearly" {
		t.Fatalf("the lodge's plan is %q after an old subscription ended", plan)
	}
	// One already subscribed doesn't check out again; it changes what it has.
	if _, err := s.run(t, "subscribe", id); !refusedWith(err, 409) {
		t.Fatalf("a second checkout: %v", err)
	}
	if _, err := s.run(t, "grow", id); err != nil {
		t.Fatal(err)
	}
	changes := s.fake.asks(http.MethodPost, "/v1/subscriptions/"+newer)
	if len(changes) != 1 {
		t.Fatalf("%d changes", len(changes))
	}
	if sent := changes[0].form; sent.Get("items[0][id]") != "si_"+newer || sent.Get("items[0][quantity]") != "5" || sent.Get("proration_behavior") != "create_prorations" {
		t.Fatalf("the change sent %v", sent)
	}
	if _, err := s.run(t, "leave", id); err != nil {
		t.Fatal(err)
	}
	if sent := s.fake.asks(http.MethodPost, "/v1/subscriptions/"+newer)[1].form; sent.Get("cancel_at_period_end") != "true" {
		t.Fatalf("leaving sent %v", sent)
	}
	// An invoice, paid.
	s.fake.mu.Lock()
	s.fake.invoices["in_1"+sub] = map[string]any{"id": "in_1" + sub, "customer": s.fake.customer(1), "status": "paid", "amount_paid": float64(2700), "currency": "usd",
		"parent": map[string]any{"subscription_details": map[string]any{"subscription": newer}}}
	s.fake.mu.Unlock()
	s.send(t, "evt_e"+sub, "invoice.paid", map[string]any{"id": "in_1" + sub}, now)
	if len(s.invoices) != 1 || !s.invoices[0].Paid || s.invoices[0].Of != id || s.invoices[0].Subscription != newer || s.invoices[0].AmountPaid != 2700 {
		t.Fatalf("the app was told of invoices %+v", s.invoices)
	}
	// Signed with another secret, or long ago, it's refused.
	body, _ := json.Marshal(map[string]any{"id": "evt_f", "type": "invoice.paid"})
	request, _ := http.NewRequest(http.MethodPost, s.server.URL+"/hooks/stripe", bytes.NewReader(body))
	request.Header.Set("Stripe-Signature", signedEvent("whsec_other", body, now))
	if response, _ := http.DefaultClient.Do(request); response.StatusCode != http.StatusUnauthorized {
		t.Fatalf("an event signed with another secret was answered %d", response.StatusCode)
	}
	if code := s.send(t, "evt_g", "invoice.paid", map[string]any{"id": "in_1" + sub}, now.Add(-10*time.Minute)); code != http.StatusUnauthorized {
		t.Fatalf("an event signed ten minutes ago was answered %d", code)
	}
}

func (s *stripeApp) lodge(t *testing.T, id string) lodge {
	t.Helper()
	snap, err := s.a.store.Collection(s.a.reg.schemas[Entity[lodge]().typ].collection).Doc(id).Get(s.ctx)
	if err != nil {
		t.Fatal(err)
	}
	var c lodge
	snap.DataTo(&c)
	return c
}

func refusedWith(err error, code int) bool {
	var failure *Failure
	return errors.As(err, &failure) && failure.Status == code
}
