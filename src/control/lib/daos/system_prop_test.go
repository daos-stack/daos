//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
package daos

import (
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestDaos_BoolPropVal(t *testing.T) {
	pv := NewBoolPropVal(false)

	if pv == nil {
		t.Fatal("expected non-nil BoolPropVal")
	}

	expChoices := []string{"true", "false"}
	if diff := cmp.Diff(expChoices, pv.Choices()); diff != "" {
		t.Fatalf("unexpected choices: (-want,+got)\n%s", diff)
	}

	for _, trueStr := range []string{"true", "1", "yes", "on"} {
		if err := pv.Handler(trueStr); err != nil {
			t.Fatalf("expected no error, got %s", err)
		}
		if pv.String() != "true" {
			t.Fatalf("expected string %q, got %q", "true", pv.String())
		}
	}

	for _, falseStr := range []string{"false", "0", "no", "off"} {
		if err := pv.Handler(falseStr); err != nil {
			t.Fatalf("expected no error, got %s", err)
		}
		if pv.String() != "false" {
			t.Fatalf("expected string %q, got %q", "false", pv.String())
		}
	}

	if err := pv.Handler("invalid"); err == nil {
		t.Fatalf("expected error, got nil")
	}

	var nilPV *BoolPropVal
	if nilPV.String() != "(nil)" {
		t.Fatalf("%T stringer should handle nil", nilPV)
	}
}

func TestDaos_StringPropVal(t *testing.T) {
	choices := []string{"choice1", "choice2"}
	pv := NewStringPropVal("default", choices...)

	if pv == nil {
		t.Fatal("expected non-nil StringPropVal")
	}

	if diff := cmp.Diff(choices, pv.Choices()); diff != "" {
		t.Fatalf("unexpected choices: (-want,+got)\n%s", diff)
	}

	if pv.String() != "default" {
		t.Fatalf("expected string %q, got %q", "default", pv.String())
	}
	if err := pv.Handler("choice1"); err != nil {
		t.Fatalf("expected no error, got %s", err)
	}
	if pv.String() != "choice1" {
		t.Fatalf("expected string %q, got %q", "choice1", pv.String())
	}
	if err := pv.Handler("choice3"); err == nil {
		t.Fatalf("expected error, got nil")
	}

	var nilPV *StringPropVal
	if nilPV.String() != "(nil)" {
		t.Fatalf("%T stringer should handle nil", nilPV)
	}
}

func TestDaos_CompPropVal(t *testing.T) {
	compVal := "computed"
	pv := NewCompPropVal(func() string { return compVal })

	if pv == nil {
		t.Fatal("expected non-nil CompPropVal")
	}

	if err := pv.Handler("user"); err == nil {
		t.Fatalf("expected error, got nil")
	}
	if pv.String() != compVal {
		t.Fatalf("expected string %q, got %q", compVal, pv.String())
	}

	pv.(*CompPropVal).ValueSource = nil
	if pv.String() != "(nil)" {
		t.Fatalf("%t stringer should handle nil ValueSource", pv)
	}
	var nilPV *CompPropVal
	if nilPV.String() != "(nil)" {
		t.Fatalf("%T stringer should handle nil", nilPV)
	}
}

func TestDaos_SystemPropertyKeys(t *testing.T) {
	for i := systemPropertyUnknown + 1; i < systemPropertyMax; i++ {
		key := SystemPropertyKey(i)

		if !key.IsValid() {
			t.Fatalf("expected prop idx %d to be valid", i)
		}
		if key.String() == "unknown" {
			t.Fatalf("prop idx %d does not have a valid string value", i)
		}

		var fromString SystemPropertyKey
		if err := fromString.FromString(key.String()); err != nil {
			t.Fatalf("prop idx %d string (%q) failed to resolve: %s", i, key, err)
		}
	}
}
