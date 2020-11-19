//
// (C) Copyright 2019-2020 Intel Corporation.
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
package hostlist_test

import (
	"errors"
	"reflect"
	"testing"

	"github.com/daos-stack/daos/src/control/lib/hostlist"
)

func makeStringRef(in string) *string {
	return &in
}

func makeTestList(t *testing.T, in string) *hostlist.HostList {
	t.Helper()
	hl, err := hostlist.Create(in)
	if err != nil {
		t.Fatal(err)
	}
	return hl
}

func TestHostList_Create(t *testing.T) {
	for name, tc := range map[string]struct {
		startList    string
		expRawOut    string
		expUniqOut   string
		expUniqCount int
		expErr       error
	}{
		"simple": {
			startList:    "node[1-128]",
			expRawOut:    "node[1-128]",
			expUniqOut:   "node[1-128]",
			expUniqCount: 128,
		},
		"complex": {
			startList:    "node2-1,node1-2,node1-[45,47],node3,node1-3",
			expRawOut:    "node2-1,node1-[2,45,47],node3,node1-3",
			expUniqOut:   "node3,node1-[2-3,45,47],node2-1",
			expUniqCount: 6,
		},
		"complex with suffixes": {
			startList:    "node2-1,node1-2.suffix1,node1-[45,47].suffix2,node3,node1-3",
			expRawOut:    "node2-1,node1-2.suffix1,node1-[45,47].suffix2,node3,node1-3",
			expUniqOut:   "node3,node1-3,node1-2.suffix1,node1-[45,47].suffix2,node2-1",
			expUniqCount: 6,
		},
		"prefix-N": {
			startList:    "node-3,node-1",
			expRawOut:    "node-[3,1]",
			expUniqOut:   "node-[1,3]",
			expUniqCount: 2,
		},
		"prefixN-N": {
			startList:    "node1-3,node1-1,node2-1",
			expRawOut:    "node1-[3,1],node2-1",
			expUniqOut:   "node1-[1,3],node2-1",
			expUniqCount: 3,
		},
		"IP address range": {
			startList:    "10.5.1.[2-32]:10001,10.5.1.42:10001,10.5.1.1:10001",
			expRawOut:    "10.5.1.[2-32,42,1]:10001",
			expUniqOut:   "10.5.1.[1-32,42]:10001",
			expUniqCount: 33,
		},
		"duplicates removed": {
			startList:    "node[1-128],node2,node4,node8,node16,node32,node64,node128",
			expRawOut:    "node[1-128,2,4,8,16,32,64,128]",
			expUniqOut:   "node[1-128]",
			expUniqCount: 128,
		},
		"compatible padding": {
			startList:    "node[001-9],node[010-20]",
			expRawOut:    "node[001-020]",
			expUniqOut:   "node[001-020]",
			expUniqCount: 20,
		},
		"incompatible padding": {
			startList:    "node[001-9],node[10-20]",
			expRawOut:    "node[001-009,10-20]",
			expUniqOut:   "node[10-20,001-009]",
			expUniqCount: 20,
		},
		"space delimited": {
			startList:    "node1 node2 node3",
			expRawOut:    "node[1-3]",
			expUniqOut:   "node[1-3]",
			expUniqCount: 3,
		},
		"tab delimited": {
			startList: "node1	node2	node3",
			expRawOut:    "node[1-3]",
			expUniqOut:   "node[1-3]",
			expUniqCount: 3,
		},
		"mixed delimiters": {
			startList: "node1	node2 node3,node4",
			expRawOut:    "node[1-4]",
			expUniqOut:   "node[1-4]",
			expUniqCount: 4,
		},
		"lo > hi": {
			startList: "node[5-4]",
			expErr:    errors.New("invalid range"),
		},
		"NaN-lo": {
			startList: "node[a-b]",
			expErr:    errors.New("invalid range"),
		},
		"NaN-hi": {
			startList: "node[0-b]",
			expErr:    errors.New("invalid range"),
		},
		"bad range": {
			startList: "node[0--1]",
			expErr:    errors.New("invalid range"),
		},
		"weird": {
			startList: "node[ab]",
			expErr:    errors.New("invalid range"),
		},
		"unclosed range": {
			startList: "node[0-1",
			expErr:    errors.New("invalid range"),
		},
		"no hostname": {
			startList: "[0-1,3-5]",
			expErr:    errors.New("invalid range"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			hl, gotErr := hostlist.Create(tc.startList)
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

func TestHostList_Push(t *testing.T) {
	for name, tc := range map[string]struct {
		startList    string
		pushList     string
		expRawOut    string
		expUniqOut   string
		expUniqCount int
		expErr       error
	}{
		"simple append": {
			startList:    "node[2-5]",
			pushList:     "node5,node6,node12,node[1-2]",
			expRawOut:    "node[2-5,5-6,12,1-2]",
			expUniqOut:   "node[1-6,12]",
			expUniqCount: 7,
		},
		"complex append (suffixes)": {
			startList:    "node2,node2-1.prefix2,node2-2.prefix2",
			pushList:     "node1-[2-5].prefix1,node2-[3-22].prefix2,node3",
			expRawOut:    "node2,node2-[1-2].prefix2,node1-[2-5].prefix1,node2-[3-22].prefix2,node3",
			expUniqOut:   "node[2-3],node1-[2-5].prefix1,node2-[1-22].prefix2",
			expUniqCount: 28,
		},
		"empty start": {
			pushList:     "node5,node6,node12,node[1-2]",
			expRawOut:    "node[5-6,12,1-2]",
			expUniqOut:   "node[1-2,5-6,12]",
			expUniqCount: 5,
		},
		"empty push": {
			startList:    "node[2-5]",
			expRawOut:    "node[2-5]",
			expUniqOut:   "node[2-5]",
			expUniqCount: 4,
		},
		"single host": {
			startList:    "node2",
			pushList:     "node3",
			expRawOut:    "node[2-3]",
			expUniqOut:   "node[2-3]",
			expUniqCount: 2,
		},
		"incompatible widths": {
			startList:    "node[0003-5]",
			pushList:     "node[1-2]",
			expRawOut:    "node[0003-0005,1-2]",
			expUniqOut:   "node[1-2,0003-0005]",
			expUniqCount: 5,
		},
	} {
		t.Run(name, func(t *testing.T) {
			hl, err := hostlist.Create(tc.startList)
			if err != nil {
				t.Fatal(err)
			}

			gotErr := hl.Push(tc.pushList)
			cmpErr(t, tc.expErr, gotErr)
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

func TestHostList_PushList(t *testing.T) {
	for name, tc := range map[string]struct {
		startList string
		pushList  *hostlist.HostList
		expOut    string
	}{
		"nil other": {
			startList: "node[2-5]",
			expOut:    "node[2-5]",
		},
		"empty start": {
			startList: "",
			pushList:  makeTestList(t, "node5,node6,node12,node[1-2]"),
			expOut:    "node[1-2,5-6,12]",
		},
		"empty other": {
			startList: "node[2-5]",
			pushList:  makeTestList(t, ""),
			expOut:    "node[2-5]",
		},
		"success": {
			startList: "node[2-5]",
			pushList:  makeTestList(t, "node5,node6,node12,node[1-2]"),
			expOut:    "node[1-6,12]",
		},
	} {
		t.Run(name, func(t *testing.T) {
			hl, err := hostlist.Create(tc.startList)
			if err != nil {
				t.Fatal(err)
			}

			hl.PushList(tc.pushList)
			hl.Uniq()
			cmpOut(t, tc.expOut, hl.String())
		})
	}
}

func TestHostList_ReplaceList(t *testing.T) {
	for name, tc := range map[string]struct {
		startList string
		pushList  *hostlist.HostList
		expOut    string
	}{
		"nil other": {
			startList: "node[2-5]",
			expOut:    "node[2-5]",
		},
		"empty start": {
			startList: "",
			pushList:  makeTestList(t, "node5,node6,node12,node[1-2]"),
			expOut:    "node[1-2,5-6,12]",
		},
		"empty other": {
			startList: "node[2-5]",
			pushList:  makeTestList(t, ""),
			expOut:    "",
		},
		"success": {
			startList: "node[2-5]",
			pushList:  makeTestList(t, "node5,node6,node12,node[1-2]"),
			expOut:    "node[1-2,5-6,12]",
		},
	} {
		t.Run(name, func(t *testing.T) {
			hl, err := hostlist.Create(tc.startList)
			if err != nil {
				t.Fatal(err)
			}

			hl.ReplaceList(tc.pushList)
			hl.Uniq()
			cmpOut(t, tc.expOut, hl.String())
		})
	}
}

func TestHostList_Etc(t *testing.T) {
	emptyList := makeStringRef("")
	defaultList := makeStringRef("node[1-2,4-8]")
	singleHostList := makeStringRef("node8")
	nonRangeHost := makeStringRef("quack")

	for name, tc := range map[string]struct {
		method    string
		startList *string
		expOut    string
		expCount  int
		expString string
		expErr    error
	}{
		"Pop (empty)": {
			method:    "Pop",
			startList: emptyList,
			expErr:    hostlist.ErrEmpty,
		},
		"Pop": {
			method:    "Pop",
			expOut:    "node8",
			expString: "node[1-2,4-7]",
			expCount:  6,
		},
		"Pop (single host)": {
			method:    "Pop",
			startList: singleHostList,
			expOut:    "node8",
			expCount:  0,
		},
		"Pop (single host, non-range)": {
			method:    "Pop",
			startList: nonRangeHost,
			expOut:    "quack",
			expCount:  0,
		},
		"Shift (empty)": {
			method:    "Shift",
			startList: emptyList,
			expErr:    hostlist.ErrEmpty,
		},
		"Shift": {
			method:    "Shift",
			expOut:    "node1",
			expString: "node[2,4-8]",
			expCount:  6,
		},
		"Shift (single host)": {
			method:    "Shift",
			startList: singleHostList,
			expOut:    "node8",
			expCount:  0,
		},
		"Shift (single host, non-range)": {
			method:    "Shift",
			startList: nonRangeHost,
			expOut:    "quack",
			expCount:  0,
		},
		"PopRange (empty)": {
			method:    "PopRange",
			startList: emptyList,
			expErr:    hostlist.ErrEmpty,
		},
		"PopRange": {
			method:    "PopRange",
			expOut:    "node[4-8]",
			expString: "node[1-2]",
			expCount:  2,
		},
		"PopRange (single host)": {
			method:    "PopRange",
			startList: singleHostList,
			expOut:    "node8",
			expCount:  0,
		},
		"PopRange (single host, non-range)": {
			method:    "PopRange",
			startList: nonRangeHost,
			expOut:    "quack",
			expCount:  0,
		},
		"ShiftRange (empty)": {
			method:    "ShiftRange",
			startList: emptyList,
			expErr:    hostlist.ErrEmpty,
		},
		"ShiftRange": {
			method:    "ShiftRange",
			expOut:    "node[1-2]",
			expString: "node[4-8]",
			expCount:  5,
		},
		"ShiftRange (single host)": {
			method:    "ShiftRange",
			startList: singleHostList,
			expOut:    "node8",
			expCount:  0,
		},
		"ShiftRange (single host, non-range)": {
			method:    "ShiftRange",
			startList: nonRangeHost,
			expOut:    "quack",
			expCount:  0,
		},
	} {
		t.Run(name, func(t *testing.T) {
			if tc.startList == nil {
				tc.startList = defaultList
			}

			hl, err := hostlist.Create(*tc.startList)
			if err != nil {
				t.Fatal(err)
			}

			var gotOut string
			var gotErr error
			val := reflect.ValueOf(hl)
			fn := val.MethodByName(tc.method)
			if fn.IsNil() {
				t.Fatalf("unable to resolve %q to method name", tc.method)
			}
			res := fn.Call([]reflect.Value{})
			if len(res) != 2 {
				t.Fatalf("unexpected result: %v", res)
			}
			gotOut = res[0].String()

			if !res[1].IsNil() {
				var ok bool
				if gotErr, ok = res[1].Interface().(error); !ok {
					t.Fatalf("%v result was not (string, error)", res)
				}
			}

			cmpErr(t, tc.expErr, gotErr)
			if err != nil {
				return
			}

			cmpOut(t, tc.expOut, gotOut)
			cmpOut(t, tc.expString, hl.String())

			gotCount := hl.Count()
			if gotCount != tc.expCount {
				t.Fatalf("\nexpected final count to be %d, got %d", tc.expCount, gotCount)
			}
		})
	}
}

func TestHostList_Nth(t *testing.T) {
	defaultList := makeStringRef("node[1-2,4-8],quack")

	for name, tc := range map[string]struct {
		startList *string
		n         int
		expOut    string
		expErr    error
	}{
		"empty list": {
			startList: makeStringRef(""),
			expErr:    hostlist.ErrEmpty,
		},
		"1st": {
			n:      0,
			expOut: "node1",
		},
		"middle": {
			n:      3,
			expOut: "node5",
		},
		"last": {
			n:      7,
			expOut: "quack",
		},
		"out of bounds": {
			n:      8,
			expErr: errors.New("< hostCount"),
		},
		"-1": {
			n:      -1,
			expErr: errors.New("< 0"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			if tc.startList == nil {
				tc.startList = defaultList
			}

			hl, err := hostlist.Create(*tc.startList)
			if err != nil {
				t.Fatal(err)
			}

			gotOut, gotErr := hl.Nth(tc.n)
			cmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			cmpOut(t, tc.expOut, gotOut)
		})
	}
}

func TestHostList_DeleteNth(t *testing.T) {
	defaultList := makeStringRef("node[1-2,4-8],quack")

	for name, tc := range map[string]struct {
		startList *string
		n         int
		expString string
		expCount  int
		expErr    error
	}{
		"empty list": {
			startList: makeStringRef(""),
			expErr:    hostlist.ErrEmpty,
		},
		"1st": {
			n:         0,
			expString: "node[2,4-8],quack",
			expCount:  7,
		},
		"middle": {
			n:         3,
			expString: "node[1-2,4,6-8],quack",
			expCount:  7,
		},
		"last": {
			n:         7,
			expString: "node[1-2,4-8]",
			expCount:  7,
		},
		"last empties range": {
			n:         2,
			startList: makeStringRef("node[1-2,4]"),
			expString: "node[1-2]",
			expCount:  2,
		},
		"out of bounds": {
			n:      8,
			expErr: errors.New("< hostCount"),
		},
		"-1": {
			n:      -1,
			expErr: errors.New("< 0"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			if tc.startList == nil {
				tc.startList = defaultList
			}

			hl, err := hostlist.Create(*tc.startList)
			if err != nil {
				t.Fatal(err)
			}

			gotErr := hl.DeleteNth(tc.n)
			cmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			cmpOut(t, tc.expString, hl.String())

			gotCount := hl.Count()
			if gotCount != tc.expCount {
				t.Fatalf("\nexpected final count to be %d, got %d", tc.expCount, gotCount)
			}
		})
	}
}

func TestHostList_DeleteHosts(t *testing.T) {
	defaultList := makeStringRef("node[1-2,4-8],quack")

	for name, tc := range map[string]struct {
		startList  *string
		deleteList string
		expString  string
		expDeleted int
		expCount   int
		expErr     error
	}{
		"empty list": {
			startList: makeStringRef(""),
			expErr:    hostlist.ErrEmpty,
		},
		"first host": {
			deleteList: "node1",
			expString:  "node[2,4-8],quack",
			expDeleted: 1,
			expCount:   7,
		},
		"first range": {
			deleteList: "node1,node2",
			expString:  "node[4-8],quack",
			expDeleted: 2,
			expCount:   6,
		},
		"last range": {
			deleteList: "node[4-8]",
			expString:  "node[1-2],quack",
			expDeleted: 5,
			expCount:   3,
		},
		"last host": {
			deleteList: "quack",
			expString:  "node[1-2,4-8]",
			expDeleted: 1,
			expCount:   7,
		},
		"everything": {
			deleteList: *defaultList,
			expDeleted: 8,
		},
		"nothing": {
			expString: *defaultList,
			expCount:  8,
		},
		"nonexistent host": {
			deleteList: "woof",
			expString:  *defaultList,
			expCount:   8,
		},
	} {
		t.Run(name, func(t *testing.T) {
			if tc.startList == nil {
				tc.startList = defaultList
			}

			hl, err := hostlist.Create(*tc.startList)
			if err != nil {
				t.Fatal(err)
			}

			delCount, gotErr := hl.Delete(tc.deleteList)
			cmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			cmpOut(t, tc.expString, hl.String())

			if delCount != tc.expDeleted {
				t.Fatalf("\nexpected %d hosts to be deleted, got %d", tc.expDeleted, delCount)
			}

			gotCount := hl.Count()
			if gotCount != tc.expCount {
				t.Fatalf("\nexpected final count to be %d, got %d", tc.expCount, gotCount)
			}
		})
	}
}

func TestHostList_Within(t *testing.T) {
	defaultList := makeStringRef("node[1-128]")

	for name, tc := range map[string]struct {
		startList *string
		checkList string
		expWithin bool
		expErr    error
	}{
		"empty list": {
			startList: makeStringRef(""),
			checkList: "node1",
		},
		"single node": {
			checkList: "node1",
			expWithin: true,
		},
		"range": {
			checkList: "node[1-8]",
			expWithin: true,
		},
		"range overflows": {
			checkList: "node[1-256]",
			expWithin: false,
		},
		"range overlaps": {
			checkList: "node[128-256]",
			expWithin: false,
		},
	} {
		t.Run(name, func(t *testing.T) {
			if tc.startList == nil {
				tc.startList = defaultList
			}

			hl, err := hostlist.Create(*tc.startList)
			if err != nil {
				t.Fatal(err)
			}

			gotWithin, gotErr := hl.Within(tc.checkList)
			cmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			if gotWithin != tc.expWithin {
				t.Fatalf("\nexpected Within(%q) to be %t; got %t", tc.checkList, tc.expWithin, gotWithin)
			}
		})
	}
}

func TestHostList_ZeroValue(t *testing.T) {
	zVal := &hostlist.HostList{}

	err := zVal.Push("host[1-8]")
	if err != nil {
		t.Fatal(err)
	}

	gotCount := zVal.Count()
	if gotCount != 8 {
		t.Fatalf("expected count to be 8, got %d", gotCount)
	}
}
