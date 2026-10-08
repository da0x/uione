// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one

import (
	"fmt"
	"net/url"
	"reflect"
	"regexp"
	"strconv"
	"strings"
	"sync"
	"time"
	"unicode"
)

// Record holds what every entity has. A generated entity embeds it, so ids,
// timestamps and who made each change are never written by hand.
type Record struct {
	ID        string    `firestore:"id"`
	CreatedAt time.Time `firestore:"created_at"`
	CreatedBy string    `firestore:"created_by"`
	UpdatedAt time.Time `firestore:"updated_at"`
	UpdatedBy string    `firestore:"updated_by"`
}

func (r *Record) record() *Record { return r }

// History marks an entity whose every change is kept. A generated entity that
// keeps its history embeds it beside Record:
//
//	type Issue struct {
//		one.Record
//		one.History
//		...
//	}
//
// Each change of a field is kept as a ChangeOf[Issue], in the same transaction
// as the change itself.
type History struct{}

// ChangeOf is one change to an entity that keeps its history: which field, what it
// was before and after, and the command that changed it, with who ran it and when
// in its Record. A change of an issue also holds the issue's id under issue, and
// what the issue points at, like its project, so a view can list the changes of
// one issue, or of every issue in a project. Making something is one change with
// no field.
type ChangeOf[E any] struct {
	Record
	Field  string `firestore:"field"`
	Action string `firestore:"action"`
	Before any    `firestore:"before"`
	After  any    `firestore:"after"`
}

func (ChangeOf[E]) changes() reflect.Type { return reflect.TypeFor[E]() }

// changer is a ChangeOf of some entity.
type changer interface{ changes() reflect.Type }

// entityPointer is what a generated entity is: a pointer to a struct that embeds
// Record.
type entityPointer[E any] interface {
	*E
	record() *Record
}

// A field of an entity, read once from its struct tags:
//
//	Email string `firestore:"email" one:"required,key,email"`
//
// firestore is the stored name, which is the .one name. one holds the field's rules.
type field struct {
	index    []int
	name     string
	typ      reflect.Type
	required bool
	key      bool
	unique   bool
	serial   bool   // counted up as each entity is made
	lower    bool   // stored lowercase, like a GitHub repository's name
	per      string // for a serial counted within another entity: the field pointing at it
	email    bool
	slug     bool // a name like my-app: lowercase letters and digits, joined by single dashes
	after    string
	choices  []string
	initial  string // the value a new entity starts with; "me" and "now" are special
	from     string // the field a new entity's value is made from when it's given none, like a phase's name from its title
	refers   string // the entity this field points at, like library::book
	mentions string // for a list of people: the field whose text names them, like a comment's body, with @username
	history  bool   // what it points at keeps this entity's changes in its own history, as an issue does its comments'
}

type schema struct {
	entity     string // the full name, like waitlist::signup
	name       string // the entity's own name, like signup
	collection string // where it's stored, like waitlist_signup
	typ        reflect.Type
	fields     []field
	keys       []*field // the fields an entity is named by, in order
	history    *schema  // for an entity that keeps its history: what its changes are
}

var schemas sync.Map // reflect.Type to *schema

func schemaOf(t reflect.Type, entity string) *schema {
	if s, ok := schemas.Load(t); ok {
		return s.(*schema)
	}
	s := &schema{
		entity:     entity,
		name:       entity[strings.LastIndex(entity, ":")+1:],
		collection: strings.ReplaceAll(entity, "::", "_"),
		typ:        t,
	}
	for i := 0; i < t.NumField(); i++ {
		f := t.Field(i)
		if f.Anonymous && f.Type == reflect.TypeOf(Record{}) {
			continue
		}
		if f.Anonymous && f.Type == reflect.TypeOf(History{}) {
			s.history = &schema{
				entity:     entity + "_change",
				name:       s.name + "_change",
				collection: s.collection + "_history",
			}
			continue
		}
		name, _, _ := strings.Cut(f.Tag.Get("firestore"), ",")
		if name == "" || name == "-" {
			continue
		}
		fd := field{index: f.Index, name: name, typ: f.Type}
		for _, rule := range strings.Split(f.Tag.Get("one"), ",") {
			rule, value, _ := strings.Cut(strings.TrimSpace(rule), "=")
			switch rule {
			case "required":
				fd.required = true
			case "key":
				fd.key = true
			case "unique":
				fd.unique = true
			case "serial":
				fd.serial, fd.per = true, value
			case "email":
				fd.email = true
			case "slug":
				fd.slug, fd.lower = true, true
			case "after":
				fd.after = value
			case "choices":
				fd.choices = strings.Split(value, "|")
			case "default":
				fd.initial = value
			case "from":
				fd.from = value
			case "mentions":
				fd.mentions = value
			case "history":
				fd.history = true
			case "refers":
				fd.refers = value
			}
		}
		s.fields = append(s.fields, fd)
	}
	for i := range s.fields {
		if s.fields[i].key {
			s.keys = append(s.keys, &s.fields[i])
		}
	}
	if s.history != nil {
		// A change names what it's of, under that entity's own name, and holds what
		// it points at, so a view can ask for the changes of one issue or of every
		// issue in a project.
		s.history.fields = append(s.history.fields, field{name: s.name, refers: s.entity})
		for _, f := range s.fields {
			if f.refers != "" {
				s.history.fields = append(s.history.fields, field{name: f.name, refers: f.refers})
			}
		}
		for _, name := range []string{"field", "action", "before", "after"} {
			s.history.fields = append(s.history.fields, field{name: name})
		}
	}
	actual, _ := schemas.LoadOrStore(t, s)
	return actual.(*schema)
}

// startsWithUsername says whether a field starts as the person's username, which
// has to be read from their profile first.
func (s *schema) startsWithUsername() bool {
	for _, f := range s.fields {
		if f.initial == "me.username" {
			return true
		}
	}
	return false
}

func (s *schema) field(name string) *field {
	for i := range s.fields {
		if s.fields[i].name == name {
			return &s.fields[i]
		}
	}
	return nil
}

// Who made and last changed something, which every entity holds.
var stamps = []field{{name: "created_by", refers: "user"}, {name: "updated_by", refers: "user"}}

// through is the field a view reads through, like the author in author.name: one of
// the entity's own, or who made or last changed it, as in created_by.name.
func (s *schema) through(name string) *field {
	if f := s.field(name); f != nil {
		return f
	}
	for i := range stamps {
		if stamps[i].name == name {
			return &stamps[i]
		}
	}
	return nil
}

// label makes a readable name from a snake_case one: created_at becomes "Created at".
// It's the same rule @uione/react uses, so messages match the form's labels.
func label(name string) string {
	words := strings.ReplaceAll(name, "_", " ")
	if words == "" {
		return words
	}
	return strings.ToUpper(words[:1]) + words[1:]
}

// Failure is a mistake the person can fix, like a missing field or a permission
// they don't have. Its message is shown to them as it is.
type Failure struct {
	Status  int
	Message string
}

func (f *Failure) Error() string { return f.Message }

func invalid(format string, args ...any) *Failure {
	return &Failure{Status: 400, Message: fmt.Sprintf(format, args...)}
}

// A list a command is sent is bounded, so one request can't hold the server up.
const (
	maxListItems      = 1000
	maxListItemLength = 1000
)

// decode copies what a command was sent onto an entity. Only the fields sent are
// changed. A field that starts as "me" can't be sent at all, since it's always the
// person running the command, and neither can a serial, which is counted.
func (s *schema) decode(input map[string]any, into reflect.Value) error {
	for _, f := range s.fields {
		raw, ok := input[f.name]
		if !ok || f.initial == "me" || f.serial {
			continue
		}
		target := into.FieldByIndex(f.index)
		switch f.typ {
		case reflect.TypeOf(time.Time{}):
			text, ok := raw.(string)
			if !ok {
				return invalid("%s should be a date", label(f.name))
			}
			if text == "" {
				target.Set(reflect.ValueOf(time.Time{}))
				continue
			}
			parsed, err := time.Parse(time.RFC3339, text)
			if err != nil {
				parsed, err = time.Parse("2006-01-02", text)
			}
			if err != nil {
				return invalid("%s should be a date, like 2026-09-30", label(f.name))
			}
			target.Set(reflect.ValueOf(parsed))
		default:
			switch f.typ.Kind() {
			case reflect.String:
				text, ok := raw.(string)
				if !ok {
					return invalid("%s should be text", label(f.name))
				}
				target.SetString(text)
			case reflect.Float64:
				number, ok := raw.(float64)
				if !ok {
					return invalid("%s should be a number", label(f.name))
				}
				target.SetFloat(number)
			case reflect.Bool:
				flag, ok := raw.(bool)
				if !ok {
					return invalid("%s should be yes or no", label(f.name))
				}
				target.SetBool(flag)
			case reflect.Slice:
				// A list, like labels or assignees: each one's id, or a piece of text.
				items, ok := raw.([]any)
				if !ok {
					return invalid("%s should be a list", label(f.name))
				}
				if len(items) > maxListItems {
					return invalid("%s can hold at most %d", label(f.name), maxListItems)
				}
				list := make([]string, 0, len(items))
				seen := make(map[string]bool, len(items))
				for _, item := range items {
					text, ok := item.(string)
					if !ok {
						return invalid("%s should be a list of text", label(f.name))
					}
					if len(text) > maxListItemLength {
						return invalid("each of %s can be at most %d characters", strings.ToLower(label(f.name)), maxListItemLength)
					}
					if text = strings.TrimSpace(text); text != "" && !seen[text] {
						seen[text] = true
						list = append(list, text)
					}
				}
				if target.Type() != reflect.TypeOf(list) {
					return invalid("%s can't be set this way", label(f.name))
				}
				target.Set(reflect.ValueOf(list))
			default:
				return invalid("%s can't be set this way", label(f.name))
			}
		}
	}
	return nil
}

// start fills in what a new entity starts with: "me" is the person creating it,
// "now" is the time, and anything else is a literal value, like a first choice.
//
// It returns the field a key was made from, like a phase's title, when one was: an
// entity whose key was made that way, and is taken, isn't the same one made again.
func (s *schema) start(v reflect.Value, me, username string, now time.Time) (madeFrom string) {
	for _, f := range s.fields {
		if f.from != "" {
			target := v.FieldByIndex(f.index)
			source := s.field(f.from)
			if target.Kind() == reflect.String && target.String() == "" && source != nil && v.FieldByIndex(source.index).Kind() == reflect.String {
				target.SetString(slugOf(v.FieldByIndex(source.index).String()))
				if f.key {
					madeFrom = f.from
				}
			}
			continue
		}
		if f.initial == "" {
			continue
		}
		target := v.FieldByIndex(f.index)
		if !target.IsZero() && f.initial != "me" && f.initial != "me.username" {
			continue
		}
		switch {
		case f.initial == "me" && target.Kind() == reflect.String:
			target.SetString(me)
		case f.initial == "me.username" && target.Kind() == reflect.String:
			target.SetString(username)
		case f.initial == "now" && f.typ == reflect.TypeOf(time.Time{}):
			target.Set(reflect.ValueOf(now))
		case f.initial == "false" && target.Kind() == reflect.Bool:
			target.SetBool(false)
		case f.initial == "true" && target.Kind() == reflect.Bool:
			target.SetBool(true)
		case target.Kind() == reflect.String:
			target.SetString(f.initial)
		}
	}
	return madeFrom
}

// slugOf is a name made from words, as a key: In review is in_review. Letters and
// digits are kept, lowercase, and everything between them becomes one underscore.
func slugOf(words string) string {
	var b strings.Builder
	gap := false
	for _, r := range strings.ToLower(words) {
		if unicode.IsLetter(r) || unicode.IsDigit(r) {
			if gap && b.Len() > 0 {
				b.WriteByte('_')
			}
			b.WriteRune(r)
			gap = false
		} else {
			gap = true
		}
	}
	return b.String()
}

// validate checks an entity against its rules, and says what's wrong the way a
// form would.
func (s *schema) validate(v reflect.Value) error {
	for _, f := range s.fields {
		value := v.FieldByIndex(f.index)
		if f.required && (value.IsZero() || (value.Kind() == reflect.Slice && value.Len() == 0)) {
			return invalid("%s is required", label(f.name))
		}
		if f.email && value.Kind() == reflect.String && value.String() != "" && !looksLikeEmail(value.String()) {
			return invalid("%s isn't an email address", label(f.name))
		}
		if f.slug && value.Kind() == reflect.String && value.String() != "" && !slugPattern.MatchString(value.String()) {
			return invalid("%s is lowercase letters, digits and dashes, like my-app", label(f.name))
		}
		if len(f.choices) > 0 && value.Kind() == reflect.String && value.String() != "" && !contains(f.choices, value.String()) {
			return invalid("%s has to be one of %s", label(f.name), strings.Join(f.choices, ", "))
		}
		if f.after != "" {
			other := s.field(f.after)
			if other != nil && f.typ == reflect.TypeOf(time.Time{}) && other.typ == f.typ {
				this, before := value.Interface().(time.Time), v.FieldByIndex(other.index).Interface().(time.Time)
				if !this.IsZero() && !before.IsZero() && !this.After(before) {
					return invalid("%s has to be after %s", label(f.name), strings.ToLower(label(f.after)))
				}
			}
		}
	}
	return nil
}

func looksLikeEmail(text string) bool {
	at := strings.LastIndex(text, "@")
	return at > 0 && strings.Contains(text[at+1:], ".") && !strings.ContainsAny(text, " \t\n")
}

func contains(list []string, value string) bool {
	for _, item := range list {
		if item == value {
			return true
		}
	}
	return false
}

// id is the document id an entity is stored under. An entity with key fields is
// stored under their normalized values, joined by dashes, so the same values are
// the same entity: Ada@Example.com and ada@example.com are one signup, and the
// third loan of book HIS-0142 is HIS-0142-3.
func (s *schema) id(v reflect.Value) (string, bool) {
	if len(s.keys) == 0 {
		return "", false
	}
	parts := make([]string, len(s.keys))
	for i, key := range s.keys {
		value := strings.TrimSpace(fmt.Sprint(v.FieldByIndex(key.index).Interface()))
		if number, ok := v.FieldByIndex(key.index).Interface().(float64); ok {
			value = strconv.FormatFloat(number, 'f', -1, 64) // 1000000, not 1e+06
		}
		if key.email {
			value = strings.ToLower(value)
		}
		if value == "" {
			return "", false
		}
		parts[i] = value
	}
	return Key(parts...), true
}

// Key is the id of an entity named by its keys, in order: Key("engine",
// "maintainer") is the engine project's maintainer role. Parts are joined by
// dashes, so a dash in any part but the first is escaped: project engine with
// person x-1 is engine-x%2D1, never the same id as project engine-x with person 1.
func Key(parts ...string) string {
	escaped := make([]string, len(parts))
	for i, part := range parts {
		escaped[i] = url.PathEscape(part)
		if i > 0 {
			escaped[i] = strings.ReplaceAll(escaped[i], "-", "%2D")
		}
	}
	return strings.Join(escaped, "-")
}

// normalize stores key fields in the same form the id is made from, and lowercases
// the fields that are compared without case.
func (s *schema) normalize(v reflect.Value) {
	for i := range s.fields {
		f := &s.fields[i]
		if f.typ.Kind() != reflect.String || !(f.key || f.lower) {
			continue
		}
		target := v.FieldByIndex(f.index)
		value := strings.TrimSpace(target.String())
		if f.email || f.lower {
			value = strings.ToLower(value)
		}
		target.SetString(value)
	}
}

// data is an entity as stored: every field under its .one name, Record's included.
// A date with no value is stored as none, rather than as the year 1, so a view can
// ask for the loans whose returned_at is none.
func (s *schema) data(v reflect.Value) map[string]any {
	out := map[string]any{}
	record := v.Addr().Interface().(interface{ record() *Record }).record()
	out["id"], out["created_at"], out["created_by"] = record.ID, record.CreatedAt, record.CreatedBy
	out["updated_at"], out["updated_by"] = record.UpdatedAt, record.UpdatedBy
	for _, f := range s.fields {
		value := v.FieldByIndex(f.index).Interface()
		if t, ok := value.(time.Time); ok && t.IsZero() {
			value = nil
		}
		if list, ok := value.([]string); ok && list == nil {
			value = []string{} // an empty list is stored as one, so has can ask it
		}
		out[f.name] = value
	}
	return out
}

// slugPattern is a name like my-app or neotrac: lowercase letters and digits,
// joined by single dashes, at most 100 characters.
var slugPattern = regexp.MustCompile(`^[a-z0-9](?:[a-z0-9]|-[a-z0-9]){0,99}$`)
