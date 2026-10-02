// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one_test

import (
	"net/http"
	"reflect"
	"testing"
	"time"

	"github.com/da0x/uione/one"
)

// A small lending library, like examples/library: books, and loans that point at
// them and change them.

type BookStatus string

const (
	StatusOnShelf   BookStatus = "on_shelf"
	StatusLent      BookStatus = "lent"
	StatusWithdrawn BookStatus = "withdrawn"
)

type Book struct {
	one.Record
	Title  string     `firestore:"title" one:"required"`
	Status BookStatus `firestore:"status" one:"choices=on_shelf|lent|withdrawn,default=on_shelf"`
}

type Loan struct {
	one.Record
	Book       string    `firestore:"book" one:"required,refers=library::book"`
	Member     string    `firestore:"member" one:"default=me"`
	ReturnedAt time.Time `firestore:"returned_at"`
}

// "The Hobbit" is shelved under H.
func SortTitle(title string) string {
	if one.StartsWith(title, "The ") {
		return one.Drop(title, 4)
	}
	return title
}

var library = one.Module("library",
	one.Command[Book]("book::create").Allow(one.SignedIn),
	one.Command[Book]("book::update").Allow(one.SignedIn),
	one.Command[Book]("book::withdraw").Allow(one.SignedIn).Do(func(c *one.Ctx, b *Book) error {
		if b.Status != StatusOnShelf {
			return c.Fail("only a book on the shelf can be withdrawn")
		}
		b.Status = StatusWithdrawn
		return nil
	}),
	one.Command[Loan]("loan::create").Allow(one.SignedIn).Do(func(c *one.Ctx, l *Loan) error {
		book, err := one.Read[Book](c, l.Book)
		if err != nil {
			return err
		}
		if book.Status != StatusOnShelf {
			return c.Fail("that book is not on the shelf")
		}
		book.Status = StatusLent
		return nil
	}),
	one.Command[Loan]("loan::checkin").Allow(one.SignedIn).Do(func(c *one.Ctx, l *Loan) error {
		if !l.ReturnedAt.IsZero() {
			return c.Fail("that book is already back")
		}
		l.ReturnedAt = c.Now()
		book, err := one.Read[Book](c, l.Book)
		if err != nil {
			return err
		}
		book.Status = StatusOnShelf
		return nil
	}),
	// Returns a book while leaving it in a state its rules refuse, so the whole
	// command has to be refused, the loan's change included.
	one.Command[Loan]("loan::spoil").Allow(one.SignedIn).Do(func(c *one.Ctx, l *Loan) error {
		l.ReturnedAt = c.Now()
		book, err := one.Read[Book](c, l.Book)
		if err != nil {
			return err
		}
		book.Status = "misplaced"
		return nil
	}),
	one.View("shelf").Public().
		Each(one.All[Book]().Except("status", StatusWithdrawn)).
		Order(one.By("title", SortTitle)).
		Fields("title", "status").
		Value("lent_to", one.First[Loan]("book", one.Row).And("returned_at", nil), "member"),
	one.View("desk").Public().
		Each(one.Where[Loan]("returned_at", nil)).
		Fields("book.title", "member"),
	// Returns a loan without touching its book, so only the loan changes.
	one.Command[Loan]("loan::close").Allow(one.SignedIn).Do(func(c *one.Ctx, l *Loan) error {
		l.ReturnedAt = c.Now()
		return nil
	}),
	one.View("book_page").Per(one.Entity[Book]()).
		Copy("title", "title").
		Count("loans", one.Where[Loan]("book", one.Subject)).
		Count("out", one.Where[Loan]("book", one.Subject).And("returned_at", nil)).
		Each(one.Where[Loan]("book", one.Subject)).
		Fields("member").
		List("out_now", one.Where[Loan]("book", one.Subject).And("returned_at", nil)).
		Fields("member", "book.title"),
	one.View("mine").PerUser().
		Each(one.Where[Loan]("member", one.Viewer).And("returned_at", nil)).
		Fields("book.title"),
)

func rows(view map[string]any) []map[string]any { return list(view, "rows") }

// list reads one of a view's lists: its rows, or a list with a name.
func list(view map[string]any, name string) []map[string]any {
	var out []map[string]any
	stored, _ := view[name].([]any)
	for _, row := range stored {
		out = append(out, row.(map[string]any))
	}
	return out
}

func column(view map[string]any, name string) []any {
	var out []any
	for _, row := range rows(view) {
		out = append(out, row[name])
	}
	return out
}

func TestTheShelfLeavesOutWithdrawnBooksAndIgnoresTheInItsOrder(t *testing.T) {
	h := start(t)
	_, token := h.signUp("librarian@example.com")
	h.mustRun("library/book/create", token, map[string]any{"title": "The Hobbit"})
	h.mustRun("library/book/create", token, map[string]any{"title": "Dune"})
	anathem := h.mustRun("library/book/create", token, map[string]any{"title": "Anathem"})
	h.mustRun("library/book/create", token, map[string]any{"title": "Ivanhoe"})
	if got := column(h.view("library::shelf"), "title"); !reflect.DeepEqual(got, []any{"Anathem", "Dune", "The Hobbit", "Ivanhoe"}) {
		t.Errorf("the shelf is in the order %v", got)
	}
	h.mustRun("library/book/withdraw", token, map[string]any{"id": anathem})
	if got := column(h.view("library::shelf"), "title"); !reflect.DeepEqual(got, []any{"Dune", "The Hobbit", "Ivanhoe"}) {
		t.Errorf("after withdrawing Anathem the shelf holds %v", got)
	}
}

func TestLendingChangesTheBookInTheSameStep(t *testing.T) {
	h := start(t)
	ada, token := h.signUp("ada@example.com")
	book := h.mustRun("library/book/create", token, map[string]any{"title": "Dune"})
	h.mustRun("library/loan/create", token, map[string]any{"book": book})

	shelf := rows(h.view("library::shelf"))
	if len(shelf) != 1 || shelf[0]["status"] != "lent" || shelf[0]["lent_to"] != ada {
		t.Fatalf("after lending, the shelf shows %v", shelf)
	}
	h.expect("library/loan/create", token, map[string]any{"book": book}, http.StatusBadRequest, "that book is not on the shelf")
	if got := len(rows(h.view("library::desk"))); got != 1 {
		t.Errorf("lending a lent book left %d loans on the desk", got)
	}
	h.expect("library/loan/create", token, map[string]any{"book": "no-such-book"}, http.StatusNotFound, "that book doesn't exist")
}

func TestAChangeToTheBookItPointsAtIsSavedWithTheLoanOrNotAtAll(t *testing.T) {
	h := start(t)
	_, token := h.signUp("ada@example.com")
	book := h.mustRun("library/book/create", token, map[string]any{"title": "Dune"})
	loan := h.mustRun("library/loan/create", token, map[string]any{"book": book})
	h.expect("library/loan/spoil", token, map[string]any{"id": loan}, http.StatusBadRequest,
		"Status has to be one of on_shelf, lent, withdrawn")
	if got := len(rows(h.view("library::desk"))); got != 1 {
		t.Errorf("a refused command still returned the loan: the desk has %d", got)
	}
	if got := column(h.view("library::shelf"), "status"); !reflect.DeepEqual(got, []any{"lent"}) {
		t.Errorf("a refused command still changed the book: %v", got)
	}
}

func TestCheckingInPutsTheBookBackAndClearsTheLoan(t *testing.T) {
	h := start(t)
	_, token := h.signUp("ada@example.com")
	book := h.mustRun("library/book/create", token, map[string]any{"title": "Dune"})
	loan := h.mustRun("library/loan/create", token, map[string]any{"book": book})
	h.mustRun("library/loan/checkin", token, map[string]any{"id": loan})

	shelf := rows(h.view("library::shelf"))
	if len(shelf) != 1 || shelf[0]["status"] != "on_shelf" || shelf[0]["lent_to"] != nil {
		t.Errorf("after checking in, the shelf shows %v", shelf)
	}
	if got := len(rows(h.view("library::desk"))); got != 0 {
		t.Errorf("a returned loan is still on the desk: %d rows", got)
	}
	h.expect("library/loan/checkin", token, map[string]any{"id": loan}, http.StatusBadRequest, "that book is already back")
}

func TestRenamingABookUpdatesEveryViewThatShowsItsTitle(t *testing.T) {
	h := start(t)
	ada, adaToken := h.signUp("ada@example.com")
	grace, graceToken := h.signUp("grace@example.com")
	dune := h.mustRun("library/book/create", adaToken, map[string]any{"title": "Dune"})
	emma := h.mustRun("library/book/create", adaToken, map[string]any{"title": "Emma"})
	h.mustRun("library/loan/create", adaToken, map[string]any{"book": dune})
	h.mustRun("library/loan/create", graceToken, map[string]any{"book": emma})

	h.mustRun("library/book/update", adaToken, map[string]any{"id": dune, "title": "Dune Messiah"})
	if got := column(h.view("library::mine:"+ada), "book.title"); !reflect.DeepEqual(got, []any{"Dune Messiah"}) {
		t.Errorf("Ada's loans show %v", got)
	}
	if got := column(h.view("library::mine:"+grace), "book.title"); !reflect.DeepEqual(got, []any{"Emma"}) {
		t.Errorf("Grace's loans show %v", got)
	}
	desk := column(h.view("library::desk"), "book.title")
	if len(desk) != 2 || !(desk[0] == "Dune Messiah" || desk[1] == "Dune Messiah") {
		t.Errorf("the desk shows %v", desk)
	}
}

func TestDropCountsLettersNotBytes(t *testing.T) {
	cases := map[string]string{
		one.Drop("The Hobbit", 4): "Hobbit",
		one.Drop("Éclair", 1):     "clair",
		one.Drop("ab", 5):         "",
		one.Drop("ab", 0):         "ab",
	}
	for got, want := range cases {
		if got != want {
			t.Errorf("got %q, want %q", got, want)
		}
	}
}

func TestAViewPerBookHoldsThatBookAndOnlyItsLoans(t *testing.T) {
	h := start(t)
	ada, token := h.signUp("ada@example.com")
	dune := h.mustRun("library/book/create", token, map[string]any{"title": "Dune"})
	emma := h.mustRun("library/book/create", token, map[string]any{"title": "Emma"})
	h.mustRun("library/loan/create", token, map[string]any{"book": dune})

	page := h.view("library::book_page:" + dune)
	if page["title"] != "Dune" || page["loans"] != int64(1) || len(rows(page)) != 1 || rows(page)[0]["member"] != ada {
		t.Fatalf("Dune's page holds %v", page)
	}
	if page["owner_uid"] != "" || page["subject"] != dune {
		t.Errorf("a page per book is keyed by the book, never as someone's own: %v %v", page["owner_uid"], page["subject"])
	}
	if other := h.view("library::book_page:" + emma); other["title"] != "Emma" || other["loans"] != int64(0) {
		t.Errorf("Emma's page holds %v", other)
	}

	if out := list(page, "out_now"); len(out) != 1 || out[0]["member"] != ada || out[0]["book.title"] != "Dune" {
		t.Errorf("Dune's page lists %v as out now", out)
	}

	// A change to a loan alone still reaches its book's page, and each list on it.
	loan := rows(page)[0]["id"].(string)
	h.mustRun("library/loan/close", token, map[string]any{"id": loan})
	page = h.view("library::book_page:" + dune)
	if page["out"] != int64(0) || len(list(page, "out_now")) != 0 || len(rows(page)) != 1 {
		t.Errorf("after the loan closed, Dune's page has %v out, lists %v as out now, and %d loans", page["out"], list(page, "out_now"), len(rows(page)))
	}

	h.mustRun("library/book/update", token, map[string]any{"id": dune, "title": "Dune Messiah"})
	if got := h.view("library::book_page:" + dune)["title"]; got != "Dune Messiah" {
		t.Errorf("renaming the book left its page saying %v", got)
	}
}
