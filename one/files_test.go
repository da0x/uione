// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one_test

import (
	"bytes"
	"context"
	"encoding/json"
	"io"
	"net/http"
	"net/url"
	"os"
	"strings"
	"testing"
	"time"

	"cloud.google.com/go/firestore"
	"github.com/da0x/uione/one"
)

// A print shop's orders, each with a proof to approve and pictures of the job, and
// notes on an order with a scan of each. The shop's clerks read its orders; anyone
// does when the shop is public.

type Shop struct {
	one.Record
	Slug       string `firestore:"slug" one:"required,unique,key"`
	Visibility string `firestore:"visibility" one:"choices=public|private,default=private"`
}

type Clerk struct {
	one.Record
	Shop   string `firestore:"shop" one:"required,key,refers=press::shop"`
	Person string `firestore:"person" one:"required,key,refers=user"`
	Role   string `firestore:"role" one:"choices=clerk,default=clerk"`
}

type Order struct {
	one.Record
	Shop     string   `firestore:"shop" one:"required,refers=press::shop"`
	Title    string   `firestore:"title" one:"required"`
	Proof    string   `firestore:"proof" one:"file"`
	Pictures []string `firestore:"pictures" one:"file"`
}

type OrderNote struct {
	one.Record
	Order string `firestore:"order" one:"required,refers=press::order"`
	Scan  string `firestore:"scan" one:"file"`
}

var press = one.Module("press",
	one.Command[Shop]("shop::create").Allow(one.SignedIn).Do(func(c *one.Ctx, s *Shop) error {
		return one.Create(c, &Clerk{Shop: s.ID, Person: c.Me(), Role: "clerk"})
	}),
	one.Command[Shop]("shop::update"),
	one.Command[Order]("order::create"),
	one.Command[Order]("order::update"),
	one.Command[Order]("order::delete"),
	one.Command[OrderNote]("order_note::create"),
	one.Role("clerk", "shop:update", "order:create", "order:update", "order:delete", "order_note:create").Per(one.Entity[Shop](), one.Entity[Clerk]()),
	one.View("order_page").Per(one.Entity[Order]()).Readers(one.Entity[Clerk]()).PublicWhen("shop.visibility", "public").
		Copy("title", "title").Copy("proof", "proof"),
)

// ask gets somewhere to upload a file, or the failure.
func (h *harness) ask(token, name, kind string, size int) (int, map[string]any) {
	h.t.Helper()
	body, _ := json.Marshal(map[string]any{"name": name, "type": kind, "size": size})
	request, _ := http.NewRequest(http.MethodPost, h.server.URL+"/api/files", bytes.NewReader(body))
	if token != "" {
		request.Header.Set("Authorization", "Bearer "+token)
	}
	response, err := http.DefaultClient.Do(request)
	if err != nil {
		h.t.Fatal(err)
	}
	defer response.Body.Close()
	reply := map[string]any{}
	json.NewDecoder(response.Body).Decode(&reply)
	return response.StatusCode, reply
}

// upload uploads a file as someone, the way a browser does with what it's given,
// and returns its path.
func (h *harness) upload(token, name, content string) string {
	h.t.Helper()
	status, given := h.ask(token, name, "text/plain", len(content))
	if status != http.StatusOK {
		h.t.Fatalf("no upload for %s: %d %v", name, status, given)
	}
	request, _ := http.NewRequest(given["method"].(string), given["url"].(string), strings.NewReader(content))
	for k, v := range given["headers"].(map[string]any) {
		request.Header.Set(k, v.(string))
	}
	response, err := http.DefaultClient.Do(request)
	if err != nil || response.StatusCode >= 300 {
		h.t.Fatalf("uploading %s: %v %v", name, response, err)
	}
	response.Body.Close()
	return given["path"].(string)
}

// download asks for somewhere to download a file as someone, and reads it from
// there: its content, or the status it was refused with.
func (h *harness) download(token, path string) (int, string) {
	h.t.Helper()
	request, _ := http.NewRequest(http.MethodGet, h.server.URL+"/api/files?path="+url.QueryEscape(path), nil)
	if token != "" {
		request.Header.Set("Authorization", "Bearer "+token)
	}
	response, err := http.DefaultClient.Do(request)
	if err != nil {
		h.t.Fatal(err)
	}
	defer response.Body.Close()
	reply := map[string]any{}
	json.NewDecoder(response.Body).Decode(&reply)
	if response.StatusCode != http.StatusOK {
		return response.StatusCode, ""
	}
	got, err := http.Get(reply["url"].(string))
	if err != nil {
		h.t.Fatal(err)
	}
	defer got.Body.Close()
	content, _ := io.ReadAll(got.Body)
	if got.StatusCode != http.StatusOK {
		return got.StatusCode, ""
	}
	return http.StatusOK, string(content)
}

// stored says whether a file is still in the bucket.
func stored(t *testing.T, path string) bool {
	t.Helper()
	response, err := http.Get("http://" + os.Getenv("STORAGE_EMULATOR_HOST") + "/storage/v1/b/" + project + "-files/o/" + url.PathEscape(path))
	if err != nil {
		t.Fatal(err)
	}
	response.Body.Close()
	return response.StatusCode == http.StatusOK
}

func TestAFileIsAttachedByTheCommandThatSetsIt(t *testing.T) {
	h := start(t)
	ada, clerk := h.signUp("ada@example.com")
	_, outsider := h.signUp("grace@example.com")
	h.mustRun("press/shop/create", clerk, map[string]any{"slug": "inky"})

	status, _ := h.ask("", "proof.pdf", "application/pdf", 10)
	if status != http.StatusUnauthorized {
		t.Fatalf("someone signed out was given somewhere to upload: %d", status)
	}
	proof := h.upload(clerk, "../../etc/proof v1.pdf", "first proof")
	if !strings.HasPrefix(proof, "files/"+ada+"/") || !strings.HasSuffix(proof, "/proof v1.pdf") {
		t.Fatalf("an upload is kept as its uploader's, under its own name only: %s", proof)
	}
	if code, _ := h.download(clerk, proof); code != http.StatusNotFound {
		t.Fatalf("an upload nothing holds yet can't be downloaded: %d", code)
	}

	order := h.mustRun("press/order/create", clerk, map[string]any{"shop": "inky", "title": "Posters", "proof": proof})
	if code, got := h.download(clerk, proof); code != http.StatusOK || got != "first proof" {
		t.Fatalf("a clerk downloads the order's proof: %d %q", code, got)
	}
	if code, _ := h.download(outsider, proof); code != http.StatusNotFound {
		t.Fatalf("someone outside a private shop can't: %d", code)
	}
	if code, _ := h.download("", proof); code != http.StatusNotFound {
		t.Fatalf("nor can someone signed out: %d", code)
	}
	if page := h.view("press::order_page:" + order); page["proof"] != proof {
		t.Fatalf("a view holds the file's path like any value: %v", page["proof"])
	}

	// Once the shop is public, anyone may.
	h.mustRun("press/shop/update", clerk, map[string]any{"id": "inky", "visibility": "public"})
	if code, got := h.download("", proof); code != http.StatusOK || got != "first proof" {
		t.Fatalf("anyone downloads a public shop's proof: %d %q", code, got)
	}

	// A new proof takes the old one's place, and the old one is deleted.
	second := h.upload(clerk, "proof.pdf", "second proof")
	h.mustRun("press/order/update", clerk, map[string]any{"id": order, "proof": second})
	if stored(t, proof) {
		t.Fatal("the proof taken out of the order is still in the bucket")
	}
	if code, _ := h.download(clerk, proof); code != http.StatusNotFound {
		t.Fatalf("and isn't offered: %d", code)
	}
	if code, got := h.download(clerk, second); code != http.StatusOK || got != "second proof" {
		t.Fatalf("the new one is: %d %q", code, got)
	}
}

func TestOnlyYourOwnFinishedUploadIsAttached(t *testing.T) {
	h := start(t)
	_, clerk := h.signUp("ada@example.com")
	_, other := h.signUp("grace@example.com")
	h.mustRun("press/shop/create", clerk, map[string]any{"slug": "inky"})
	h.mustRun("press/shop/create", other, map[string]any{"slug": "dotty"})

	theirs := h.upload(other, "theirs.pdf", "not yours")
	h.expect("press/order/create", clerk, map[string]any{"shop": "inky", "title": "Posters", "proof": theirs}, 400,
		"Proof is a file someone else uploaded")
	h.expect("press/order/create", clerk, map[string]any{"shop": "inky", "title": "Posters", "proof": "files/x/y/made-up.pdf"}, 400,
		"Proof isn't a file that was uploaded")
	_, given := h.ask(clerk, "late.pdf", "application/pdf", 4)
	h.expect("press/order/create", clerk, map[string]any{"shop": "inky", "title": "Posters", "proof": given["path"]}, 400,
		"Proof hasn't finished uploading")

	mine := h.upload(clerk, "proof.pdf", "mine")
	h.mustRun("press/order/create", clerk, map[string]any{"shop": "inky", "title": "Posters", "proof": mine})
	h.expect("press/order/create", clerk, map[string]any{"shop": "inky", "title": "Flyers", "proof": mine}, 400,
		"Proof is a file that belongs to something else already")

	status, reply := h.ask(clerk, "huge.mov", "video/quicktime", int(one.MaxFileSize)+1)
	if status != http.StatusBadRequest || reply["error"] != "a file can hold at most 5 GB" {
		t.Fatalf("a file bigger than the most there may be is refused: %d %v", status, reply)
	}
}

func TestAListOfFilesAndWhatGoesWithARecord(t *testing.T) {
	h := start(t)
	_, clerk := h.signUp("ada@example.com")
	_, outsider := h.signUp("grace@example.com")
	h.mustRun("press/shop/create", clerk, map[string]any{"slug": "inky"})
	front, back := h.upload(clerk, "front.jpg", "front"), h.upload(clerk, "back.jpg", "back")
	order := h.mustRun("press/order/create", clerk, map[string]any{"shop": "inky", "title": "Cards", "pictures": []string{front, back}})
	if code, got := h.download(clerk, back); code != http.StatusOK || got != "back" {
		t.Fatalf("each of a list's files is downloaded: %d %q", code, got)
	}
	h.mustRun("press/order/update", clerk, map[string]any{"id": order, "pictures": []string{back}})
	if stored(t, front) || !stored(t, back) {
		t.Fatal("only the picture taken out of the list is deleted")
	}

	// A note has no page of its own, so its scan is read by whoever reads its order's.
	scan := h.upload(clerk, "scan.png", "scan")
	h.mustRun("press/order_note/create", clerk, map[string]any{"order": order, "scan": scan})
	if code, got := h.download(clerk, scan); code != http.StatusOK || got != "scan" {
		t.Fatalf("a clerk reads the scan on the order's note: %d %q", code, got)
	}
	if code, _ := h.download(outsider, scan); code != http.StatusNotFound {
		t.Fatalf("someone outside the shop doesn't: %d", code)
	}

	h.mustRun("press/order/delete", clerk, map[string]any{"id": order})
	if stored(t, back) {
		t.Fatal("deleting an order deletes its files")
	}
	if _, err := h.store.Collection("files").Where("path", "==", back).Documents(context.Background()).Next(); err == nil {
		t.Fatal("and forgets them")
	}
}

func TestAnUploadNeverAttachedGoesADayOn(t *testing.T) {
	h := start(t)
	_, clerk := h.signUp("ada@example.com")
	forgotten := h.upload(clerk, "draft.pdf", "draft")
	// A day on: when it waits until is put in the past.
	docs, err := h.store.Collection("files").Where("path", "==", forgotten).Documents(context.Background()).GetAll()
	if err != nil || len(docs) != 1 {
		t.Fatalf("an upload is kept: %v %v", docs, err)
	}
	if _, err := docs[0].Ref.Update(context.Background(), []firestore.Update{{Path: "waits_until", Value: time.Now().Add(-time.Hour)}}); err != nil {
		t.Fatal(err)
	}
	h.upload(clerk, "next.pdf", "next")
	for range 50 {
		if !stored(t, forgotten) {
			break
		}
		time.Sleep(100 * time.Millisecond)
	}
	if stored(t, forgotten) {
		t.Fatal("an upload that waited a day without being attached is still there")
	}
}
