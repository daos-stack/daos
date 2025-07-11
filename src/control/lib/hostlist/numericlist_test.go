//
// (C) Copyright 2019-2022 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package hostlist_test

import (
	"errors"
	"testing"

	"github.com/daos-stack/daos/src/control/lib/hostlist"
)

func TestHostList_NumericList(t *testing.T) {
	uints := func(input ...uint) []uint {
		return input
	}

	for name, tc := range map[string]struct {
		startList   []uint
		addList     []uint
		delList     []uint
		expOut      string
		expFinalOut string
		expCount    int
		expErr      error
	}{
		"add to and delete from empty list": {
			startList:   uints(),
			addList:     uints(0, 2, 5),
			expOut:      "[0,2,5]",
			delList:     uints(2),
			expFinalOut: "[0,5]",
			expCount:    2,
		},
		"add to and delete from existing list": {
			startList:   uints(1, 3, 4),
			addList:     uints(0, 2, 5),
			expOut:      "[1,3-4,0,2,5]",
			delList:     uints(4),
			expFinalOut: "[1,3,0,2,5]",
			expCount:    5,
		},
		"add dupes": {
			addList:     uints(1, 1, 1),
			expOut:      "[1,1,1]",
			expFinalOut: "[1,1,1]",
			expCount:    3,
		},
		"delete one dupe": {
			startList:   uints(1, 1, 1),
			expOut:      "[1,1,1]",
			delList:     uints(1),
			expFinalOut: "[1,1]",
			expCount:    2,
		},
		"delete non-existent no-op": {
			startList:   uints(1, 3, 4),
			expOut:      "[1,3-4]",
			delList:     uints(5),
			expFinalOut: "[1,3-4]",
			expCount:    3,
		},
	} {
		t.Run(name, func(t *testing.T) {
			nl := hostlist.NewNumericList(tc.startList...)
			for _, i := range tc.addList {
				nl.Add(i)
			}
			cmpOut(t, tc.expOut, nl.String())

			for _, i := range tc.delList {
				nl.Delete(i)
			}
			cmpOut(t, tc.expFinalOut, nl.String())

			gotCount := nl.Count()
			if gotCount != tc.expCount {
				t.Fatalf("expected count to be %d; got %d", tc.expCount, gotCount)
			}
		})
	}
}

func TestHostList_NumericList_Contains(t *testing.T) {
	for name, tc := range map[string]struct {
		startList   string
		searchNum   uint
		expContains bool
	}{
		"missing": {
			startList:   "[1-128]",
			searchNum:   200,
			expContains: false,
		},
		"found": {
			startList:   "[1-128]",
			searchNum:   126,
			expContains: true,
		},
		"gaps in range": {
			startList:   "[1-125,127,128]",
			searchNum:   126,
			expContains: false,
		},
	} {
		t.Run(name, func(t *testing.T) {
			nl, err := hostlist.CreateNumericList(tc.startList)
			if err != nil {
				t.Fatal(err)
			}

			gotContains := nl.Contains(tc.searchNum)
			if gotContains != tc.expContains {
				t.Fatalf("expected %d to be found in %s, want %v got %v",
					tc.searchNum, tc.startList, tc.expContains, gotContains)
			}
		})
	}
}

func TestHostList_CreateNumericList(t *testing.T) {
	for name, tc := range map[string]struct {
		startList    string
		expRawOut    string
		expUniqOut   string
		expUniqCount int
		expErr       error
	}{
		"letters in numeric list": {
			startList: "node[1-128]",
			expErr:    errors.New("unexpected alphabetic character(s)"),
		},
		"ranges only": {
			startList:    "[0-1,3-5,3]",
			expRawOut:    "[0-1,3-5,3]",
			expUniqOut:   "[0-1,3-5]",
			expUniqCount: 5,
		},
	} {
		t.Run(name, func(t *testing.T) {
			hl, gotErr := hostlist.CreateNumericList(tc.startList)
			if gotErr != nil {
				t.Log(gotErr.Error())
			}
			cmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			cmpOut(t, tc.expRawOut, hl.String())
			hl.Uniq()
			cmpOut(t, tc.expUniqOut, hl.String())

			gotCount := hl.Count()
			if gotCount != tc.expUniqCount {
				t.Fatalf("expected count to be %d; got %d", tc.expUniqCount, gotCount)
			}
		})
	}
}

func TestHostList_NumericSet(t *testing.T) {
	uints := func(input ...uint) []uint {
		return input
	}

	for name, tc := range map[string]struct {
		startList   []uint
		addList     []uint
		delList     []uint
		expOut      string
		expFinalOut string
		expCount    int
		expErr      error
	}{
		"add to and delete from empty set": {
			startList:   uints(),
			addList:     uints(0, 2, 5),
			expOut:      "[0,2,5]",
			delList:     uints(2),
			expFinalOut: "[0,5]",
			expCount:    2,
		},
		"add to and delete from existing set": {
			startList:   uints(1, 3, 4),
			addList:     uints(0, 2, 5),
			expOut:      "[0-5]",
			delList:     uints(4),
			expFinalOut: "[0-3,5]",
			expCount:    5,
		},
		"delete non-existent no-op": {
			startList:   uints(1, 3, 4),
			expOut:      "[1,3-4]",
			delList:     uints(5),
			expFinalOut: "[1,3-4]",
			expCount:    3,
		},
		"test dupes": {
			startList:   uints(1, 1, 1),
			addList:     uints(1, 1, 1),
			expOut:      "1",
			expFinalOut: "1",
			expCount:    1,
		},
	} {
		t.Run(name, func(t *testing.T) {
			nl := hostlist.NewNumericSet(tc.startList...)
			for _, i := range tc.addList {
				nl.Add(i)
			}
			cmpOut(t, tc.expOut, nl.String())

			for _, i := range tc.delList {
				nl.Delete(i)
			}
			cmpOut(t, tc.expFinalOut, nl.String())

			gotCount := nl.Count()
			if gotCount != tc.expCount {
				t.Fatalf("expected count to be %d; got %d", tc.expCount, gotCount)
			}
		})
	}
}

func TestHostSet_CreateNumericSet(t *testing.T) {
	for name, tc := range map[string]struct {
		startList string
		expOut    string
		expCount  int
		expErr    error
	}{
		"letters in numeric list": {
			startList: "node2-1,node1-2.suffix1,node1-[45,47].suffix2,node3,node1-3",
			expErr:    errors.New("unexpected alphabetic character(s)"),
		},
		"ranges only": {
			startList: "[1-128,2,4,8,6,32,64,128]",
			expOut:    "[1-128]",
			expCount:  128,
		},
		"whitespace in numeric list": {
			startList: "[0, 5]",
			expErr:    errors.New("unexpected whitespace character(s)"),
		},
		"no brackets": {
			startList: "1-128",
			expErr:    errors.New("missing brackets around numeric ranges"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			hs, gotErr := hostlist.CreateNumericSet(tc.startList)
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
