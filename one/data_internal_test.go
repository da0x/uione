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
	// A run of numbers, like a chart's, is kept as sent, up to its bound.
	cpu := []any{12.5, 40.0, 7.0}
	merged, err = mergeData("data", Data{}, map[string]any{"cpu": cpu})
	if err != nil || len(merged["cpu"].([]any)) != 3 {
		t.Fatalf("a run of numbers: %v %v", merged, err)
	}
	if _, err := mergeData("data", Data{}, map[string]any{"cpu": make([]any, maxDataNumbers+1)}); err == nil {
		t.Fatal("more numbers than a run holds is refused")
	}
	if _, err := mergeData("data", Data{}, map[string]any{"cpu": []any{1.0, "two"}}); err == nil {
		t.Fatal("a run of numbers with text in it is refused")
	}
	many := map[string]any{}
	for i := 0; i <= maxDataNames; i++ {
		many["n"+strings.Repeat("a", i%30)+string(rune('a'+i%26))+string(rune('a'+i/26))] = 1.0
	}
	if _, err := mergeData("data", Data{}, many); err == nil {
		t.Fatal("more names than data holds is refused")
	}
}
