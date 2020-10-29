//
// (C) Copyright 2020 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package system

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
)

func TestSystem_NewFaultDomain(t *testing.T) {
	for name, tc := range map[string]struct {
		input     []string
		expErr    error
		expResult *FaultDomain
	}{
		"no input": {
			expResult: &FaultDomain{},
		},
		"empty strings": {
			input:  []string{"ok", ""},
			expErr: errors.New("invalid fault domain"),
		},
		"whitespace-only strings": {
			input:  []string{"ok", "\t    "},
			expErr: errors.New("invalid fault domain"),
		},
		"single-level": {
			input: []string{"ok"},
			expResult: &FaultDomain{
				Domains: []string{"ok"},
			},
		},
		"multi-level": {
			input: []string{"ok", "go"},
			expResult: &FaultDomain{
				Domains: []string{"ok", "go"},
			},
		},
		"trim whitespace": {
			input: []string{" ok  ", "\tgo\n"},
			expResult: &FaultDomain{
				Domains: []string{"ok", "go"},
			},
		},
		"fix case": {
			input: []string{"OK", "Go"},
			expResult: &FaultDomain{
				Domains: []string{"ok", "go"},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			result, err := NewFaultDomain(tc.input...)

			common.CmpErr(t, tc.expErr, err)

			if diff := cmp.Diff(result, tc.expResult); diff != "" {
				t.Fatalf("(-want, +got): %s", diff)
			}
		})
	}
}

func TestSystem_NewFaultDomainFromString(t *testing.T) {
	for name, tc := range map[string]struct {
		input     string
		expErr    error
		expResult *FaultDomain
	}{
		"nil": {
			input: FaultDomainNilStr,
		},
		"empty": {
			expResult: &FaultDomain{},
		},
		"only whitespace": {
			input:     "  \n  \t",
			expResult: &FaultDomain{},
		},
		"single-level fault domain": {
			input: "/host",
			expResult: &FaultDomain{
				Domains: []string{"host"},
			},
		},
		"multi-level fault domain": {
			input: "/dc0/rack1/pdu2/host",
			expResult: &FaultDomain{
				Domains: []string{"dc0", "rack1", "pdu2", "host"},
			},
		},
		"fix whitespace errors": {
			input: " / rack 1/pdu 0    /host\n",
			expResult: &FaultDomain{
				Domains: []string{"rack 1", "pdu 0", "host"},
			},
		},
		"symbols ok": {
			input: "/$$$/#/!?/--++/}{",
			expResult: &FaultDomain{
				Domains: []string{"$$$", "#", "!?", "--++", "}{"},
			},
		},
		"case insensitive": {
			input: "/DC0/Rack1/PDU2/Host",
			expResult: &FaultDomain{
				Domains: []string{"dc0", "rack1", "pdu2", "host"},
			},
		},
		"fault domain doesn't start with separator": {
			input:  "junk",
			expErr: errors.New("invalid fault domain"),
		},
		"fault domain ends with separator": {
			input:  "/junk/",
			expErr: errors.New("invalid fault domain"),
		},
		"only root separator": {
			input:  "/",
			expErr: errors.New("invalid fault domain"),
		},
		"fault domain with empty levels": {
			input:  "/dc0//pdu2/host",
			expErr: errors.New("invalid fault domain"),
		},
		"fault domain with only whitespace between levels": {
			input:  "/dc0/    /pdu2/host",
			expErr: errors.New("invalid fault domain"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			result, err := NewFaultDomainFromString(tc.input)

			common.CmpErr(t, tc.expErr, err)

			if diff := cmp.Diff(result, tc.expResult); diff != "" {
				t.Fatalf("(-want, +got): %s", diff)
			}
		})
	}
}

func TestSystem_FaultDomain_String(t *testing.T) {
	for name, tc := range map[string]struct {
		domain *FaultDomain
		expStr string
	}{
		"nil": {
			expStr: "(nil)",
		},
		"empty": {
			domain: &FaultDomain{},
			expStr: "",
		},
		"single level": {
			domain: &FaultDomain{
				Domains: []string{"host"},
			},
			expStr: "/host",
		},
		"multi level": {
			domain: &FaultDomain{
				Domains: []string{"rack0", "pdu1", "host"},
			},
			expStr: "/rack0/pdu1/host",
		},
	} {
		t.Run(name, func(t *testing.T) {
			common.AssertEqual(t, tc.expStr, tc.domain.String(), "unexpected result")
		})
	}
}

func TestSystem_FaultDomain_Equals(t *testing.T) {
	for name, tc := range map[string]struct {
		domain1   *FaultDomain
		domain2   *FaultDomain
		expResult bool
	}{
		"both nil": {
			expResult: true,
		},
		"nil vs. empty": {
			domain2:   &FaultDomain{},
			expResult: false,
		},
		"nil vs. populated": {
			domain2: &FaultDomain{
				Domains: []string{"one", "two"},
			},
			expResult: false,
		},
		"both empty": {
			domain1:   &FaultDomain{},
			domain2:   &FaultDomain{},
			expResult: true,
		},
		"empty vs. populated": {
			domain1: &FaultDomain{},
			domain2: &FaultDomain{
				Domains: []string{"one", "two"},
			},
			expResult: false,
		},
		"populated matching": {
			domain1: &FaultDomain{
				Domains: []string{"one", "two"},
			},
			domain2: &FaultDomain{
				Domains: []string{"one", "two"},
			},
			expResult: true,
		},
		"subset": {
			domain1: &FaultDomain{
				Domains: []string{"one"},
			},
			domain2: &FaultDomain{
				Domains: []string{"one", "two"},
			},
			expResult: false,
		},
		"totally different": {
			domain1: &FaultDomain{
				Domains: []string{"three", "four"},
			},
			domain2: &FaultDomain{
				Domains: []string{"one", "two"},
			},
			expResult: false,
		},
	} {
		t.Run(name, func(t *testing.T) {
			common.AssertEqual(t, tc.domain1.Equals(tc.domain2), tc.expResult, "domain1.Equals failed")
			common.AssertEqual(t, tc.domain2.Equals(tc.domain1), tc.expResult, "domain2.Equals failed")
		})
	}
}

func TestSystem_FaultDomain_Empty(t *testing.T) {
	for name, tc := range map[string]struct {
		domain    *FaultDomain
		expResult bool
	}{
		"nil": {
			expResult: true,
		},
		"empty": {
			domain:    &FaultDomain{},
			expResult: true,
		},
		"single level": {
			domain: &FaultDomain{
				Domains: []string{"host"},
			},
			expResult: false,
		},
		"multi level": {
			domain: &FaultDomain{
				Domains: []string{"rack0", "pdu1", "host"},
			},
			expResult: false,
		},
	} {
		t.Run(name, func(t *testing.T) {
			common.AssertEqual(t, tc.expResult, tc.domain.Empty(), "unexpected result")
		})
	}
}

func TestSystem_FaultDomain_BottomLevel(t *testing.T) {
	for name, tc := range map[string]struct {
		domain    *FaultDomain
		expResult string
	}{
		"nil": {
			expResult: "",
		},
		"empty": {
			domain:    &FaultDomain{},
			expResult: "",
		},
		"single level": {
			domain: &FaultDomain{
				Domains: []string{"host"},
			},
			expResult: "host",
		},
		"multi level": {
			domain: &FaultDomain{
				Domains: []string{"rack0", "pdu1", "host"},
			},
			expResult: "host",
		},
	} {
		t.Run(name, func(t *testing.T) {
			common.AssertEqual(t, tc.expResult, tc.domain.BottomLevel(), "unexpected result")
		})
	}
}

func TestSystem_FaultDomain_TopLevel(t *testing.T) {
	for name, tc := range map[string]struct {
		domain    *FaultDomain
		expResult string
	}{
		"nil": {
			expResult: "",
		},
		"empty": {
			domain:    &FaultDomain{},
			expResult: "",
		},
		"single level": {
			domain: &FaultDomain{
				Domains: []string{"host"},
			},
			expResult: "host",
		},
		"multi level": {
			domain: &FaultDomain{
				Domains: []string{"rack0", "pdu1", "host"},
			},
			expResult: "rack0",
		},
	} {
		t.Run(name, func(t *testing.T) {
			common.AssertEqual(t, tc.expResult, tc.domain.TopLevel(), "unexpected result")
		})
	}
}

func TestSystem_FaultDomain_NumLevels(t *testing.T) {
	for name, tc := range map[string]struct {
		domain    *FaultDomain
		expResult int
	}{
		"nil": {
			expResult: 0,
		},
		"empty": {
			domain:    &FaultDomain{},
			expResult: 0,
		},
		"single level": {
			domain: &FaultDomain{
				Domains: []string{"host"},
			},
			expResult: 1,
		},
		"multi level": {
			domain: &FaultDomain{
				Domains: []string{"dc2", "rack0", "pdu1", "host"},
			},
			expResult: 4,
		},
	} {
		t.Run(name, func(t *testing.T) {
			common.AssertEqual(t, tc.expResult, tc.domain.NumLevels(), "unexpected result")
		})
	}
}

func TestSystem_FaultDomain_Level(t *testing.T) {
	for name, tc := range map[string]struct {
		domain    *FaultDomain
		level     int
		expErr    error
		expResult string
	}{
		"nil": {
			level:  0,
			expErr: errors.New("out of range"),
		},
		"empty": {
			domain: &FaultDomain{},
			level:  0,
			expErr: errors.New("out of range"),
		},
		"single level": {
			domain: &FaultDomain{
				Domains: []string{"host"},
			},
			level:     0,
			expResult: "host",
		},
		"multi level - bottom": {
			domain: &FaultDomain{
				Domains: []string{"rack0", "pdu1", "host"},
			},
			level:     0,
			expResult: "host",
		},
		"multi level - middle": {
			domain: &FaultDomain{
				Domains: []string{"rack0", "pdu1", "host"},
			},
			level:     1,
			expResult: "pdu1",
		},
		"multi level - top": {
			domain: &FaultDomain{
				Domains: []string{"rack0", "pdu1", "host"},
			},
			level:     2,
			expResult: "rack0",
		},
		"out of range": {
			domain: &FaultDomain{
				Domains: []string{"rack0", "pdu1", "host"},
			},
			level:  3,
			expErr: errors.New("out of range"),
		},
		"negative": {
			domain: &FaultDomain{
				Domains: []string{"rack0", "pdu1", "host"},
			},
			level:  -1,
			expErr: errors.New("out of range"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			lev, err := tc.domain.Level(tc.level)

			common.CmpErr(t, tc.expErr, err)
			common.AssertEqual(t, tc.expResult, lev, "unexpected result")
		})
	}
}

func TestSystem_FaultDomain_Overlaps(t *testing.T) {
	for name, tc := range map[string]struct {
		fd1       string
		fd2       string
		expResult bool
	}{
		"both empty": {
			expResult: false,
		},
		"single-level": {
			fd1:       "/host",
			fd2:       "/host",
			expResult: true,
		},
		"multi-level identical": {
			fd1:       "/top/middle/bottom",
			fd2:       "/top/middle/bottom",
			expResult: true,
		},
		"one empty": {
			fd2:       "/top/middle/bottom",
			expResult: false,
		},
		"no match": {
			fd1:       "/top/middle/bottom",
			fd2:       "/highest/intermediate/lowest",
			expResult: false,
		},
		"top-level match": {
			fd1:       "/top/middle/bottom",
			fd2:       "/top/intermediate/lowest",
			expResult: true,
		},
		"match below top level": {
			fd1:       "/top/middle/bottom",
			fd2:       "/top/middle/lowest",
			expResult: true,
		},
		"different root with same names underneath": {
			fd1:       "/top1/middle/bottom",
			fd2:       "/top2/middle/bottom",
			expResult: false,
		},
		"different numbers of levels": {
			fd1:       "/top/middle/another/bottom",
			fd2:       "/top/middle",
			expResult: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			fd1 := MustCreateFaultDomainFromString(tc.fd1)
			fd2 := MustCreateFaultDomainFromString(tc.fd2)

			// Should be identical in both directions
			common.AssertEqual(t, tc.expResult, fd1.Overlaps(fd2), "unexpected result for fd1.Overlaps")
			common.AssertEqual(t, tc.expResult, fd2.Overlaps(fd1), "unexpected result for fd2.Overlaps")
		})
	}
}

func TestSystem_FaultDomain_NewChild(t *testing.T) {
	for name, tc := range map[string]struct {
		orig       *FaultDomain
		childLevel string
		expResult  *FaultDomain
		expErr     error
	}{
		"nil parent": {
			childLevel: "child",
			expResult: &FaultDomain{
				Domains: []string{"child"},
			},
		},
		"empty parent": {
			orig:       &FaultDomain{},
			childLevel: "child",
			expResult: &FaultDomain{
				Domains: []string{"child"},
			},
		},
		"valid parent": {
			orig: &FaultDomain{
				Domains: []string{"parent"},
			},
			childLevel: "child",
			expResult: &FaultDomain{
				Domains: []string{"parent", "child"},
			},
		},
		"empty child level": {
			orig: &FaultDomain{
				Domains: []string{"parent"},
			},
			expErr: errors.New("invalid fault domain"),
		},
		"whitespace-only child level": {
			orig: &FaultDomain{
				Domains: []string{"parent"},
			},
			childLevel: "   ",
			expErr:     errors.New("invalid fault domain"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			result, err := tc.orig.NewChild(tc.childLevel)

			common.CmpErr(t, tc.expErr, err)

			if diff := cmp.Diff(result, tc.expResult); diff != "" {
				t.Fatalf("(-want, +got): %s", diff)
			}
		})
	}
}

func TestSystem_FaultDomain_MustCreateChild(t *testing.T) {
	for name, tc := range map[string]struct {
		orig       *FaultDomain
		childLevel string
		expResult  *FaultDomain
		expPanic   bool
	}{
		"success": {
			orig: &FaultDomain{
				Domains: []string{"parent"},
			},
			childLevel: "child",
			expResult: &FaultDomain{
				Domains: []string{"parent", "child"},
			},
		},
		"panic": {
			orig: &FaultDomain{
				Domains: []string{"parent"},
			},
			childLevel: "   ",
			expPanic:   true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			if tc.expPanic {
				defer func(t *testing.T) {
					if r := recover(); r == nil {
						t.Fatal("didn't panic")
					}
				}(t)
			}
			result := tc.orig.MustCreateChild(tc.childLevel)

			if diff := cmp.Diff(result, tc.expResult); diff != "" {
				t.Fatalf("(-want, +got): %s", diff)
			}
		})
	}
}

func TestSystem_FaultDomain_MustCreateFaultDomain(t *testing.T) {
	for name, tc := range map[string]struct {
		input     []string
		expResult *FaultDomain
		expPanic  bool
	}{
		"success": {
			input: []string{"one", "two", "three"},
			expResult: &FaultDomain{
				Domains: []string{"one", "two", "three"},
			},
		},
		"panic": {
			input:    []string{"//", ""}, // bad fault domain
			expPanic: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			if tc.expPanic {
				defer func(t *testing.T) {
					if r := recover(); r == nil {
						t.Fatal("didn't panic")
					}
				}(t)
			}
			result := MustCreateFaultDomain(tc.input...)

			if diff := cmp.Diff(result, tc.expResult); diff != "" {
				t.Fatalf("(-want, +got): %s", diff)
			}
		})
	}
}

func TestSystem_FaultDomain_MustCreateFaultDomainFromString(t *testing.T) {
	for name, tc := range map[string]struct {
		input     string
		expResult *FaultDomain
		expPanic  bool
	}{
		"success": {
			input: "/one/two/three",
			expResult: &FaultDomain{
				Domains: []string{"one", "two", "three"},
			},
		},
		"panic": {
			input:    "/not/////good", // bad fault domain
			expPanic: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			if tc.expPanic {
				defer func(t *testing.T) {
					if r := recover(); r == nil {
						t.Fatal("didn't panic")
					}
				}(t)
			}
			result := MustCreateFaultDomainFromString(tc.input)

			if diff := cmp.Diff(result, tc.expResult); diff != "" {
				t.Fatalf("(-want, +got): %s", diff)
			}
		})
	}
}
