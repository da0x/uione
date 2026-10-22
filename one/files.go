// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one

import (
	"context"
	"crypto/rand"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"net/http"
	"net/url"
	"os"
	"path"
	"strings"
	"time"
	"unicode"

	"cloud.google.com/go/firestore"
	"cloud.google.com/go/storage"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

// Files. A field of type file holds a path in the app's bucket, like
// files/<who>/<random>/proof.pdf, whose last part is the file's name; a list of
// file holds several. Bytes never pass through the backend:
//
//   - POST /api/files {name, type, size} gives whoever is signed in somewhere to
//     upload one file, for ten minutes, and the path it will have.
//   - A command that sets a field to that path attaches it: in the command's
//     transaction, the upload has to be the person's own and not attached already,
//     and from then on it belongs to that record's field.
//   - GET /api/files?path=… gives somewhere to download it, for ten minutes, to
//     whoever may read the record: anyone when a page of it is public, else its
//     readers, a project's members, say, people and services alike.
//   - A file taken out of its field, or whose record is deleted, is deleted too,
//     once the command's change is saved. An upload never attached is deleted a day
//     on.
//
// What's uploaded is kept in files/<sha256 of its path>: who uploaded it, and once
// it's attached, which record and field it's in. The bucket is ONE_FILES_BUCKET,
// or <project>-files; on the emulators, the Storage emulator's, at
// STORAGE_EMULATOR_HOST or localhost:9199.

// MaxFileSize is the most one file may hold, in bytes.
var MaxFileSize int64 = 5 << 30

// How long an address to upload or download a file lasts, and how long an upload
// waits to be attached.
const (
	fileAddressLasts = 10 * time.Minute
	uploadWaits      = 24 * time.Hour
)

type files struct {
	bucket   string
	client   *storage.Client
	emulator string // the Storage emulator's host, like localhost:9199
}

// filesFor connects to the bucket, for an app with a file field anywhere.
func filesFor(ctx context.Context, project string, r *registry) (*files, error) {
	needed := false
	for _, s := range r.schemas {
		for _, f := range s.fields {
			needed = needed || f.file
		}
	}
	if !needed {
		return nil, nil
	}
	bucket := os.Getenv("ONE_FILES_BUCKET")
	if bucket == "" {
		bucket = project + "-files"
	}
	// On the emulators, files are kept by the Storage emulator too, on its usual
	// port unless it's said, never in a real bucket.
	if os.Getenv("FIRESTORE_EMULATOR_HOST") != "" && os.Getenv("STORAGE_EMULATOR_HOST") == "" {
		os.Setenv("STORAGE_EMULATOR_HOST", "localhost:9199")
	}
	client, err := storage.NewClient(ctx)
	if err != nil {
		return nil, err
	}
	return &files{bucket: bucket, client: client, emulator: os.Getenv("STORAGE_EMULATOR_HOST")}, nil
}

// fileRecord is where what's known of an uploaded file is kept.
func (a *App) fileRecord(p string) *firestore.DocumentRef {
	sum := sha256.Sum256([]byte(p))
	return a.store.Collection("files").Doc(hex.EncodeToString(sum[:]))
}

// fileName is a name a file may be kept under: its last part only, without what a
// path or a header would read differently, and never empty.
func fileName(name string) string {
	name = path.Base(strings.ReplaceAll(name, "\\", "/"))
	name = strings.Map(func(r rune) rune {
		if unicode.IsControl(r) || r == '"' || r == '/' {
			return -1
		}
		return r
	}, name)
	name = strings.TrimSpace(name)
	if len([]rune(name)) > 200 {
		name = string([]rune(name)[:200])
	}
	if name == "" || name == "." || name == ".." {
		return "file"
	}
	return name
}

// paths is what a stored value holds: one path, or a list of them.
func paths(value any) []string {
	switch v := value.(type) {
	case string:
		if v != "" {
			return []string{v}
		}
	case []string:
		return v
	case []any:
		var out []string
		for _, item := range v {
			if s, ok := item.(string); ok && s != "" {
				out = append(out, s)
			}
		}
		return out
	}
	return nil
}

// added is what after holds that before doesn't.
func added(before, after []string) []string {
	var out []string
	for _, p := range after {
		if !contains(before, p) && !contains(out, p) {
			out = append(out, p)
		}
	}
	return out
}

// serveUpload gives somewhere to upload one file: the path it will have, and the
// address, method and headers to send it with.
func (a *App) serveUpload(w http.ResponseWriter, r *http.Request) {
	if a.files == nil {
		reply(w, http.StatusNotFound, map[string]any{"error": "this app keeps no files"})
		return
	}
	who, err := a.caller(r)
	if err != nil {
		replyFailure(w, a, "signing in", err)
		return
	}
	if who.id == "" {
		reply(w, http.StatusUnauthorized, map[string]any{"error": "sign in to upload a file"})
		return
	}
	if err := a.allow("asking", who.id, askingRate, "uploading"); err != nil {
		replyFailure(w, a, "uploading", err)
		return
	}
	var asked struct {
		Name string  `json:"name"`
		Type string  `json:"type"`
		Size float64 `json:"size"`
	}
	if err := json.NewDecoder(http.MaxBytesReader(w, r.Body, 1<<16)).Decode(&asked); err != nil {
		reply(w, http.StatusBadRequest, map[string]any{"error": "say the file's name, type and size"})
		return
	}
	size := int64(asked.Size)
	if size < 0 || size > MaxFileSize {
		reply(w, http.StatusBadRequest, map[string]any{"error": "a file can hold at most " + bytesSaid(MaxFileSize)})
		return
	}
	kind := asked.Type
	if kind == "" || strings.ContainsAny(kind, "\r\n") {
		kind = "application/octet-stream"
	}
	random := make([]byte, 12)
	if _, err := rand.Read(random); err != nil {
		replyFailure(w, a, "naming an upload", err)
		return
	}
	name := fileName(asked.Name)
	p := "files/" + who.id + "/" + hex.EncodeToString(random) + "/" + name
	now := time.Now().UTC()
	if _, err := a.fileRecord(p).Set(r.Context(), map[string]any{
		"path": p, "name": name, "type": kind, "size": size,
		"uploaded_by": who.id, "uploaded_at": now, "waits_until": now.Add(uploadWaits),
	}); err != nil {
		replyFailure(w, a, "keeping an upload", err)
		return
	}
	a.usage.filed(who.id, size)
	go a.sweepUploads(context.WithoutCancel(r.Context()))

	headers := map[string]string{"Content-Type": kind}
	var address, method string
	if a.files.emulator != "" {
		method = http.MethodPost
		address = "http://" + a.files.emulator + "/upload/storage/v1/b/" + a.files.bucket + "/o?uploadType=media&name=" + url.QueryEscape(p)
	} else {
		method = http.MethodPut
		limit := "0," + jsonNumber(size)
		headers["x-goog-content-length-range"] = limit
		address, err = a.files.client.Bucket(a.files.bucket).SignedURL(p, &storage.SignedURLOptions{
			Scheme:      storage.SigningSchemeV4,
			Method:      method,
			Expires:     now.Add(fileAddressLasts),
			ContentType: kind,
			Headers:     []string{"x-goog-content-length-range:" + limit},
		})
		if err != nil {
			replyFailure(w, a, "signing an upload", err)
			return
		}
	}
	reply(w, http.StatusOK, map[string]any{"path": p, "url": address, "method": method, "headers": headers})
}

// serveDownload gives somewhere to download a file, to whoever may read the record
// it's attached to. With ?download=1 the browser saves it rather than showing it.
func (a *App) serveDownload(w http.ResponseWriter, r *http.Request) {
	if a.files == nil {
		reply(w, http.StatusNotFound, map[string]any{"error": "this app keeps no files"})
		return
	}
	who, err := a.caller(r)
	if err != nil {
		replyFailure(w, a, "signing in", err)
		return
	}
	if err := a.allow("asking", asker(who, r), askingRate, "downloading"); err != nil {
		replyFailure(w, a, "downloading", err)
		return
	}
	p := r.URL.Query().Get("path")
	missing := map[string]any{"error": "that file isn't there"}
	snap, err := a.fileRecord(p).Get(r.Context())
	if status.Code(err) == codes.NotFound || p == "" {
		reply(w, http.StatusNotFound, missing)
		return
	}
	if err != nil {
		replyFailure(w, a, "finding a file", err)
		return
	}
	kept := snap.Data()
	entity, _ := kept["entity"].(string)
	id, _ := kept["record"].(string)
	field, _ := kept["field"].(string)
	s := a.reg.entity(entity)
	if s == nil || id == "" {
		reply(w, http.StatusNotFound, missing)
		return
	}
	stored, err := a.stored(r.Context(), s, id)
	if err != nil {
		replyFailure(w, a, "finding a file", err)
		return
	}
	if !contains(paths(stored[field]), p) {
		reply(w, http.StatusNotFound, missing)
		return
	}
	may, err := a.mayRead(r.Context(), who.id, s, id, stored)
	if err != nil {
		replyFailure(w, a, "finding a file", err)
		return
	}
	if !may {
		// The same answer as for a file that isn't there, so asking says nothing.
		reply(w, http.StatusNotFound, missing)
		return
	}
	name, _ := kept["name"].(string)
	var address string
	if a.files.emulator != "" {
		address = "http://" + a.files.emulator + "/storage/v1/b/" + a.files.bucket + "/o/" + url.PathEscape(p) + "?alt=media"
	} else {
		disposition := "inline"
		if r.URL.Query().Get("download") != "" {
			disposition = "attachment"
		}
		address, err = a.files.client.Bucket(a.files.bucket).SignedURL(p, &storage.SignedURLOptions{
			Scheme:          storage.SigningSchemeV4,
			Method:          http.MethodGet,
			Expires:         time.Now().Add(fileAddressLasts),
			QueryParameters: url.Values{"response-content-disposition": {disposition + `; filename="` + name + `"`}},
		})
		if err != nil {
			replyFailure(w, a, "signing a download", err)
			return
		}
	}
	reply(w, http.StatusOK, map[string]any{"url": address, "name": name, "type": kept["type"], "size": kept["size"]})
}

// mayRead says whether someone may read a record, and so its files. A record with a
// page of its own, a view per its kind, is read as that page says: public, or naming
// them among its readers. One with none, like a comment, is read only when every
// page of what it points at says so, so a comment on a private issue isn't read
// through the issue's public project.
func (a *App) mayRead(ctx context.Context, me string, s *schema, id string, stored map[string]any) (bool, error) {
	paged := func(per *schema) bool {
		for _, v := range a.reg.views {
			if v.per != nil && a.reg.schemas[v.per] == per {
				return true
			}
		}
		return false
	}
	check := func(per *schema, id string, fields map[string]any) (bool, error) {
		for _, v := range a.reg.views {
			if v.per == nil || a.reg.schemas[v.per] != per {
				continue
			}
			if v.public {
				return true, nil
			}
			data := map[string]any{}
			if err := a.access(ctx, v, id, fields, data); err != nil {
				return false, err
			}
			readers, _ := data["readers"].([]string)
			if data["public"] == true || (me != "" && contains(readers, me)) {
				return true, nil
			}
		}
		return false, nil
	}
	if paged(s) {
		return check(s, id, stored)
	}
	checked := false
	for _, f := range s.fields {
		through := a.reg.entity(f.refers)
		pointed, _ := stored[f.name].(string)
		if through == nil || pointed == "" || !paged(through) {
			continue
		}
		fields, err := a.stored(ctx, through, pointed)
		if err != nil {
			return false, err
		}
		if len(fields) == 0 {
			continue
		}
		may, err := check(through, pointed, fields)
		if !may || err != nil {
			return false, err
		}
		checked = true
	}
	return checked, nil
}

// claim attaches the files a command puts in a record's fields, as part of its
// transaction: each has to be an upload of the person running it that isn't
// attached already, and has to have arrived. It reads, so it runs before the
// command writes anything; the writes it returns are made with the command's.
func (c *Ctx) claim(s *schema, id string, before, after map[string]any) ([]write, error) {
	var out []write
	for _, f := range s.fields {
		if !f.file {
			continue
		}
		for _, p := range added(paths(before[f.name]), paths(after[f.name])) {
			if c.app.files == nil {
				return nil, invalid("this app keeps no files")
			}
			ref := c.app.fileRecord(p)
			snap, err := c.tx.Get(ref)
			if status.Code(err) == codes.NotFound {
				return nil, invalid("%s isn't a file that was uploaded", label(f.name))
			}
			if err != nil {
				return nil, err
			}
			kept := snap.Data()
			if by, _ := kept["uploaded_by"].(string); c.me != "" && by != c.me {
				return nil, invalid("%s is a file someone else uploaded", label(f.name))
			}
			if entity, _ := kept["entity"].(string); entity != "" {
				return nil, invalid("%s is a file that belongs to something else already", label(f.name))
			}
			if _, err := c.app.files.client.Bucket(c.app.files.bucket).Object(p).Attrs(c); errors.Is(err, storage.ErrObjectNotExist) {
				return nil, invalid("%s hasn't finished uploading", label(f.name))
			} else if err != nil {
				return nil, err
			}
			out = append(out, write{ref: ref, merged: true, data: map[string]any{
				"entity": s.entity, "record": id, "field": f.name, "waits_until": firestore.Delete,
			}})
		}
	}
	return out, nil
}

// forgetFiles deletes the files a change took out of a record, or that went with
// a record deleted, once the change is saved. A failure is logged, and leaves a
// file nothing points at.
func (a *App) forgetFiles(ctx context.Context, events []event) {
	if a.files == nil {
		return
	}
	for _, ev := range events {
		s := a.reg.entity(ev.Entity)
		if s == nil || ev.Before == nil {
			continue
		}
		for _, f := range s.fields {
			if !f.file {
				continue
			}
			for _, p := range added(paths(ev.After[f.name]), paths(ev.Before[f.name])) {
				a.deleteFile(ctx, p)
			}
		}
	}
}

func (a *App) deleteFile(ctx context.Context, p string) {
	err := a.files.client.Bucket(a.files.bucket).Object(p).Delete(ctx)
	if err != nil && !errors.Is(err, storage.ErrObjectNotExist) {
		a.log.Printf("one: file %s wasn't deleted: %v", p, err)
		return
	}
	record := a.fileRecord(p)
	if snap, err := record.Get(ctx); err == nil {
		by, _ := snap.Data()["uploaded_by"].(string)
		a.usage.filed(by, -asInt(snap.Data()["size"]))
	}
	if _, err := record.Delete(ctx); err != nil {
		a.log.Printf("one: file %s wasn't forgotten: %v", p, err)
	}
}

// sweepUploads deletes uploads that waited a day and were never attached.
func (a *App) sweepUploads(ctx context.Context) {
	docs, err := a.store.Collection("files").Where("waits_until", "<", time.Now().UTC()).Limit(20).Documents(ctx).GetAll()
	if err != nil {
		a.log.Printf("one: uploads weren't swept: %v", err)
		return
	}
	for _, d := range docs {
		if p, _ := d.Data()["path"].(string); p != "" {
			a.deleteFile(ctx, p)
		}
	}
}

func jsonNumber(n int64) string {
	b, _ := json.Marshal(n)
	return string(b)
}

// bytesSaid is a size as people say it, like 5 GB.
func bytesSaid(n int64) string {
	for _, unit := range []struct {
		size int64
		name string
	}{{1 << 30, "GB"}, {1 << 20, "MB"}, {1 << 10, "KB"}} {
		if n >= unit.size && n%unit.size == 0 {
			return jsonNumber(n/unit.size) + " " + unit.name
		}
	}
	return jsonNumber(n) + " bytes"
}
