// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one

import (
	_ "embed"
	"encoding/json"
	"net"
	"net/http"
	"net/netip"
	"strings"
	"sync"
)

// Where a request came from, for an audit and for limiting those who aren't signed
// in. A caller can write any X-Forwarded-For it likes, so only what Google wrote is
// believed:
//
//   - Cloud Run adds the address that connected to it as the last entry. Nobody can
//     fake that one.
//   - Through the site, what connected is Firebase Hosting, which drops whatever the
//     caller sent and writes the caller's own address before it.
//
// So the last entry is where the request came from, unless it's Hosting's, when it's
// the one before. Hosting's addresses are Google's own, which no Google Cloud customer
// can send from; Google publishes both lists, and tools/google-addresses refreshes
// them.

//go:embed google-addresses.json
var googleAddressesJSON []byte

//go:embed google-cloud-addresses.json
var googleCloudAddressesJSON []byte

var googlesOwnRanges = sync.OnceValues(func() ([]netip.Prefix, []netip.Prefix) {
	return prefixes(googleAddressesJSON), prefixes(googleCloudAddressesJSON)
})

func prefixes(list []byte) []netip.Prefix {
	var published struct {
		Prefixes []struct {
			IPv4 string `json:"ipv4Prefix"`
			IPv6 string `json:"ipv6Prefix"`
		} `json:"prefixes"`
	}
	if err := json.Unmarshal(list, &published); err != nil {
		panic("one: Google's list of its addresses doesn't read: " + err.Error())
	}
	var all []netip.Prefix
	for _, p := range published.Prefixes {
		all = append(all, netip.MustParsePrefix(p.IPv4+p.IPv6))
	}
	return all
}

// googlesOwn says whether an address is Google's own, like Hosting's, rather than
// one that anybody on Google Cloud could send from.
func googlesOwn(address string) bool {
	ip, err := netip.ParseAddr(address)
	if err != nil {
		return false
	}
	ip = ip.Unmap()
	google, cloud := googlesOwnRanges()
	within := func(ranges []netip.Prefix) bool {
		for _, p := range ranges {
			if p.Contains(ip) {
				return true
			}
		}
		return false
	}
	return within(google) && !within(cloud)
}

// addressOf is where a request came from: the last address it was forwarded for, or
// the one before when the last is Hosting's, or the connection's own.
func addressOf(r *http.Request) string {
	var forwarded []string
	for _, header := range r.Header.Values("X-Forwarded-For") {
		for entry := range strings.SplitSeq(header, ",") {
			if entry = strings.TrimSpace(entry); entry != "" {
				forwarded = append(forwarded, entry)
			}
		}
	}
	if n := len(forwarded); n > 1 && googlesOwn(forwarded[n-1]) {
		return forwarded[n-2]
	} else if n > 0 {
		return forwarded[n-1]
	}
	host, _, err := net.SplitHostPort(r.RemoteAddr)
	if err != nil {
		return r.RemoteAddr
	}
	return host
}

// asker is who a limit counts against: the person or service signed in, or, for
// those who aren't, where they asked from. An IPv6 address counts with all of its
// /64, since one connection may use any address in it.
func asker(who caller, r *http.Request) string {
	if who.id != "" {
		return who.id
	}
	address := addressOf(r)
	if ip, err := netip.ParseAddr(address); err == nil && ip.Unmap().Is6() {
		address = netip.PrefixFrom(ip, 64).Masked().String()
	}
	return "at " + address
}
