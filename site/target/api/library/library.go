// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Generated from main.one by one. Do not edit.

package library

import (
	"time"

	"github.com/da0x/uione/one"
)

type Book struct {
	one.Record
	Title     string `firestore:"title" one:"required"`
	Author    string `firestore:"author" one:"required"`
	Shelfmark string `firestore:"shelfmark" one:"required,key,unique"`
	Status    string `firestore:"status" one:"choices=on_shelf|lent|withdrawn,default=on_shelf"`
	Summary   string `firestore:"summary"`
}

const (
	StatusOnShelf   = "on_shelf"
	StatusLent      = "lent"
	StatusWithdrawn = "withdrawn"
)

type Loan struct {
	one.Record
	Book       string    `firestore:"book" one:"required,key,refers=library::book"`
	Number     float64   `firestore:"number" one:"key,serial=book"`
	Member     string    `firestore:"member" one:"required,refers=user"`
	LentAt     time.Time `firestore:"lent_at" one:"default=now"`
	DueAt      time.Time `firestore:"due_at" one:"required,after=lent_at"`
	ReturnedAt time.Time `firestore:"returned_at"`
}

var BookCreate = one.Command[Book]("book::create")

var Update = one.Command[Book]("book::update").Fields("title", "author", "summary")

var Withdraw = one.Command[Book]("book::withdraw").
	Do(func(c *one.Ctx, b *Book) error {
		if b.Status != StatusOnShelf {
			return c.Fail("only a book on the shelf can be withdrawn")
		}
		b.Status = StatusWithdrawn
		return nil
	})

var LoanCreate = one.Command[Loan]("loan::create").
	Do(func(c *one.Ctx, l *Loan) error {
		book, err := one.Read[Book](c, l.Book)
		if err != nil {
			return err
		}
		if book.Status != StatusOnShelf {
			return c.Fail("that book is not on the shelf")
		}
		if err := one.DispatchUpdate(c, l.Book, func(dispatched *Book) {
			dispatched.Status = StatusLent
		}, nil, nil); err != nil {
			return err
		}
		return nil
	})

var Checkin = one.Command[Loan]("loan::checkin").
	Do(func(c *one.Ctx, l *Loan) error {
		if !l.ReturnedAt.IsZero() {
			return c.Fail("that book is already back")
		}
		l.ReturnedAt = c.Now()
		if err := one.DispatchUpdate(c, l.Book, func(dispatched *Book) {
			dispatched.Status = StatusOnShelf
		}, nil, nil); err != nil {
			return err
		}
		return nil
	})

var Shelf = one.View("shelf").
	Each(one.All[Book]().Except("status", StatusWithdrawn)).
	Order(one.By("title", SortTitle)).
	Fields("shelfmark", "title", "author", "status").
	Value("lent_to", one.First[Loan]("book", one.Row).And("returned_at", nil), "member")

var Desk = one.View("desk").
	Each(one.Where[Loan]("returned_at", nil)).
	Order("due_at").
	Fields("book.title", "member.picture", "member.name", "due_at")

var BookPage = one.View("book_page").Per(one.Entity[Book]()).
	Copy("title", "title").
	Copy("author", "author").
	Copy("shelfmark", "shelfmark").
	Copy("summary", "summary").
	List("loans", one.Where[Loan]("book", one.Subject)).
	Order("-number").
	Fields("number", "member.picture", "member.name", "lent_at", "returned_at")

var Mine = one.View("mine").PerUser().
	Each(one.Where[Loan]("member", one.Viewer).And("returned_at", nil)).
	Order("due_at").
	Fields("book.title", "due_at")

func SortTitle(title string) string {
	if one.StartsWith(title, "The ") {
		return one.Drop(title, 4)
	}
	if one.StartsWith(title, "An ") {
		return one.Drop(title, 3)
	}
	if one.StartsWith(title, "A ") {
		return one.Drop(title, 2)
	}
	return title
}

var Module = one.Module("library", BookCreate, Update, Withdraw, LoanCreate, Checkin, Shelf, Desk, BookPage, Mine, one.Role("librarian", "book:view", "book:create", "book:update", "book:withdraw", "loan:view", "loan:create", "loan:checkin"))
