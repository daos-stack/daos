//
// (C) Copyright 2022-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package daos

import (
	"strconv"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
)

var (
	_ SystemPropertyValue = NewBoolPropVal(false)
	_ SystemPropertyValue = NewStringPropVal("default")
	_ SystemPropertyValue = NewCompPropVal(func() string { return "computed" })
	_ SystemPropertyValue = NewIntPropVal(0)
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

func TestDaos_IntPropVal(t *testing.T) {
	choices := []int64{1, 2, -3}
	pv := NewIntPropVal(0, choices...)

	if pv == nil {
		t.Fatal("expected non-nil NumberPropVal")
	}

	intChoices := make([]string, len(choices))
	for i, c := range choices {
		intChoices[i] = strconv.FormatInt(c, 10)
	}
	if diff := cmp.Diff(intChoices, pv.Choices()); diff != "" {
		t.Fatalf("unexpected choices: (-want,+got)\n%s", diff)
	}

	if pv.String() != "0" {
		t.Fatalf("expected string %q, got %q", "0", pv.String())
	}
	if err := pv.Handler("1"); err != nil {
		t.Fatalf("expected no error, got %s", err)
	}
	if pv.String() != "1" {
		t.Fatalf("expected string %q, got %q", "1", pv.String())
	}
	if err := pv.Handler("-3"); err != nil {
		t.Fatalf("expected no error, got %s", err)
	}
	if pv.String() != "-3" {
		t.Fatalf("expected string %q, got %q", "-3", pv.String())
	}
	if err := pv.Handler("42"); err == nil {
		t.Fatalf("expected error, got nil")
	}
	if err := pv.Handler("invalid"); err == nil {
		t.Fatalf("expected error, got nil")
	}

	var nilPV *IntPropVal
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

func TestDaos_parseDuration(t *testing.T) {
	for name, tc := range map[string]struct {
		input  string
		expDur time.Duration
		expErr error
	}{
		"invalid unit": {
			input:  "40q",
			expErr: errors.New("unknown unit"),
		},
		"missing number": {
			input:  "m",
			expErr: errors.New("invalid"),
		},
		"invalid number": {
			input:  "onem",
			expErr: errors.New("invalid"),
		},
		"empty string": {
			input:  "",
			expDur: 0,
		},
		"no unit": {
			input:  "300",
			expDur: 5 * time.Minute,
		},
		"milliseconds": {
			input:  "3000ms",
			expDur: 3 * time.Second,
		},
		"seconds": {
			input:  "600s",
			expDur: 10 * time.Minute,
		},
		"custom: missing number": {
			input:  "w",
			expErr: errors.New("invalid"),
		},
		"custom: invalid number": {
			input:  "onew",
			expErr: errors.New("invalid"),
		},
		"custom: 1d": {
			input:  "1d",
			expDur: 24 * time.Hour,
		},
		"custom: 1w": {
			input:  "1w",
			expDur: 7 * 24 * time.Hour,
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotDur, gotErr := parseDuration(tc.input)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
			if diff := cmp.Diff(tc.expDur, gotDur); diff != "" {
				t.Fatalf("unexpected duration: (-want,+got)\n%s", diff)
			}
		})
	}
}
