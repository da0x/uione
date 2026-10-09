// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one_test

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"os"
	"slices"
	"testing"
	"time"

	"github.com/da0x/uione/one"
)

// Clubs with racks of tricks, each trick keeping its history. A person's news is
// every change of a trick on a rack they follow, or that's dealt to them, and only
// while they may read the trick's page: a private club's tricks are its members'.

type Club struct {
	one.Record
	Slug       string `firestore:"slug" one:"required,unique,key"`
	Visibility string `firestore:"visibility" one:"choices=public|private,default=public"`
}

type Seating struct {
	one.Record
	Club   string `firestore:"club" one:"required,key,refers=club::club"`
	Person string `firestore:"person" one:"required,key,refers=user"`
	Role   string `firestore:"role" one:"choices=player,default=player"`
}

type Rack struct {
	one.Record
	Club      string   `firestore:"club" one:"required,refers=club::club"`
	Name      string   `firestore:"name" one:"required"`
	Followers []string `firestore:"followers" one:"refers=user"`
}

type Trick struct {
	one.Record
	one.History
	Club  string   `firestore:"club" one:"required,refers=club::club"`
	Rack  string   `firestore:"rack" one:"required,refers=club::rack"`
	Title string   `firestore:"title" one:"required"`
	Dealt []string `firestore:"dealt" one:"refers=user"`
}

// A note on a trick, kept in the trick's history, naming people with @username.
type Aside struct {
	one.Record
	Trick     string   `firestore:"trick" one:"required,refers=club::trick,history"`
	Body      string   `firestore:"body" one:"required"`
	Mentioned []string `firestore:"mentioned" one:"refers=user,mentions=body"`
}

// An invitation to a club by email, until someone signs in with it.
type Invite struct {
	one.Record
	Club  string `firestore:"club" one:"required,key,refers=club::club"`
	Email string `firestore:"email" one:"required,key,email"`
}

// When a person last looked at their news, one for each person.
type Glance struct {
	one.Record
	Person string    `firestore:"person" one:"key,default=me,refers=user"`
	SeenAt time.Time `firestore:"seen_at"`
}

var clubs = one.Module("club",
	one.Command[Invite]("invite::create"),
	one.Invites[Invite, Seating]("email", "person"),
	one.Command[Glance]("glance::create").Allow(one.SignedIn).Do(func(c *one.Ctx, g *Glance) error {
		g.SeenAt = time.Now()
		return nil
	}),
	one.Command[Club]("club::create").Allow(one.SignedIn).Do(func(c *one.Ctx, k *Club) error {
		return one.Create(c, &Seating{Club: k.ID, Person: c.Me()})
	}),
	one.Command[Club]("club::update"),
	one.Command[Seating]("seating::create"),
	one.Command[Seating]("seating::delete"),
	one.Command[Rack]("rack::create"),
	one.Command[Rack]("rack::follow").Allow(one.SignedIn).Do(func(c *one.Ctx, r *Rack) error {
		r.Followers = append(r.Followers, c.Me())
		return nil
	}),
	one.Command[Trick]("trick::create"),
	one.Command[Aside]("aside::create").Allow(one.SignedIn),
	one.Command[Aside]("aside::update").Allow(one.Owner),
	one.Command[Trick]("trick::update"),
	one.Role("player", "club:update", "seating:create", "seating:delete", "rack:create", "trick:create", "trick:update", "invite:create").
		Per(one.Entity[Club](), one.Entity[Seating]()),
	one.View("trick_page").Per(one.Entity[Trick]()).Readers(one.Entity[Seating]()).PublicWhen("club.visibility", "public").
		Copy("title", "title"),
	one.View("news").PerUser().
		List("news", one.All[one.ChangeOf[Trick]]().Except("created_by", one.Viewer).Has("rack.followers", one.Viewer).
			Or(one.All[one.ChangeOf[Trick]]().Has("dealt", one.Viewer).Except("created_by", one.Viewer)).
			Or(one.All[one.ChangeOf[Trick]]().Has("mentioned", one.Viewer).Except("created_by", one.Viewer))).
		Fields("trick", "field", "after", "created_at").
		FirstOf("seen", one.Where[Glance]("person", one.Viewer), "seen_at").
		CountRows("unread", "news", one.RowWhere("created_at", ">", "seen")),
	// The two newest changes someone else made, however many of their own came after.
	one.View("others").PerUser().
		List("others", one.All[one.ChangeOf[Trick]]().Except("created_by", one.Viewer)).Order("-created_at").Limit(2).
		Fields("created_by", "trick"),
	// A club's racks in an order of their own, by club, which every rack here ties on.
	one.View("racks").Per(one.Entity[Club]()).Readers(one.Entity[Seating]()).PublicWhen("visibility", "public").
		List("racks", one.Where[Rack]("club", one.Subject)).Order("club").Fields("name"),
	one.View("dealt").PerUser().Each(one.All[Trick]().Has("dealt", one.Viewer)).Fields("title"),
)

func init() { modules = append(modules, clubs) }

// news is what a person's news says: each change's field and what it became, in
// the order they were made.
func (h *harness) news(person string) []string {
	h.t.Helper()
	var said []string
	for _, row := range list(h.view("club::news:"+person), "news") {
		after, _ := row["after"].(string)
		said = append(said, row["field"].(string)+":"+after)
	}
	slices.Sort(said)
	return said
}

func TestNewsHoldsTheChangesOnRacksFollowedAndTricksDealt(t *testing.T) {
	h := start(t)
	ada, adaToken := h.signUp("ada@example.com")
	grace, graceToken := h.signUp("grace@example.com")
	h.mustRun("club/club/create", adaToken, map[string]any{"slug": "bridge"})
	followed := h.mustRun("club/rack/create", adaToken, map[string]any{"club": "bridge", "name": "Openings"})
	other := h.mustRun("club/rack/create", adaToken, map[string]any{"club": "bridge", "name": "Endgames"})
	early := h.mustRun("club/trick/create", adaToken, map[string]any{"club": "bridge", "rack": followed, "title": "Early"})
	h.mustRun("club/trick/create", adaToken, map[string]any{"club": "bridge", "rack": other, "title": "Elsewhere"})

	// Following a rack brings in what already happened on it.
	h.mustRun("club/rack/follow", graceToken, map[string]any{"id": followed})
	if got := h.news(grace); !slices.Equal(got, []string{":"}) {
		t.Fatalf("after following, Grace's news is %v, want the early trick's making", got)
	}
	// A change on it later, by someone else, is in it too.
	h.mustRun("club/trick/update", adaToken, map[string]any{"id": early, "title": "Early, renamed"})
	// And a trick dealt to her on a rack she doesn't follow.
	h.mustRun("club/trick/create", adaToken, map[string]any{"club": "bridge", "rack": other, "title": "Hers", "dealt": []string{grace}})
	want := []string{":", ":", "title:Early, renamed"}
	if got := h.news(grace); !slices.Equal(got, want) {
		t.Errorf("Grace's news is %v, want %v", got, want)
	}

	// What a person did themselves isn't news to them.
	h.mustRun("club/rack/follow", adaToken, map[string]any{"id": followed})
	if got := h.news(ada); len(got) != 0 {
		t.Errorf("Ada's news holds her own changes: %v", got)
	}
	h.mustRun("club/seating/create", adaToken, map[string]any{"club": "bridge", "person": grace})
	h.mustRun("club/trick/update", graceToken, map[string]any{"id": early, "title": "Early, by Grace"})
	if got := h.news(ada); !slices.Equal(got, []string{"title:Early, by Grace"}) {
		t.Errorf("Ada's news is %v, want Grace's change", got)
	}
}

func TestNewsAndWhatsDealtLeaveOutWhatAPersonMayNoLongerRead(t *testing.T) {
	h := start(t)
	_, adaToken := h.signUp("ada@example.com")
	grace, graceToken := h.signUp("grace@example.com")
	h.mustRun("club/club/create", adaToken, map[string]any{"slug": "poker", "visibility": "private"})
	rack := h.mustRun("club/rack/create", adaToken, map[string]any{"club": "poker", "name": "Hands"})
	h.mustRun("club/trick/create", adaToken, map[string]any{"club": "poker", "rack": rack, "title": "Aces", "dealt": []string{grace}})

	// Grace isn't in the private club, so following its rack, or being dealt one of
	// its tricks, shows her nothing.
	h.mustRun("club/rack/follow", graceToken, map[string]any{"id": rack})
	if got := h.news(grace); len(got) != 0 {
		t.Errorf("outside the private club, Grace's news is %v", got)
	}
	if got := list(h.view("club::dealt:"+grace), "rows"); len(got) != 0 {
		t.Errorf("outside the private club, Grace is shown the tricks %v", got)
	}

	// Seated, she reads them.
	seat := h.mustRun("club/seating/create", adaToken, map[string]any{"club": "poker", "person": grace})
	if got := h.news(grace); len(got) != 1 {
		t.Errorf("seated, Grace's news is %v", got)
	}
	if got := list(h.view("club::dealt:"+grace), "rows"); len(got) != 1 {
		t.Errorf("seated, Grace is shown the tricks %v", got)
	}

	// Unseated, she doesn't any more.
	h.mustRun("club/seating/delete", adaToken, map[string]any{"id": seat})
	if got := h.news(grace); len(got) != 0 {
		t.Errorf("unseated, Grace's news is %v", got)
	}

	// Made public, the club's tricks are anyone's to read.
	h.mustRun("club/club/update", adaToken, map[string]any{"id": "poker", "visibility": "public"})
	if got := h.news(grace); len(got) != 1 {
		t.Errorf("once the club is public, Grace's news is %v", got)
	}
	if got := list(h.view("club::dealt:"+grace), "rows"); len(got) != 1 {
		t.Errorf("once the club is public, Grace is shown the tricks %v", got)
	}
}

func TestNewsSaysWhenThePersonLastLookedAtIt(t *testing.T) {
	h := start(t)
	grace, graceToken := h.signUp("grace@example.com")
	ada, adaToken := h.signUp("ada@example.com")
	h.mustRun("club/club/create", adaToken, map[string]any{"slug": "chess"})
	rack := h.mustRun("club/rack/create", adaToken, map[string]any{"club": "chess", "name": "Openings"})
	h.mustRun("club/rack/follow", graceToken, map[string]any{"id": rack})
	if seen := h.view("club::news:" + grace)["seen"]; seen != nil {
		t.Fatalf("before she's looked, Grace's news says she looked at %v", seen)
	}
	h.mustRun("club/glance/create", graceToken, map[string]any{})
	first, ok := h.view("club::news:" + grace)["seen"].(time.Time)
	if !ok {
		t.Fatalf("after looking, Grace's news says she looked at %v", h.view("club::news:" + grace)["seen"])
	}
	h.mustRun("club/glance/create", graceToken, map[string]any{})
	if again, _ := h.view("club::news:" + grace)["seen"].(time.Time); !again.After(first) {
		t.Errorf("looking again moved when she looked from %v to %v", first, again)
	}
	if seen := h.view("club::news:" + ada)["seen"]; seen != nil {
		t.Errorf("Ada's news says she looked, when only Grace did: %v", seen)
	}
}

func TestNewsCountsWhatsNewSinceThePersonLastLooked(t *testing.T) {
	h := start(t)
	grace, graceToken := h.signUp("grace@example.com")
	_, adaToken := h.signUp("ada@example.com")
	h.mustRun("club/club/create", adaToken, map[string]any{"slug": "chess"})
	rack := h.mustRun("club/rack/create", adaToken, map[string]any{"club": "chess", "name": "Openings"})
	h.mustRun("club/rack/follow", graceToken, map[string]any{"id": rack})
	trick := h.mustRun("club/trick/create", adaToken, map[string]any{"club": "chess", "rack": rack, "title": "Italian"})
	h.mustRun("club/trick/update", adaToken, map[string]any{"id": trick, "title": "Italian game"})
	unread := func() any { return h.view("club::news:" + grace)["unread"] }
	// Before she's ever looked, all of it is new.
	if got := unread(); got != int64(2) {
		t.Fatalf("before Grace looks, her news counts %v new, want 2", got)
	}
	h.mustRun("club/glance/create", graceToken, map[string]any{})
	if got := unread(); got != int64(0) {
		t.Errorf("right after Grace looks, her news counts %v new, want 0", got)
	}
	h.mustRun("club/trick/update", adaToken, map[string]any{"id": trick, "title": "Italian opening"})
	if got := unread(); got != int64(1) {
		t.Errorf("after one more change, Grace's news counts %v new, want 1", got)
	}
}

func TestTheNewestRowsOfAListAreFoundPastPagesOfOnesLeftOut(t *testing.T) {
	h := start(t)
	ada, adaToken := h.signUp("ada@example.com")
	grace, graceToken := h.signUp("grace@example.com")
	h.mustRun("club/club/create", adaToken, map[string]any{"slug": "go"})
	h.mustRun("club/seating/create", adaToken, map[string]any{"club": "go", "person": grace})
	rack := h.mustRun("club/rack/create", adaToken, map[string]any{"club": "go", "name": "Joseki"})
	first := h.mustRun("club/trick/create", graceToken, map[string]any{"club": "go", "rack": rack, "title": "First"})
	second := h.mustRun("club/trick/create", graceToken, map[string]any{"club": "go", "rack": rack, "title": "Second"})
	mine := h.mustRun("club/trick/create", adaToken, map[string]any{"club": "go", "rack": rack, "title": "Mine"})
	for i := range 25 {
		h.mustRun("club/trick/update", adaToken, map[string]any{"id": mine, "title": fmt.Sprint("Mine ", i)})
	}
	var got []string
	for _, row := range list(h.view("club::others:"+ada), "others") {
		got = append(got, fmt.Sprint(row["created_by"] == grace, " ", row["trick"]))
	}
	if want := []string{"true " + second, "true " + first}; !slices.Equal(got, want) {
		t.Errorf("the two newest changes Ada didn't make are %v, want %v", got, want)
	}
}

func TestRowsThatTieInAListsOwnOrderStayInTheOrderTheyWereMade(t *testing.T) {
	h := start(t)
	_, adaToken := h.signUp("ada@example.com")
	h.mustRun("club/club/create", adaToken, map[string]any{"slug": "bridge"})
	for _, name := range []string{"First", "Second", "Third"} {
		h.mustRun("club/rack/create", adaToken, map[string]any{"club": "bridge", "name": name})
	}
	var got []string
	for _, row := range list(h.view("club::racks:bridge"), "racks") {
		got = append(got, row["name"].(string))
	}
	if want := []string{"First", "Second", "Third"}; !slices.Equal(got, want) {
		t.Errorf("the racks are in the order %v, want %v, as they were made", got, want)
	}
}

func TestANoteMentioningSomeoneIsInTheirNewsAsAChangeOfItsTrick(t *testing.T) {
	h := start(t)
	ada, adaToken := h.signUp("ada@example.com")
	grace, _ := h.signUp("grace@example.com")
	if _, err := h.store.Collection("users").Doc(grace).Set(context.Background(), map[string]any{"username": "GraceH"}); err != nil {
		t.Fatal(err)
	}
	h.mustRun("club/club/create", adaToken, map[string]any{"slug": "whist"})
	rack := h.mustRun("club/rack/create", adaToken, map[string]any{"club": "whist", "name": "Leads"})
	trick := h.mustRun("club/trick/create", adaToken, map[string]any{"club": "whist", "rack": rack, "title": "Lead low"})
	if got := h.news(grace); len(got) != 0 {
		t.Fatalf("before she's named, Grace's news is %v", got)
	}
	note := h.mustRun("club/aside/create", adaToken, map[string]any{"trick": trick, "body": "What do you think, @gracehg? And @nobody, and ada@example.com."})
	if got := h.stored("club_aside", note)["mentioned"]; got != nil && len(got.([]any)) != 0 {
		t.Errorf("a name nobody has, and an address, mention %v", got)
	}
	note = h.mustRun("club/aside/create", adaToken, map[string]any{"trick": trick, "body": "What do you think, @GraceH?"})
	if got := h.stored("club_aside", note)["mentioned"]; fmt.Sprint(got) != fmt.Sprint([]any{grace}) {
		t.Errorf("the note mentions %v, want Grace", got)
	}
	if got := h.news(grace); !slices.Equal(got, []string{"aside:"}) {
		t.Errorf("Grace's news is %v, want the note that named her", got)
	}
	_ = ada
}

// verify marks someone's email verified, as a Google sign-in's is, and returns a
// token that says so.
func (h *harness) verify(id, email string) string {
	h.t.Helper()
	host := "http://" + os.Getenv("FIREBASE_AUTH_EMULATOR_HOST")
	body, _ := json.Marshal(map[string]any{"localId": id, "emailVerified": true})
	request, _ := http.NewRequest(http.MethodPost, host+"/identitytoolkit.googleapis.com/v1/projects/"+project+"/accounts:update", bytes.NewReader(body))
	request.Header.Set("Authorization", "Bearer owner")
	request.Header.Set("Content-Type", "application/json")
	if response, err := http.DefaultClient.Do(request); err != nil || response.StatusCode != http.StatusOK {
		h.t.Fatalf("the Auth emulator didn't verify %s: %v", email, err)
	}
	body, _ = json.Marshal(map[string]any{"email": email, "password": "password", "returnSecureToken": true})
	response, err := http.Post(host+"/identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=any", "application/json", bytes.NewReader(body))
	if err != nil {
		h.t.Fatal(err)
	}
	defer response.Body.Close()
	var account struct {
		IDToken string `json:"idToken"`
	}
	json.NewDecoder(response.Body).Decode(&account)
	return account.IDToken
}

// signIn tells the backend someone's opened the app.
func (h *harness) signIn(token string) {
	h.t.Helper()
	request, _ := http.NewRequest(http.MethodPost, h.server.URL+"/api/signin", nil)
	request.Header.Set("Authorization", "Bearer "+token)
	response, err := http.DefaultClient.Do(request)
	if err != nil || response.StatusCode != http.StatusOK {
		h.t.Fatalf("signing in: %v %v", err, response)
	}
}

func TestAnInvitationByEmailMakesWhoeverSignsInWithItAMember(t *testing.T) {
	h := start(t)
	_, adaToken := h.signUp("ada@example.com")
	h.mustRun("club/club/create", adaToken, map[string]any{"slug": "go", "visibility": "private"})
	h.mustRun("club/invite/create", adaToken, map[string]any{"club": "go", "email": "Grace@Example.com"})

	// Someone whose sign-in doesn't vouch for the email gets nothing from it.
	grace, unverified := h.signUp("grace@example.com")
	h.signIn(unverified)
	if seats, _ := h.store.Collection("club_seating").Where("club", "==", "go").Documents(context.Background()).GetAll(); len(seats) != 1 {
		t.Fatalf("an unverified email joined the club: %d seats", len(seats))
	}

	graceToken := h.verify(grace, "grace@example.com")
	h.signIn(graceToken)
	seats, _ := h.store.Collection("club_seating").Where("person", "==", grace).Documents(context.Background()).GetAll()
	if len(seats) != 1 || seats[0].Data()["club"] != "go" {
		t.Errorf("signing in with the invited email gave Grace the seats %v", seats)
	}
	if invites, _ := h.store.Collection("club_invite").Documents(context.Background()).GetAll(); len(invites) != 0 {
		t.Errorf("the invitation is still there once it's taken: %d", len(invites))
	}
	// Signing in again changes nothing.
	h.signIn(graceToken)
	if again, _ := h.store.Collection("club_seating").Where("person", "==", grace).Documents(context.Background()).GetAll(); len(again) != 1 {
		t.Errorf("signing in again gave Grace %d seats", len(again))
	}
}
