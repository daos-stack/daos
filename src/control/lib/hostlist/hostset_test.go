//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
package hostlist_test

import (
	"errors"
	"testing"

	"github.com/daos-stack/daos/src/control/lib/hostlist"
)

// NB: The testing here is minimal because the bulk of the logic is in HostList. We
// just want to verify that HostSet automatically sorts/dedupes.

func TestHostSet_Create(t *testing.T) {
	for name, tc := range map[string]struct {
		startList string
		expOut    string
		expCount  int
		expErr    error
	}{
		"complex with suffixes": {
			startList: "node2-1,node1-2.suffix1,node1-[45,47].suffix2,node3,node1-3",
			expOut:    "node3,node1-3,node1-2.suffix1,node1-[45,47].suffix2,node2-1",
			expCount:  6,
		},
		"duplicates removed": {
			startList: "node[1-128],node2,node4,node8,node16,node32,node64,node128",
			expOut:    "node[1-128]",
			expCount:  128,
		},
		"port included": {
			startList: "server[1,3,5,7,9]:10000,server[2,4,6,8,10]:10001",
			expOut:    "server[1,3,5,7,9]:10000,server[2,4,6,8,10]:10001",
			expCount:  10,
		},
	} {
		t.Run(name, func(t *testing.T) {
			hs, gotErr := hostlist.CreateSet(tc.startList)
			cmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			cmpOut(t, tc.expOut, hs.String())

			gotCount := hs.Count()
			if gotCount != tc.expCount {
				t.Fatalf("\nexpected count to be %d; got %d", tc.expCount, gotCount)
			}
		})
	}
}

func TestHostSet_Insert(t *testing.T) {
	for name, tc := range map[string]struct {
		startList string
		pushList  string
		expOut    string
		expCount  int
		expErr    error
	}{
		"complex with duplicates": {
			startList: "node2-1,node1-2.suffix1,node1-[45,47].suffix2,node3,node1-3",
			pushList:  "node1-[1-5],node1-[32-64].suffix2",
			expOut:    "node3,node1-[1-5],node1-2.suffix1,node1-[32-64].suffix2,node2-1",
			expCount:  41,
		},
	} {
		t.Run(name, func(t *testing.T) {
			hs, err := hostlist.CreateSet(tc.startList)
			if err != nil {
				t.Fatal(err)
			}

			_, gotErr := hs.Insert(tc.pushList)
			cmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			cmpOut(t, tc.expOut, hs.String())

			gotCount := hs.Count()
			if gotCount != tc.expCount {
				t.Fatalf("\nexpected count to be %d; got %d", tc.expCount, gotCount)
			}
		})
	}
}

func TestHostSet_Intersects(t *testing.T) {
	defaultList := makeStringRef("node[1-128]")

	for name, tc := range map[string]struct {
		startList *string
		checkList string
		expString string
		expErr    error
	}{
		"empty list": {
			startList: makeStringRef(""),
			checkList: "node1",
		},
		"single node": {
			checkList: "node1",
			expString: "node1",
		},
		"range": {
			checkList: "node[1-8]",
			expString: "node[1-8]",
		},
		"range overflows": {
			checkList: "node[1-256]",
			expString: "node[1-128]",
		},
		"range overlaps": {
			checkList: "node[128-256]",
			expString: "node128",
		},
	} {
		t.Run(name, func(t *testing.T) {
			if tc.startList == nil {
				tc.startList = defaultList
			}

			hs, err := hostlist.CreateSet(*tc.startList)
			if err != nil {
				t.Fatal(err)
			}

			gotSet, gotErr := hs.Intersects(tc.checkList)
			cmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			cmpOut(t, tc.expString, gotSet.String())
		})
	}
}

func TestHostSet_ZeroValue(t *testing.T) {
	zVal := &hostlist.HostSet{}

	_, err := zVal.Insert("host[1-8]")
	if err != nil {
		t.Fatal(err)
	}

	gotCount := zVal.Count()
	if gotCount != 8 {
		t.Fatalf("expected count to be 8, got %d", gotCount)
	}
}

func TestHostSet_MergeSet(t *testing.T) {
	a, err := hostlist.CreateSet("host[1-8]")
	if err != nil {
		t.Fatal(err)
	}
	b, err := hostlist.CreateSet("host[8-15]")
	if err != nil {
		t.Fatal(err)
	}
	expCount := a.Count() + b.Count() - 1

	if err := a.MergeSet(b); err != nil {
		t.Fatal(err)
	}

	gotCount := a.Count()
	if gotCount != expCount {
		t.Fatalf("expected count to be %d, got %d", expCount, gotCount)
	}
}

func TestHostSet_ReplaceSet(t *testing.T) {
	a, err := hostlist.CreateSet("host[1-8]")
	if err != nil {
		t.Fatal(err)
	}
	b, err := hostlist.CreateSet("host[8-15]")
	if err != nil {
		t.Fatal(err)
	}

	a.ReplaceSet(b)
	if a.String() != b.String() {
		t.Fatalf("%s != %s", a, b)
	}
}

func TestHostSet_FuzzCrashers(t *testing.T) {
	// Test against problematic inputs found by go-fuzz testing

	for input, tc := range map[string]struct {
		expErr    error
		expDelErr error
	}{
		// check for r < l
		"00]000000[": {
			expErr: errors.New("invalid range"),
		},
		// -ETOOBIG
		"host[1-10000000000]": {
			expErr: errors.New("invalid range"),
		},
		// should be parsed as two ranges, not merged
		"d,d1": {},
		// don't allow nonsensical prefixes
		"-,-,,1 b,e-,d,y _m,%": {
			expErr: errors.New("invalid hostname"),
		},
		// require a prefix (duh!)
		"[45,    7,2,42,4,6,8,8,0]": {
			expErr: errors.New("invalid range"),
		},
		// be careful with uint--
		"host0": {},
	} {
		t.Run(input, func(t *testing.T) {
			hs, gotErr := hostlist.CreateSet(input)
			cmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			t.Logf("set: %s", hs)

			_, delErr := hs.Delete(input)
			cmpErr(t, tc.expDelErr, delErr)
		})
	}
}
