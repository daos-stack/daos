//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system

import (
	"fmt"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
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
		"explicit root": {
			input:  []string{"/"},
			expErr: errors.New("invalid fault domain"),
		},
		"whitespace-only strings": {
			input:  []string{"ok", "\t    "},
			expErr: errors.New("invalid fault domain"),
		},
		"name contains separator": {
			input:  []string{"ok", "alpha/beta"},
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

			test.CmpErr(t, tc.expErr, err)

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
		"root specified": {
			input:     FaultDomainSeparator,
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

			test.CmpErr(t, tc.expErr, err)

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
			expStr: FaultDomainSeparator,
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
			test.AssertEqual(t, tc.expStr, tc.domain.String(), "unexpected result")
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
			test.AssertEqual(t, tc.domain1.Equals(tc.domain2), tc.expResult, "domain1.Equals failed")
			test.AssertEqual(t, tc.domain2.Equals(tc.domain1), tc.expResult, "domain2.Equals failed")
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
			test.AssertEqual(t, tc.expResult, tc.domain.Empty(), "unexpected result")
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
			test.AssertEqual(t, tc.expResult, tc.domain.BottomLevel(), "unexpected result")
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
			test.AssertEqual(t, tc.expResult, tc.domain.TopLevel(), "unexpected result")
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
			test.AssertEqual(t, tc.expResult, tc.domain.NumLevels(), "unexpected result")
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

			test.CmpErr(t, tc.expErr, err)
			test.AssertEqual(t, tc.expResult, lev, "unexpected result")
		})
	}
}

func TestSystem_FaultDomain_IsAncestorOf(t *testing.T) {
	for name, tc := range map[string]struct {
		fd1       string
		fd2       string
		expResult bool
	}{
		"both empty": {
			expResult: true,
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
		"first empty": {
			fd2:       "/top/middle/bottom",
			expResult: true,
		},
		"second empty": {
			fd1:       "/top/middle/bottom",
			expResult: false,
		},
		"no match": {
			fd1:       "/top/middle/bottom",
			fd2:       "/highest/intermediate/lowest",
			expResult: false,
		},
		"match at top level": {
			fd1:       "/top/middle/bottom",
			fd2:       "/top/intermediate/lowest",
			expResult: false,
		},
		"match below top level": {
			fd1:       "/top/middle/bottom",
			fd2:       "/top/middle/lowest",
			expResult: false,
		},
		"different root with same names underneath": {
			fd1:       "/top1/middle/bottom",
			fd2:       "/top2/middle/bottom",
			expResult: false,
		},
		"first is a parent domain of second": {
			fd1:       "/top/middle",
			fd2:       "/top/middle/another/bottom",
			expResult: true,
		},
		"second is a parent domain of first": {
			fd1:       "/top/middle/another/bottom",
			fd2:       "/top/middle",
			expResult: false,
		},
	} {
		t.Run(name, func(t *testing.T) {
			fd1 := MustCreateFaultDomainFromString(tc.fd1)
			fd2 := MustCreateFaultDomainFromString(tc.fd2)

			test.AssertEqual(t, tc.expResult, fd1.IsAncestorOf(fd2), "")
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
		"child level is root": {
			orig: &FaultDomain{
				Domains: []string{"parent"},
			},
			childLevel: "/",
			expErr:     errors.New("invalid fault domain"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			result, err := tc.orig.NewChild(tc.childLevel)

			test.CmpErr(t, tc.expErr, err)

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

func testFaultDomains(num int) []*FaultDomain {
	emptyFD := MustCreateFaultDomain()
	fdList := []*FaultDomain{emptyFD}
	for i := 0; i < num/2; i++ {
		rack := emptyFD.MustCreateChild(fmt.Sprintf("rack%d", i))
		fdList = append(fdList, rack)
	}

	for j := 0; j < num/2; j++ {
		fdList = append(fdList, emptyFD.MustCreateChild(fmt.Sprintf("pdu%d", j)))
	}
	return fdList
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

func expFaultDomainID(offset uint32) uint32 {
	return FaultDomainRootID + offset
}

func TestSystem_NewFaultDomainTree(t *testing.T) {
	fd1 := MustCreateFaultDomain("one")
	fd2 := fd1.MustCreateChild("two")
	fd3 := fd2.MustCreateChild("three")

	fd4 := MustCreateFaultDomain("four")
	fd5 := fd4.MustCreateChild("five")
	fd6 := fd4.MustCreateChild("six")

	for name, tc := range map[string]struct {
		domains   []*FaultDomain
		expResult *FaultDomainTree
	}{
		"no domains": {
			expResult: &FaultDomainTree{
				Domain:   MustCreateFaultDomain(),
				ID:       expFaultDomainID(0),
				Children: []*FaultDomainTree{},
			},
		},
		"nil domain": {
			domains: []*FaultDomain{nil},
			expResult: &FaultDomainTree{
				Domain:   MustCreateFaultDomain(),
				ID:       expFaultDomainID(0),
				Children: []*FaultDomainTree{},
			},
		},
		"empty domain": {
			domains: []*FaultDomain{MustCreateFaultDomain()},
			expResult: &FaultDomainTree{
				Domain:   MustCreateFaultDomain(),
				ID:       expFaultDomainID(0),
				Children: []*FaultDomainTree{},
			},
		},
		"single layer domain": {
			domains: []*FaultDomain{fd1},
			expResult: &FaultDomainTree{
				Domain: MustCreateFaultDomain(),
				ID:     expFaultDomainID(0),
				Children: []*FaultDomainTree{
					{
						Domain:   fd1,
						ID:       expFaultDomainID(1),
						Children: []*FaultDomainTree{},
					},
				},
			},
		},
		"multi-layer domain": {
			domains: []*FaultDomain{fd3},
			expResult: &FaultDomainTree{
				Domain: MustCreateFaultDomain(),
				ID:     FaultDomainRootID,
				Children: []*FaultDomainTree{
					{
						Domain: fd1,
						ID:     expFaultDomainID(1),
						Children: []*FaultDomainTree{
							{
								Domain: fd2,
								ID:     expFaultDomainID(2),
								Children: []*FaultDomainTree{
									{
										Domain:   fd3,
										ID:       expFaultDomainID(3),
										Children: []*FaultDomainTree{},
									},
								},
							},
						},
					},
				},
			},
		},
		"multiple domains": {
			domains: []*FaultDomain{fd3, fd5, fd6},
			expResult: &FaultDomainTree{
				Domain: MustCreateFaultDomain(),
				ID:     FaultDomainRootID,
				Children: []*FaultDomainTree{
					{
						Domain: fd4,
						ID:     expFaultDomainID(4),
						Children: []*FaultDomainTree{
							{
								Domain:   fd5,
								ID:       expFaultDomainID(5),
								Children: []*FaultDomainTree{},
							},
							{
								Domain:   fd6,
								ID:       expFaultDomainID(6),
								Children: []*FaultDomainTree{},
							},
						},
					},
					{
						Domain: fd1,
						ID:     expFaultDomainID(1),
						Children: []*FaultDomainTree{
							{
								Domain: fd2,
								ID:     expFaultDomainID(2),
								Children: []*FaultDomainTree{
									{
										Domain:   fd3,
										ID:       expFaultDomainID(3),
										Children: []*FaultDomainTree{},
									},
								},
							},
						},
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := NewFaultDomainTree(tc.domains...)

			if diff := cmp.Diff(tc.expResult, result); diff != "" {
				t.Fatalf("(-want, +got): %s", diff)
			}
		})
	}
}

// For tests where the ID is unimportant
func ignoreFaultDomainIDOption() cmp.Option {
	return cmp.FilterPath(
		func(p cmp.Path) bool {
			return p.Last().String() == ".ID"
		}, cmp.Ignore())
}

func TestSystem_FaultDomainTree_WithNodeDomain(t *testing.T) {
	for name, tc := range map[string]struct {
		tree      *FaultDomainTree
		domain    *FaultDomain
		expResult *FaultDomainTree
	}{
		"nil tree": {
			domain: MustCreateFaultDomain(),
			expResult: &FaultDomainTree{
				Domain:   MustCreateFaultDomain(),
				Children: []*FaultDomainTree{},
			},
		},
		"root node": {
			tree:   NewFaultDomainTree(),
			domain: MustCreateFaultDomain("multi", "layer"),
			expResult: &FaultDomainTree{
				Domain:   MustCreateFaultDomain("multi", "layer"),
				Children: []*FaultDomainTree{},
			},
		},
		"replace domain": {
			tree: &FaultDomainTree{
				Domain:   MustCreateFaultDomain("something"),
				Children: []*FaultDomainTree{},
			},
			domain: MustCreateFaultDomain("another", "thing"),
			expResult: &FaultDomainTree{
				Domain:   MustCreateFaultDomain("another", "thing"),
				Children: []*FaultDomainTree{},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := tc.tree.WithNodeDomain(tc.domain)

			if diff := cmp.Diff(result, tc.expResult, ignoreFaultDomainIDOption()); diff != "" {
				t.Fatalf("(-want, +got): %s", diff)
			}

			if tc.tree != nil && result != tc.tree {
				t.Fatalf("pointers didn't match")
			}
		})
	}
}

func TestSystem_FaultDomainTree_WithID(t *testing.T) {
	for name, tc := range map[string]struct {
		tree      *FaultDomainTree
		id        uint32
		expResult *FaultDomainTree
	}{
		"nil tree": {
			id: 5,
			expResult: &FaultDomainTree{
				Domain:   MustCreateFaultDomain(),
				ID:       5,
				Children: []*FaultDomainTree{},
			},
		},
		"no original ID set": {
			tree: &FaultDomainTree{
				Domain:   MustCreateFaultDomain("something"),
				Children: []*FaultDomainTree{},
			},
			id: 2,
			expResult: &FaultDomainTree{
				Domain:   MustCreateFaultDomain("something"),
				ID:       2,
				Children: []*FaultDomainTree{},
			},
		},
		"replace nonzero ID": {
			tree: &FaultDomainTree{
				Domain:   MustCreateFaultDomain("something"),
				ID:       25,
				Children: []*FaultDomainTree{},
			},
			id: 1,
			expResult: &FaultDomainTree{
				Domain:   MustCreateFaultDomain("something"),
				ID:       1,
				Children: []*FaultDomainTree{},
			},
		},
		"set to zero": {
			tree: &FaultDomainTree{
				Domain:   MustCreateFaultDomain("something"),
				ID:       25,
				Children: []*FaultDomainTree{},
			},
			id: 0,
			expResult: &FaultDomainTree{
				Domain:   MustCreateFaultDomain("something"),
				Children: []*FaultDomainTree{},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := tc.tree.WithID(tc.id)

			if diff := cmp.Diff(result, tc.expResult); diff != "" {
				t.Fatalf("(-want, +got): %s", diff)
			}

			if tc.tree != nil && result != tc.tree {
				t.Fatalf("pointers didn't match")
			}
		})
	}
}

func TestSystem_FaultDomainTree_nextID(t *testing.T) {
	for name, tc := range map[string]struct {
		tree      *FaultDomainTree
		expResult uint32
	}{
		"nil": {
			expResult: FaultDomainRootID,
		},
		"empty": {
			tree:      NewFaultDomainTree(),
			expResult: FaultDomainRootID + 1,
		},
		"single branch": {
			tree: NewFaultDomainTree(
				MustCreateFaultDomain("one", "two", "three"),
			),
			expResult: FaultDomainRootID + 4,
		},
		"multi branch": {
			tree: NewFaultDomainTree(
				MustCreateFaultDomain("one", "two", "three"),
				MustCreateFaultDomain("four", "five", "six"),
				MustCreateFaultDomain("seven", "eight", "nine", "ten"),
			),
			expResult: FaultDomainRootID + 11,
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.AssertEqual(t, tc.expResult, tc.tree.nextID(), "")
		})
	}
}

func TestSystem_FaultDomainTree_AddDomain(t *testing.T) {
	single := MustCreateFaultDomain("rack0")
	multi := single.MustCreateChild("node1")
	multi2 := single.MustCreateChild("node2")

	for name, tc := range map[string]struct {
		tree      *FaultDomainTree
		toAdd     *FaultDomain
		expResult *FaultDomainTree
		expErr    error
	}{
		"nil tree": {
			toAdd:  MustCreateFaultDomain(),
			expErr: errors.New("nil FaultDomainTree"),
		},
		"nil input": {
			tree:   NewFaultDomainTree(),
			expErr: errors.New("nil domain"),
		},
		"root domain": {
			tree:  NewFaultDomainTree(),
			toAdd: MustCreateFaultDomain(),
		},
		"single level": {
			tree:  NewFaultDomainTree(),
			toAdd: single,
			expResult: &FaultDomainTree{
				Domain: MustCreateFaultDomain(),
				ID:     expFaultDomainID(0),
				Children: []*FaultDomainTree{
					NewFaultDomainTree().WithNodeDomain(single).WithID(expFaultDomainID(1)),
				},
			},
		},
		"multi level": {
			tree:  NewFaultDomainTree(),
			toAdd: multi,
			expResult: &FaultDomainTree{
				Domain: MustCreateFaultDomain(),
				ID:     expFaultDomainID(0),
				Children: []*FaultDomainTree{
					{
						Domain: single,
						ID:     expFaultDomainID(1),
						Children: []*FaultDomainTree{
							{
								Domain:   multi,
								ID:       expFaultDomainID(2),
								Children: []*FaultDomainTree{},
							},
						},
					},
				},
			},
		},
		"branch of existing tree": {
			tree:  NewFaultDomainTree(MustCreateFaultDomain("another", "branch")),
			toAdd: multi,
			expResult: &FaultDomainTree{
				Domain: MustCreateFaultDomain(),
				ID:     expFaultDomainID(0),
				Children: []*FaultDomainTree{
					{
						Domain: MustCreateFaultDomain("another"),
						ID:     expFaultDomainID(1),
						Children: []*FaultDomainTree{
							{
								Domain:   MustCreateFaultDomain("another", "branch"),
								ID:       expFaultDomainID(2),
								Children: []*FaultDomainTree{},
							},
						},
					},
					{
						Domain: single,
						ID:     expFaultDomainID(3),
						Children: []*FaultDomainTree{
							{
								Domain:   multi,
								ID:       expFaultDomainID(4),
								Children: []*FaultDomainTree{},
							},
						},
					},
				},
			},
		},
		"overlap existing tree": {
			tree:  NewFaultDomainTree(multi),
			toAdd: multi2,
			expResult: &FaultDomainTree{
				Domain: MustCreateFaultDomain(),
				ID:     expFaultDomainID(0),
				Children: []*FaultDomainTree{
					{
						Domain: single,
						ID:     expFaultDomainID(1),
						Children: []*FaultDomainTree{
							{
								Domain:   multi,
								ID:       expFaultDomainID(2),
								Children: []*FaultDomainTree{},
							},
							{
								Domain:   multi2,
								ID:       expFaultDomainID(3),
								Children: []*FaultDomainTree{},
							},
						},
					},
				},
			},
		},
		"complete overlap - no change": {
			tree:  NewFaultDomainTree(multi),
			toAdd: single,
		},
	} {
		t.Run(name, func(t *testing.T) {
			if tc.expResult == nil && tc.tree != nil {
				// nil expResult => unchanged from start
				tc.expResult = NewFaultDomainTree()
				*tc.expResult = *tc.tree
			}

			err := tc.tree.AddDomain(tc.toAdd)

			test.CmpErr(t, tc.expErr, err)

			if diff := cmp.Diff(tc.expResult, tc.tree); diff != "" {
				t.Fatalf("(-want, +got): %s", diff)
			}
		})
	}
}

func TestSystem_FaultDomainTree_Merge(t *testing.T) {
	rack0 := MustCreateFaultDomain("rack0")
	rack0node1 := rack0.MustCreateChild("node1")
	rack0node2 := rack0.MustCreateChild("node2")

	rack1 := MustCreateFaultDomain("rack1")
	rack1node3 := rack1.MustCreateChild("node3")
	rack1node4 := rack1.MustCreateChild("node4")

	fullTree := func() *FaultDomainTree {
		return &FaultDomainTree{
			Domain: MustCreateFaultDomain(),
			ID:     expFaultDomainID(0),
			Children: []*FaultDomainTree{
				{
					Domain: rack0,
					ID:     expFaultDomainID(1),
					Children: []*FaultDomainTree{
						{
							Domain:   rack0node1,
							ID:       expFaultDomainID(2),
							Children: []*FaultDomainTree{},
						},
						{
							Domain:   rack0node2,
							ID:       expFaultDomainID(3),
							Children: []*FaultDomainTree{},
						},
					},
				},
				{
					Domain: rack1,
					ID:     expFaultDomainID(4),
					Children: []*FaultDomainTree{
						{
							Domain:   rack1node3,
							ID:       expFaultDomainID(5),
							Children: []*FaultDomainTree{},
						},
						{
							Domain:   rack1node4,
							ID:       expFaultDomainID(6),
							Children: []*FaultDomainTree{},
						},
					},
				},
			},
		}
	}

	for name, tc := range map[string]struct {
		tree      *FaultDomainTree
		toMerge   *FaultDomainTree
		expResult *FaultDomainTree
		expErr    error
	}{
		"nil tree": {
			toMerge: NewFaultDomainTree(),
			expErr:  errors.New("can't merge into nil FaultDomainTree"),
		},
		"nil input - no change": {
			tree: NewFaultDomainTree(),
		},
		"empty input - no change": {
			tree:    NewFaultDomainTree(),
			toMerge: NewFaultDomainTree(),
		},
		"different top level domains can't merge": {
			tree:    NewFaultDomainTree(),
			toMerge: NewFaultDomainTree().WithNodeDomain(rack0),
			expErr:  errors.New("trees cannot be merged"),
		},
		"merge single branch into empty tree": {
			tree:      NewFaultDomainTree(),
			toMerge:   NewFaultDomainTree(rack0node1),
			expResult: NewFaultDomainTree(rack0node1),
		},
		"new single branch from root": {
			tree:    NewFaultDomainTree(rack1),
			toMerge: NewFaultDomainTree(rack0node1),
			expResult: &FaultDomainTree{
				Domain: MustCreateFaultDomain(),
				ID:     expFaultDomainID(0),
				Children: []*FaultDomainTree{
					{
						Domain: rack0,
						ID:     expFaultDomainID(2),
						Children: []*FaultDomainTree{
							{
								Domain:   rack0node1,
								ID:       expFaultDomainID(3),
								Children: []*FaultDomainTree{},
							},
						},
					},
					{
						Domain:   rack1,
						ID:       expFaultDomainID(4),
						Children: []*FaultDomainTree{},
					},
				},
			},
		},
		"single branch partial overlap": {
			tree:    NewFaultDomainTree(rack0node1),
			toMerge: NewFaultDomainTree(rack0node2),
			expResult: &FaultDomainTree{
				Domain: MustCreateFaultDomain(),
				ID:     expFaultDomainID(0),
				Children: []*FaultDomainTree{
					{
						Domain: rack0,
						ID:     expFaultDomainID(1),
						Children: []*FaultDomainTree{
							{
								Domain:   rack0node1,
								ID:       expFaultDomainID(2),
								Children: []*FaultDomainTree{},
							},
							{
								Domain:   rack0node2,
								ID:       expFaultDomainID(3),
								Children: []*FaultDomainTree{},
							},
						},
					},
				},
			},
		},
		"complete overlap - no change": {
			tree:    fullTree(),
			toMerge: NewFaultDomainTree(rack0node2),
		},
		"merge multi-branch tree into empty": {
			tree:      NewFaultDomainTree(),
			toMerge:   fullTree(),
			expResult: fullTree(),
		},
		"merge multi-branch tree into existing": {
			tree:      NewFaultDomainTree(rack0),
			toMerge:   fullTree(),
			expResult: fullTree(),
		},
	} {
		t.Run(name, func(t *testing.T) {
			if tc.expResult == nil && tc.tree != nil {
				// nil expResult => unchanged from start
				tc.expResult = NewFaultDomainTree()
				*tc.expResult = *tc.tree
			}

			err := tc.tree.Merge(tc.toMerge)

			test.CmpErr(t, tc.expErr, err)

			if diff := cmp.Diff(tc.tree, tc.expResult, ignoreFaultDomainIDOption()); diff != "" {
				t.Fatalf("(-want, +got): %s", diff)
			}
		})
	}
}

func TestSystem_FaultDomainTree_RemoveDomain(t *testing.T) {
	rack0 := MustCreateFaultDomain("rack0")
	rack0node1 := rack0.MustCreateChild("node1")
	rack0node2 := rack0.MustCreateChild("node2")

	rack1 := MustCreateFaultDomain("rack1")
	rack1node3 := rack1.MustCreateChild("node3")
	rack1node4 := rack1.MustCreateChild("node4")

	for name, tc := range map[string]struct {
		tree      *FaultDomainTree
		toRemove  *FaultDomain
		expResult *FaultDomainTree // nil == unchanged
		expErr    error
	}{
		"nil tree": {
			expErr: errors.New("nil FaultDomainTree"),
		},
		"remove nil is a no-op": {
			tree: NewFaultDomainTree(rack0, rack1),
		},
		"remove root domain not possible": {
			tree:     NewFaultDomainTree(rack0, rack1),
			toRemove: MustCreateFaultDomain(),
			expErr:   errors.New("cannot remove root fault domain from tree"),
		},
		"remove only leaf": {
			tree:      NewFaultDomainTree(rack0),
			toRemove:  rack0,
			expResult: NewFaultDomainTree(),
		},
		"remove top level leaf": {
			tree:      NewFaultDomainTree(rack0, rack1),
			toRemove:  rack1,
			expResult: NewFaultDomainTree(rack0),
		},
		"remove leaf": {
			tree:      NewFaultDomainTree(rack0node1, rack0node2, rack1node3, rack1node4),
			toRemove:  rack0node2,
			expResult: NewFaultDomainTree(rack0node1, rack1node3, rack1node4),
		},
		"remove branch": {
			tree:      NewFaultDomainTree(rack0node1, rack0node2, rack1node3, rack1node4),
			toRemove:  rack1,
			expResult: NewFaultDomainTree(rack0node1, rack0node2),
		},
		"remove leaf not in tree": {
			tree:     NewFaultDomainTree(rack0node1, rack0node2),
			toRemove: rack0.MustCreateChild("node3"),
		},
		"remove branch not in tree": {
			tree:     NewFaultDomainTree(rack0node1, rack0node2),
			toRemove: rack1,
		},
	} {
		t.Run(name, func(t *testing.T) {
			if tc.expResult == nil && tc.tree != nil {
				// nil expResult => unchanged from start
				tc.expResult = NewFaultDomainTree()
				*tc.expResult = *tc.tree
			}

			err := tc.tree.RemoveDomain(tc.toRemove)

			test.CmpErr(t, tc.expErr, err)

			// ignoring IDs because we don't expect the originals to
			// change on removal
			if diff := cmp.Diff(tc.tree, tc.expResult, ignoreFaultDomainIDOption()); diff != "" {
				t.Fatalf("(-want, +got): %s", diff)
			}
		})
	}
}

func TestSystem_FaultDomainTree_IsRoot(t *testing.T) {
	for name, tc := range map[string]struct {
		tree      *FaultDomainTree
		expResult bool
	}{
		"nil": {},
		"empty root node": {
			tree:      NewFaultDomainTree(),
			expResult: true,
		},
		"non-empty root node": {
			tree:      NewFaultDomainTree(MustCreateFaultDomain("one", "two")),
			expResult: true,
		},
		"contains a domain": {
			tree:      NewFaultDomainTree().WithNodeDomain(MustCreateFaultDomain("fd1")),
			expResult: false,
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.AssertEqual(t, tc.tree.IsRoot(), tc.expResult, "")
		})
	}
}

func TestSystem_FaultDomainTree_IsLeaf(t *testing.T) {
	testTree := NewFaultDomainTree(MustCreateFaultDomain("one", "two"))

	for name, tc := range map[string]struct {
		tree      *FaultDomainTree
		expResult bool
	}{
		"nil": {},
		"root node with no children": {
			tree:      NewFaultDomainTree(),
			expResult: true,
		},
		"root node with children": {
			tree:      testTree,
			expResult: false,
		},
		"non-root node with children": {
			tree:      testTree.Children[0],
			expResult: false,
		},
		"leaf node": {
			tree:      testTree.Children[0].Children[0],
			expResult: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.AssertEqual(t, tc.tree.IsLeaf(), tc.expResult, "")
		})
	}
}

func TestSystem_FaultDomainTree_IsBalanced(t *testing.T) {
	for name, tc := range map[string]struct {
		tree      *FaultDomainTree
		expResult bool
	}{
		"nil": {
			expResult: true,
		},
		"empty": {
			tree:      NewFaultDomainTree(),
			expResult: true,
		},
		"single branch": {
			tree:      NewFaultDomainTree(MustCreateFaultDomainFromString("/one")),
			expResult: true,
		},
		"single multi-level branch": {
			tree:      NewFaultDomainTree(MustCreateFaultDomainFromString("/one/long/road")),
			expResult: true,
		},
		"branches from root imbalance": {
			tree: NewFaultDomainTree(
				MustCreateFaultDomainFromString("/one/long/road"),
				MustCreateFaultDomainFromString("/short/road"),
			),
			expResult: false,
		},
		"branches from root balanced": {
			tree: NewFaultDomainTree(
				MustCreateFaultDomainFromString("/high/road"),
				MustCreateFaultDomainFromString("/low/road"),
			),
			expResult: true,
		},
		"branches imbalanced below top level": {
			tree: NewFaultDomainTree(
				MustCreateFaultDomainFromString("/take/the/high/road"),
				MustCreateFaultDomainFromString("/take/the/low/road/and/I'll/be/in/Scotland/before/ye"),
			),
			expResult: false,
		},
		"big leafy balanced tree": {
			tree: NewFaultDomainTree(
				MustCreateFaultDomainFromString("/one/two/three"),
				MustCreateFaultDomainFromString("/one/two/four"),
				MustCreateFaultDomainFromString("/one/two/five"),
				MustCreateFaultDomainFromString("/one/six/seven"),
				MustCreateFaultDomainFromString("/one/eight/nine"),
				MustCreateFaultDomainFromString("/every/good/boy"),
				MustCreateFaultDomainFromString("/every/bad/dog"),
			),
			expResult: true,
		},
		"big leafy imbalanced tree": {
			tree: NewFaultDomainTree(
				MustCreateFaultDomainFromString("/one/two/three"),
				MustCreateFaultDomainFromString("/one/two/four"),
				MustCreateFaultDomainFromString("/one/two/five"),
				MustCreateFaultDomainFromString("/one/six/seven"),
				MustCreateFaultDomainFromString("/one/eight/nine"),
				MustCreateFaultDomainFromString("/every/good/boy/deserves"),
				MustCreateFaultDomainFromString("/every/bad/dog"),
			),
			expResult: false,
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.AssertEqual(t, tc.tree.IsBalanced(), tc.expResult, "")
		})
	}
}

func TestSystem_FaultDomainTree_String(t *testing.T) {
	for name, tc := range map[string]struct {
		tree      *FaultDomainTree
		expResult string
	}{
		"nil": {
			expResult: "(nil)",
		},
		"empty": {
			tree: NewFaultDomainTree(),
			expResult: `FaultDomainTree:
- /
`,
		},
		"one layer of children": {
			tree: NewFaultDomainTree(
				MustCreateFaultDomainFromString("/one"),
				MustCreateFaultDomainFromString("/two"),
			),
			expResult: `FaultDomainTree:
- /
  - one
  - two
`,
		},
		"multiple layers of children": {
			tree: NewFaultDomainTree(
				MustCreateFaultDomainFromString("/one/two/three"),
				MustCreateFaultDomainFromString("/one/two/four"),
				MustCreateFaultDomainFromString("/one/five"),
				MustCreateFaultDomainFromString("/two/six/seven"),
				MustCreateFaultDomainFromString("/two/eight/nine"),
				MustCreateFaultDomainFromString("/two/eight/ten/eleven"),
			),
			expResult: `FaultDomainTree:
- /
  - one
    - five
    - two
      - four
      - three
  - two
    - eight
      - nine
      - ten
        - eleven
    - six
      - seven
`,
		},
		"non-root top level": {
			tree: NewFaultDomainTree(
				MustCreateFaultDomainFromString("/one"),
				MustCreateFaultDomainFromString("/two"),
			).WithNodeDomain(MustCreateFaultDomain("extra", "layer")),
			expResult: `FaultDomainTree:
- /extra/layer
  - one
  - two
`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.AssertEqual(t, tc.tree.String(), tc.expResult, "")
		})
	}
}

func testVerifyTreeStructure(t *testing.T, tree *FaultDomainTree, level int, expNumChildrenByLevel []int) {
	// Walk the tree to verify results
	test.AssertEqual(t, len(tree.Children), expNumChildrenByLevel[level],
		fmt.Sprintf("mismatch at level %d, %q", level, tree.Domain))
	for _, c := range tree.Children {
		testVerifyTreeStructure(t, c, level+1, expNumChildrenByLevel)
	}
}

func verifyTreeDiffMem(t *testing.T, orig, result *FaultDomainTree) {
	if orig == result {
		t.Fatalf("original and result node %q point to the same memory", orig.Domain.String())
	}
	for i := range orig.Children {
		verifyTreeDiffMem(t, orig.Children[i], result.Children[i])
	}
}

func TestSystem_FaultDomainTree_Copy(t *testing.T) {
	for name, tc := range map[string]struct {
		origTree *FaultDomainTree
	}{
		"nil": {},
		"empty tree": {
			origTree: NewFaultDomainTree(),
		},
		"with children": {
			NewFaultDomainTree(
				MustCreateFaultDomainFromString("/rack0/pdu0"),
				MustCreateFaultDomainFromString("/rack0/pdu1"),
				MustCreateFaultDomainFromString("/rack1/pdu2"),
				MustCreateFaultDomainFromString("/rack1/pdu3"),
			),
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := tc.origTree.Copy()

			if diff := cmp.Diff(tc.origTree, result); diff != "" {
				t.Fatalf("(-want, +got): %s", diff)
			}

			if tc.origTree != nil {
				verifyTreeDiffMem(t, tc.origTree, result)
			}
		})
	}
}

func TestSystem_FaultDomain_Depth(t *testing.T) {
	for name, tc := range map[string]struct {
		tree     *FaultDomainTree
		expDepth int
	}{
		"nil": {},
		"empty tree": {
			tree: NewFaultDomainTree(),
		},
		"single node": {
			tree: NewFaultDomainTree(
				MustCreateFaultDomainFromString("/rack0"),
			),
			expDepth: 1,
		},
		"balanced multi-branch": {
			tree: NewFaultDomainTree(
				MustCreateFaultDomainFromString("/rack0/pdu0"),
				MustCreateFaultDomainFromString("/rack0/pdu1"),
				MustCreateFaultDomainFromString("/rack1/pdu2"),
				MustCreateFaultDomainFromString("/rack1/pdu3"),
			),
			expDepth: 2,
		},
		"unbalanced": {
			tree: NewFaultDomainTree(
				MustCreateFaultDomainFromString("/rack0/pdu0"),
				MustCreateFaultDomainFromString("/rack0/pdu1"),
				MustCreateFaultDomainFromString("/rack1/pdu2/shelf0/node1"),
				MustCreateFaultDomainFromString("/rack1/pdu3/node0"),
			),
			expDepth: 4,
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.AssertEqual(t, tc.tree.Depth(), tc.expDepth, "")
		})
	}
}

func TestSystem_FaultDomainTree_Domains(t *testing.T) {
	for name, tc := range map[string]struct {
		tree       *FaultDomainTree
		expDomains []*FaultDomain
	}{
		"nil": {},
		"empty": {
			tree:       NewFaultDomainTree(),
			expDomains: []*FaultDomain{},
		},
		"one leaf": {
			tree: NewFaultDomainTree(MustCreateFaultDomain("leaf")),
			expDomains: []*FaultDomain{
				MustCreateFaultDomain("leaf"),
			},
		},
		"multi-layer": {
			tree: NewFaultDomainTree(MustCreateFaultDomain("one", "two", "three")),
			expDomains: []*FaultDomain{
				MustCreateFaultDomain("one", "two", "three"),
			},
		},
		"multi-branch": {
			tree: NewFaultDomainTree(
				MustCreateFaultDomain("a", "b", "c"),
				MustCreateFaultDomain("a", "b", "d"),
				MustCreateFaultDomain("a", "e", "f"),
				MustCreateFaultDomain("g", "h", "i"),
				MustCreateFaultDomain("g", "j", "k"),
			),
			expDomains: []*FaultDomain{
				MustCreateFaultDomain("a", "b", "c"),
				MustCreateFaultDomain("a", "b", "d"),
				MustCreateFaultDomain("a", "e", "f"),
				MustCreateFaultDomain("g", "h", "i"),
				MustCreateFaultDomain("g", "j", "k"),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			domains := tc.tree.Domains()
			if diff := cmp.Diff(tc.expDomains, domains); diff != "" {
				t.Fatalf("(-want, +got): %s", diff)
			}
		})
	}
}

func TestSystem_FaultDomain_Subtree(t *testing.T) {
	fullDomains := []*FaultDomain{
		MustCreateFaultDomain("a", "b", "c"),
		MustCreateFaultDomain("a", "b", "d"),
		MustCreateFaultDomain("a", "e", "f"),
		MustCreateFaultDomain("g", "h", "i"),
		MustCreateFaultDomain("g", "j", "k"),
	}
	fullTree := NewFaultDomainTree(fullDomains...)

	for name, tc := range map[string]struct {
		tree      *FaultDomainTree
		domains   []*FaultDomain
		expResult *FaultDomainTree
		expErr    error
	}{
		"nil": {
			expErr: errors.New("nil FaultDomainTree"),
		},
		"no domains": {
			tree: NewFaultDomainTree(
				MustCreateFaultDomain("one"),
				MustCreateFaultDomain("two"),
			),
			expResult: NewFaultDomainTree(
				MustCreateFaultDomain("one"),
				MustCreateFaultDomain("two"),
			),
		},
		"empty": {
			tree:      NewFaultDomainTree(),
			domains:   []*FaultDomain{MustCreateFaultDomain()},
			expResult: NewFaultDomainTree(),
		},
		"request all domains": {
			tree:      fullTree,
			domains:   fullDomains,
			expResult: fullTree,
		},
		"single branch requested": {
			tree: fullTree,
			domains: []*FaultDomain{
				MustCreateFaultDomain("a", "e", "f"),
			},
			expResult: &FaultDomainTree{
				Domain: MustCreateFaultDomain(),
				ID:     expFaultDomainID(0),
				Children: []*FaultDomainTree{
					{
						Domain: MustCreateFaultDomain("a"),
						ID:     expFaultDomainID(1),
						Children: []*FaultDomainTree{
							{
								Domain: MustCreateFaultDomain("a", "e"),
								ID:     expFaultDomainID(5), // preserve IDs from original tree
								Children: []*FaultDomainTree{
									{
										Domain:   MustCreateFaultDomain("a", "e", "f"),
										ID:       expFaultDomainID(6),
										Children: []*FaultDomainTree{},
									},
								},
							},
						},
					},
				},
			},
		},
		"domain not a full branch": {
			tree: fullTree,
			domains: []*FaultDomain{
				MustCreateFaultDomain("a", "e"),
			},
			expResult: &FaultDomainTree{
				Domain: MustCreateFaultDomain(),
				ID:     expFaultDomainID(0),
				Children: []*FaultDomainTree{
					{
						Domain: MustCreateFaultDomain("a"),
						ID:     expFaultDomainID(1),
						Children: []*FaultDomainTree{
							{
								Domain:   MustCreateFaultDomain("a", "e"),
								ID:       expFaultDomainID(5), // preserve IDs from original tree
								Children: []*FaultDomainTree{},
							},
						},
					},
				},
			},
		},
		"domain not in tree": {
			tree: fullTree,
			domains: []*FaultDomain{
				MustCreateFaultDomain("a", "e", "f"),
				MustCreateFaultDomain("a", "e", "notreal"),
			},
			expErr: errors.New("domain \"/a/e/notreal\" not found"),
		},
		"multiple domains requested": {
			tree: fullTree,
			domains: []*FaultDomain{
				MustCreateFaultDomain("g", "j"),
				MustCreateFaultDomain("a", "e", "f"),
				MustCreateFaultDomain("g", "h", "i"),
			},
			expResult: &FaultDomainTree{
				Domain: MustCreateFaultDomain(),
				ID:     expFaultDomainID(0),
				Children: []*FaultDomainTree{
					{
						Domain: MustCreateFaultDomain("a"),
						ID:     expFaultDomainID(1),
						Children: []*FaultDomainTree{
							{
								Domain: MustCreateFaultDomain("a", "e"),
								ID:     expFaultDomainID(5), // preserve IDs from original tree
								Children: []*FaultDomainTree{
									{
										Domain:   MustCreateFaultDomain("a", "e", "f"),
										ID:       expFaultDomainID(6),
										Children: []*FaultDomainTree{},
									},
								},
							},
						},
					},
					{
						Domain: MustCreateFaultDomain("g"),
						ID:     expFaultDomainID(7),
						Children: []*FaultDomainTree{
							{
								Domain: MustCreateFaultDomain("g", "h"),
								ID:     expFaultDomainID(8),
								Children: []*FaultDomainTree{
									{
										Domain:   MustCreateFaultDomain("g", "h", "i"),
										ID:       expFaultDomainID(9),
										Children: []*FaultDomainTree{},
									},
								},
							},
							{
								Domain:   MustCreateFaultDomain("g", "j"),
								ID:       expFaultDomainID(10),
								Children: []*FaultDomainTree{},
							},
						},
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			result, err := tc.tree.Subtree(tc.domains...)

			test.CmpErr(t, tc.expErr, err)

			if diff := cmp.Diff(tc.expResult, result); diff != "" {
				t.Fatalf("(-want, +got): %s", diff)
			}
		})
	}
}

func TestSystem_FaultDomainTree_iterative_building(t *testing.T) {
	mustAddDomain := func(t *FaultDomainTree, d *FaultDomain) {
		if err := t.AddDomain(d); err != nil {
			panic(err)
		}
	}

	fmtNode := func(prefix string, idx int) string {
		return fmt.Sprintf("%s%02d", prefix, idx)
	}

	numPdus := 4
	racksPerPdu := 8
	serversPerRack := 8
	ranksPerServer := 2

	curRack := 0
	curServer := 0
	curRank := 0

	tree := NewFaultDomainTree()
	for p := 0; p < numPdus; p++ {
		pdu := tree.Domain.MustCreateChild(fmtNode("pdu", p))
		mustAddDomain(tree, pdu)
		for pr := 0; pr < racksPerPdu; pr++ {
			rack := pdu.MustCreateChild(fmtNode("rack", curRack))
			mustAddDomain(tree, rack)
			curRack++
			for rs := 0; rs < serversPerRack; rs++ {
				srv := rack.MustCreateChild(fmtNode("server", curServer))
				mustAddDomain(tree, srv)
				curServer++
				for sr := 0; sr < ranksPerServer; sr++ {
					rank := srv.MustCreateChild(fmtNode("rank", curRank))
					mustAddDomain(tree, rank)
					curRank++
				}
			}
		}
	}

	testVerifyTreeStructure(t, tree, 0, []int{
		numPdus,
		racksPerPdu,
		serversPerRack,
		ranksPerServer,
		0,
	})
}
