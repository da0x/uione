// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one_test

import (
	"testing"

	"github.com/da0x/uione/one"
)

// A hall of shelves, each with its own visibility: a book's page is public only
// while both its shelf and the shelf's hall are, read through two pointers, and it
// closes again as soon as either is made private.

type Hall struct {
	one.Record
	Slug       string `firestore:"slug" one:"required,unique,key"`
	Visibility string `firestore:"visibility" one:"choices=public|private,default=public"`
}

type Steward struct {
	one.Record
	Hall   string `firestore:"hall" one:"required,key,refers=hall::hall"`
	Person string `firestore:"person" one:"required,key,refers=user"`
	Role   string `firestore:"role" one:"choices=keeper,default=keeper"`
}

type Alcove struct {
	one.Record
	Hall       string `firestore:"hall" one:"required,refers=hall::hall"`
	Name       string `firestore:"name" one:"required"`
	Visibility string `firestore:"visibility" one:"choices=public|private,default=public"`
}

type Volume struct {
	one.Record
	Hall   string `firestore:"hall" one:"required,refers=hall::hall"`
	Alcove string `firestore:"alcove" one:"required,refers=hall::alcove"`
	Title  string `firestore:"title" one:"required"`
	Cover  string `firestore:"cover" one:"file"`
}

// A note on a volume, with a scan, and no page of its own.
type Marginal struct {
	one.Record
	Hall   string `firestore:"hall" one:"required,refers=hall::hall"`
	Volume string `firestore:"volume" one:"required,refers=hall::volume"`
	Scan   string `firestore:"scan" one:"file"`
}

var halls = one.Module("hall",
	one.Command[Hall]("hall::create").Allow(one.SignedIn).Do(func(c *one.Ctx, h *Hall) error {
		return one.Create(c, &Steward{Hall: h.ID, Person: c.Me(), Role: "keeper"})
	}),
	one.Command[Hall]("hall::update"),
	one.Command[Alcove]("alcove::create"),
	one.Command[Alcove]("alcove::update"),
	one.Command[Volume]("volume::create"),
	one.Command[Volume]("volume::update"),
	one.Command[Marginal]("marginal::create"),
	one.Role("keeper", "hall:update", "alcove:create", "alcove:update", "volume:create", "volume:update", "marginal:create").Per(one.Entity[Hall](), one.Entity[Steward]()),
	one.View("volume_page").Per(one.Entity[Volume]()).Readers(one.Entity[Steward]()).
		PublicWhen("alcove.visibility", "public").PublicWhen("alcove.hall.visibility", "public").
		Copy("title", "title"),
	one.View("hall_page").Per(one.Entity[Hall]()).Readers(one.Entity[Steward]()).PublicWhen("visibility", "public").Copy("slug", "slug"),
)

func init() { modules = append(modules, halls) }

func TestABooksPageIsPublicOnlyWhileItsShelfAndTheShelfsHallBothAre(t *testing.T) {
	h := start(t)
	ada, token := h.signUp("ada@example.com")
	h.mustRun("hall/hall/create", token, map[string]any{"slug": "east"})
	open := h.mustRun("hall/alcove/create", token, map[string]any{"hall": "east", "name": "Atlases"})
	closed := h.mustRun("hall/alcove/create", token, map[string]any{"hall": "east", "name": "Ledgers", "visibility": "private"})
	book := h.mustRun("hall/volume/create", token, map[string]any{"hall": "east", "alcove": closed, "title": "Accounts of 1820"})

	page := func() map[string]any {
		t.Helper()
		return h.view("hall::volume_page:" + book)
	}
	if p := page(); p["public"] == true {
		t.Fatalf("a book on a private shelf in a public hall is public: %v", p)
	}
	if readers, _ := page()["readers"].([]any); len(readers) != 1 || readers[0] != ada {
		t.Fatalf("the hall's people read it: %v", readers)
	}

	// Moved to an open shelf, it's open; the hall made private closes it again,
	// through two pointers; the hall made public opens it once more.
	h.mustRun("hall/volume/update", token, map[string]any{"id": book, "alcove": open})
	if p := page(); p["public"] != true {
		t.Fatalf("a book on a public shelf in a public hall is private: %v", p)
	}
	h.mustRun("hall/hall/update", token, map[string]any{"id": "east", "visibility": "private"})
	if p := page(); p["public"] == true {
		t.Fatalf("a book in a private hall is public: %v", p)
	}
	h.mustRun("hall/hall/update", token, map[string]any{"id": "east", "visibility": "public"})
	if p := page(); p["public"] != true {
		t.Fatalf("the hall made public again doesn't open its book: %v", p)
	}

	// Its shelf made private closes it, found through what it reads through.
	h.mustRun("hall/alcove/update", token, map[string]any{"id": open, "visibility": "private"})
	if p := page(); p["public"] == true {
		t.Fatalf("a book on a shelf made private is public: %v", p)
	}
	through, _ := page()["opened_by"].([]any)
	if len(through) != 2 {
		t.Fatalf("the page names the shelf and the hall it's read through: %v", through)
	}
}

func TestAPrivateVolumesFilesArentReadThroughItsPublicHall(t *testing.T) {
	h := start(t)
	_, token := h.signUp("ada@example.com")
	_, outsider := h.signUp("grace@example.com")
	h.mustRun("hall/hall/create", token, map[string]any{"slug": "west"})
	closed := h.mustRun("hall/alcove/create", token, map[string]any{"hall": "west", "name": "Ledgers", "visibility": "private"})
	cover := h.upload(token, "cover.png", "a private cover")
	volume := h.mustRun("hall/volume/create", token, map[string]any{"hall": "west", "alcove": closed, "title": "Accounts", "cover": cover})
	scan := h.upload(token, "scan.png", "a private scan")
	h.mustRun("hall/marginal/create", token, map[string]any{"hall": "west", "volume": volume, "scan": scan})

	if code, got := h.download(token, cover); code != 200 || got != "a private cover" {
		t.Fatalf("the hall's people read the cover: %d %q", code, got)
	}
	for _, path := range []string{cover, scan} {
		if code, _ := h.download(outsider, path); code != 404 {
			t.Errorf("someone outside reads %s through the public hall: %d", path, code)
		}
		if code, _ := h.download("", path); code != 404 {
			t.Errorf("someone signed out reads %s: %d", path, code)
		}
	}
}
