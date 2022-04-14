//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package build_test

import (
	"testing"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
)

func TestBuild_NewVersion(t *testing.T) {
	for name, tc := range map[string]struct {
		input  string
		expVer *build.Version
		expErr error
	}{
		"valid": {
			input: "1.2.3",
			expVer: &build.Version{
				Major: 1,
				Minor: 2,
				Patch: 3,
			},
		},
		"valid (prefix)": {
			input: "v1.2.3",
			expVer: &build.Version{
				Major: 1,
				Minor: 2,
				Patch: 3,
			},
		},
		"all zeroes": {
			input:  "0.0.0",
			expVer: &build.Version{},
		},
		"too short": {
			input:  "1.2",
			expErr: errors.New("invalid version"),
		},
		"malformed major": {
			input:  ".2.3",
			expErr: errors.New("invalid major version"),
		},
		"malformed minor": {
			input:  "1..3",
			expErr: errors.New("invalid minor version"),
		},
		"malformed patch": {
			input:  "1.2.",
			expErr: errors.New("invalid patch version"),
		},
		"empty": {
			input:  "",
			expErr: errors.New("invalid version"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotVer, gotErr := build.NewVersion(tc.input)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if !tc.expVer.Equals(*gotVer) {
				t.Fatalf("expected version %v, got %v", tc.expVer, gotVer)
			}
		})
	}
}

func TestBuild_Version_GreaterThan(t *testing.T) {
	for name, tc := range map[string]struct {
		a           string
		b           string
		greaterThan bool
	}{
		"a > b": {
			a:           "1.2.3",
			b:           "1.2.0",
			greaterThan: true,
		},
		"a < b": {
			a:           "1.2.0",
			b:           "1.2.3",
			greaterThan: false,
		},
		"a == b": {
			a:           "1.2.3",
			b:           "1.2.3",
			greaterThan: false,
		},
	} {
		t.Run(name, func(t *testing.T) {
			a := build.MustNewVersion(tc.a)
			b := build.MustNewVersion(tc.b)

			if a.GreaterThan(b) != tc.greaterThan {
				t.Fatalf("expected %t, got %t", tc.greaterThan, a.GreaterThan(b))
			}
		})
	}
}

func TestBuild_Version_LessThan(t *testing.T) {
	for name, tc := range map[string]struct {
		a        string
		b        string
		lessThan bool
	}{
		"a < b": {
			a:        "1.2.0",
			b:        "1.2.3",
			lessThan: true,
		},
		"a > b": {
			a:        "1.2.3",
			b:        "1.2.0",
			lessThan: false,
		},
		"a == b": {
			a:        "1.2.3",
			b:        "1.2.3",
			lessThan: false,
		},
	} {
		t.Run(name, func(t *testing.T) {
			a := build.MustNewVersion(tc.a)
			b := build.MustNewVersion(tc.b)

			if a.LessThan(b) != tc.lessThan {
				t.Fatalf("expected %t, got %t", tc.lessThan, a.LessThan(b))
			}
		})
	}
}

func TestBuild_Version_Equals(t *testing.T) {
	for name, tc := range map[string]struct {
		a      string
		b      string
		equals bool
	}{
		"a == b": {
			a:      "1.2.3",
			b:      "1.2.3",
			equals: true,
		},
		"a != b": {
			a:      "1.2.3",
			b:      "1.2.0",
			equals: false,
		},
	} {
		t.Run(name, func(t *testing.T) {
			a := build.MustNewVersion(tc.a)
			b := build.MustNewVersion(tc.b)

			if a.Equals(b) != tc.equals {
				t.Fatalf("expected %t, got %t", tc.equals, a.Equals(b))
			}
		})
	}
}

func TestBuild_Version_Deltas(t *testing.T) {
	for name, tc := range map[string]struct {
		a     string
		b     string
		major int
		minor int
		patch int
	}{
		"equal": {
			a: "1.2.3",
			b: "1.2.3",
		},
		"major == 1": {
			a:     "1.2.3",
			b:     "2.2.3",
			major: 1,
		},
		"minor == 1": {
			a:     "1.1.3",
			b:     "1.2.3",
			minor: 1,
		},
		"patch == 1": {
			a:     "1.2.3",
			b:     "1.2.4",
			patch: 1,
		},
	} {
		t.Run(name, func(t *testing.T) {
			a := build.MustNewVersion(tc.a)
			b := build.MustNewVersion(tc.b)

			major := a.MajorDelta(b)
			if major != tc.major {
				t.Fatalf("major: expected %d, got %d", tc.major, major)
			}
			minor := a.MinorDelta(b)
			if minor != tc.minor {
				t.Fatalf("minor: expected %d, got %d", tc.minor, minor)
			}
			patch := a.PatchDelta(b)
			if patch != tc.patch {
				t.Fatalf("patch: expected %d, got %d", tc.patch, patch)
			}
		})
	}
}

func TestBuild_Version_PatchCompatible(t *testing.T) {
	a := build.MustNewVersion("1.2.0")
	b := build.MustNewVersion("1.2.3")

	if !a.PatchCompatible(b) {
		t.Fatalf("%s should be patch compatible with %s", a, b)
	}
}
