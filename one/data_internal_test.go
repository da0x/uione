// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one

import (
	"strings"
	"testing"
)

func TestDataMergesWhatsSentOverWhatsThereAndNullTakesANameOut(t *testing.T) {
	merged, err := mergeData("data", Data{"status": "working", "runner": "2"}, map[string]any{"status": "done", "since": "2026-10-13T18:00:00Z", "runner": nil})
	if err != nil {
		t.Fatal(err)
	}
	if merged["status"] != "done" || merged["since"] != "2026-10-13T18:00:00Z" || len(merged) != 2 {
		t.Fatalf("merged: %v", merged)
	}
	for _, sent := range []map[string]any{
		{"Status": "x"},
		{"status": []any{"a"}},
		{"status": strings.Repeat("x", maxListItemLength+1)},
	} {
		if _, err := mergeData("data", Data{}, sent); err == nil {
			t.Fatalf("refused: %v", sent)
		}
	}
	many := map[string]any{}
	for i := 0; i <= maxDataNames; i++ {
		many["n"+strings.Repeat("a", i%30)+string(rune('a'+i%26))+string(rune('a'+i/26))] = 1.0
	}
	if _, err := mergeData("data", Data{}, many); err == nil {
		t.Fatal("more names than data holds is refused")
	}
}
