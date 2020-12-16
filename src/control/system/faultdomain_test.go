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
	"fmt"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
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

			common.AssertEqual(t, tc.expResult, fd1.IsAncestorOf(fd2), "")
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

func TestSystem_FaultDomain_Uint64(t *testing.T) {
	// The main thing we care about with these int values is that they
	// don't (often) collide.

	fdList := testFaultDomains(5000)

	collisions := 0
	fdInts := make(map[uint64][]string)
	for _, fd := range fdList {
		key := fd.Uint64()
		matching, exists := fdInts[key]
		if exists {
			t.Logf("collision(s) for domain %s: %s", fd, matching)
			collisions++
		}
		matching = append(matching, fd.String())
		fdInts[key] = matching
	}

	if collisions > 0 {
		t.Errorf("Collisions: %d", collisions)
	}
}

func TestSystem_FaultDomain_Uint32(t *testing.T) {
	// The main thing we care about with these int values is that they
	// don't (often) collide.

	fdList := testFaultDomains(5000)

	collisions := 0
	fdInts := make(map[uint32][]string)
	for _, fd := range fdList {
		key := fd.Uint32()
		matching, exists := fdInts[key]
		if exists {
			t.Logf("collision(s) for domain %s: %s", fd, matching)
			collisions++
		}
		matching = append(matching, fd.String())
		fdInts[key] = matching
	}

	if collisions > 0 {
		t.Errorf("Collisions: %d", collisions)
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
				Children: []*FaultDomainTree{},
			},
		},
		"nil domain": {
			domains: []*FaultDomain{nil},
			expResult: &FaultDomainTree{
				Domain:   MustCreateFaultDomain(),
				Children: []*FaultDomainTree{},
			},
		},
		"empty domain": {
			domains: []*FaultDomain{MustCreateFaultDomain()},
			expResult: &FaultDomainTree{
				Domain:   MustCreateFaultDomain(),
				Children: []*FaultDomainTree{},
			},
		},
		"single layer domain": {
			domains: []*FaultDomain{fd1},
			expResult: &FaultDomainTree{
				Domain: MustCreateFaultDomain(),
				Children: []*FaultDomainTree{
					{
						Domain:   fd1,
						Children: []*FaultDomainTree{},
					},
				},
			},
		},
		"multi-layer domain": {
			domains: []*FaultDomain{fd3},
			expResult: &FaultDomainTree{
				Domain: MustCreateFaultDomain(),
				Children: []*FaultDomainTree{
					{
						Domain: fd1,
						Children: []*FaultDomainTree{
							{
								Domain: fd2,
								Children: []*FaultDomainTree{
									{
										Domain:   fd3,
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
				Children: []*FaultDomainTree{
					{
						Domain: fd4,
						Children: []*FaultDomainTree{
							{
								Domain:   fd5,
								Children: []*FaultDomainTree{},
							},
							{
								Domain:   fd6,
								Children: []*FaultDomainTree{},
							},
						},
					},
					{
						Domain: fd1,
						Children: []*FaultDomainTree{
							{
								Domain: fd2,
								Children: []*FaultDomainTree{
									{
										Domain:   fd3,
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

			if diff := cmp.Diff(result, tc.expResult); diff != "" {
				t.Fatalf("(-want, +got): %s", diff)
			}

			if tc.tree != nil && result != tc.tree {
				t.Fatalf("pointers didn't match")
			}
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
			expErr: errors.New("can't add to nil FaultDomainTree"),
		},
		"nil input": {
			tree:   NewFaultDomainTree(),
			expErr: errors.New("can't add empty fault domain to tree"),
		},
		"empty input": {
			tree:   NewFaultDomainTree(),
			toAdd:  MustCreateFaultDomain(),
			expErr: errors.New("can't add empty fault domain to tree"),
		},
		"single level": {
			tree:  NewFaultDomainTree(),
			toAdd: single,
			expResult: &FaultDomainTree{
				Domain: MustCreateFaultDomain(),
				Children: []*FaultDomainTree{
					NewFaultDomainTree().WithNodeDomain(single),
				},
			},
		},
		"multi level": {
			tree:  NewFaultDomainTree(),
			toAdd: multi,
			expResult: &FaultDomainTree{
				Domain: MustCreateFaultDomain(),
				Children: []*FaultDomainTree{
					{
						Domain: single,
						Children: []*FaultDomainTree{
							{
								Domain:   multi,
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
				Children: []*FaultDomainTree{
					{
						Domain: MustCreateFaultDomain("another"),
						Children: []*FaultDomainTree{
							{
								Domain:   MustCreateFaultDomain("another", "branch"),
								Children: []*FaultDomainTree{},
							},
						},
					},
					{
						Domain: single,
						Children: []*FaultDomainTree{
							{
								Domain:   multi,
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
				Children: []*FaultDomainTree{
					{
						Domain: single,
						Children: []*FaultDomainTree{
							{
								Domain:   multi,
								Children: []*FaultDomainTree{},
							},
							{
								Domain:   multi2,
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

			common.CmpErr(t, tc.expErr, err)

			if diff := cmp.Diff(tc.tree, tc.expResult); diff != "" {
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
			Children: []*FaultDomainTree{
				{
					Domain: rack0,
					Children: []*FaultDomainTree{
						{
							Domain:   rack0node1,
							Children: []*FaultDomainTree{},
						},
						{
							Domain:   rack0node2,
							Children: []*FaultDomainTree{},
						},
					},
				},
				{
					Domain: rack1,
					Children: []*FaultDomainTree{
						{
							Domain:   rack1node3,
							Children: []*FaultDomainTree{},
						},
						{
							Domain:   rack1node4,
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
				Children: []*FaultDomainTree{
					{
						Domain: rack0,
						Children: []*FaultDomainTree{
							{
								Domain:   rack0node1,
								Children: []*FaultDomainTree{},
							},
						},
					},
					{
						Domain:   rack1,
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
				Children: []*FaultDomainTree{
					{
						Domain: rack0,
						Children: []*FaultDomainTree{
							{
								Domain:   rack0node1,
								Children: []*FaultDomainTree{},
							},
							{
								Domain:   rack0node2,
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

			common.CmpErr(t, tc.expErr, err)

			if diff := cmp.Diff(tc.tree, tc.expResult); diff != "" {
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

			common.CmpErr(t, tc.expErr, err)

			if diff := cmp.Diff(tc.tree, tc.expResult); diff != "" {
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
			common.AssertEqual(t, tc.tree.IsRoot(), tc.expResult, "")
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
			common.AssertEqual(t, tc.tree.IsLeaf(), tc.expResult, "")
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
			common.AssertEqual(t, tc.tree.IsBalanced(), tc.expResult, "")
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
			common.AssertEqual(t, tc.tree.String(), tc.expResult, "")
		})
	}
}

func TestSystem_FaultDomainTree_ToProto(t *testing.T) {
	getExpID := func(domain string) uint32 {
		return MustCreateFaultDomainFromString(domain).Uint32()
	}

	for name, tc := range map[string]struct {
		tree      *FaultDomainTree
		expResult []*mgmtpb.FaultDomain
	}{
		"nil": {},
		"root only": {
			tree: NewFaultDomainTree(),
			expResult: []*mgmtpb.FaultDomain{
				{
					Domain: "/",
					Id:     getExpID("/"),
				},
			},
		},
		"single branch": {
			tree: NewFaultDomainTree(
				MustCreateFaultDomain("one", "two", "three"),
			),
			expResult: []*mgmtpb.FaultDomain{
				{
					Domain:   "/",
					Id:       getExpID("/"),
					Children: []uint32{getExpID("/one")},
				},
				{
					Domain:   "/one",
					Id:       getExpID("/one"),
					Children: []uint32{getExpID("/one/two")},
				},
				{
					Domain:   "/one/two",
					Id:       getExpID("/one/two"),
					Children: []uint32{getExpID("/one/two/three")},
				},
				{
					Domain: "/one/two/three",
					Id:     getExpID("/one/two/three"),
				},
			},
		},
		"multi branch": {
			tree: NewFaultDomainTree(
				MustCreateFaultDomainFromString("/rack0/pdu0"),
				MustCreateFaultDomainFromString("/rack0/pdu1"),
				MustCreateFaultDomainFromString("/rack1/pdu2"),
				MustCreateFaultDomainFromString("/rack1/pdu3"),
			),
			expResult: []*mgmtpb.FaultDomain{
				{
					Domain: "/",
					Id:     getExpID("/"),
					Children: []uint32{
						getExpID("/rack0"),
						getExpID("/rack1"),
					},
				},
				{
					Domain: "/rack0",
					Id:     getExpID("/rack0"),
					Children: []uint32{
						getExpID("/rack0/pdu0"),
						getExpID("/rack0/pdu1"),
					},
				},
				{
					Domain: "/rack1",
					Id:     getExpID("/rack1"),
					Children: []uint32{
						getExpID("/rack1/pdu2"),
						getExpID("/rack1/pdu3"),
					},
				},
				{
					Domain: "/rack0/pdu0",
					Id:     getExpID("/rack0/pdu0"),
				},
				{
					Domain: "/rack0/pdu1",
					Id:     getExpID("/rack0/pdu1"),
				},
				{
					Domain: "/rack1/pdu2",
					Id:     getExpID("/rack1/pdu2"),
				},
				{
					Domain: "/rack1/pdu3",
					Id:     getExpID("/rack1/pdu3"),
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := tc.tree.ToProto()

			if diff := cmp.Diff(tc.expResult, result); diff != "" {
				t.Fatalf("(-want, +got): %s", diff)
			}
		})
	}
}
