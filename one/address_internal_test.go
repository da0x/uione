// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one

import (
	"net/http"
	"testing"
)

// The headers below are as example.org's backend received them, sent once through the
// site and once straight to Cloud Run, with and without a made-up address.
func TestWhereARequestCameFromIsWhatGoogleSaid(t *testing.T) {
	for _, c := range []struct {
		said, forwarded, want string
	}{
		{"straight to Cloud Run", "67.180.127.129", "67.180.127.129"},
		{"straight to Cloud Run, with a made-up address", "203.0.113.8,67.180.127.129", "67.180.127.129"},
		{"straight to Cloud Run over IPv6, with a made-up address", "203.0.113.8,2601:647:6900:2a18::b106", "2601:647:6900:2a18::b106"},
		{"through the site", "67.180.127.129,66.249.84.225", "67.180.127.129"},
		{"through the site, from another of Hosting's", "67.180.127.129,74.125.209.130", "67.180.127.129"},
		{"from Google Cloud, with a made-up address", "203.0.113.8,34.120.1.1", "34.120.1.1"},
		{"from Google Cloud, in two headers", "203.0.113.8", "34.120.1.1"},
		{"with nothing forwarded", "", "192.0.2.1"},
	} {
		r, _ := http.NewRequest(http.MethodPost, "/api/x", nil)
		r.RemoteAddr = "192.0.2.1:4000"
		if c.forwarded != "" {
			r.Header.Add("X-Forwarded-For", c.forwarded)
		}
		if c.said == "from Google Cloud, in two headers" {
			r.Header.Add("X-Forwarded-For", "34.120.1.1")
		}
		if got := addressOf(r); got != c.want {
			t.Errorf("%s: the address is %q, want %q", c.said, got, c.want)
		}
	}
}

func TestThoseNotSignedInAreLimitedByWhereTheyAsk(t *testing.T) {
	ask := func(forwarded string) string {
		r, _ := http.NewRequest(http.MethodPost, "/api/x", nil)
		r.Header.Set("X-Forwarded-For", forwarded)
		return asker(caller{}, r)
	}
	if got := ask("67.180.127.129"); got != "at 67.180.127.129" {
		t.Errorf("an IPv4 address counts alone; got %q", got)
	}
	if a, b := ask("2601:647:6900:2a18::b106"), ask("2601:647:6900:2a18:1:2:3:4"); a != b || a != "at 2601:647:6900:2a18::/64" {
		t.Errorf("an IPv6 address counts with its /64; got %q and %q", a, b)
	}
	r, _ := http.NewRequest(http.MethodPost, "/api/x", nil)
	r.Header.Set("X-Forwarded-For", "67.180.127.129")
	if got := asker(caller{id: "ann"}, r); got != "ann" {
		t.Errorf("a person signed in counts as themselves; got %q", got)
	}
}
