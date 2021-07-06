//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
package hostlist_test

import (
	"strings"
	"testing"

	"github.com/daos-stack/daos/src/control/lib/hostlist"
)

func cmpErr(t *testing.T, want, got error) {
	t.Helper()

	if want == got {
		return
	}
	if want == nil || got == nil {
		t.Fatalf("\nunexpected error (wanted: %v, got: %v)", want, got)
	}
	if !strings.Contains(got.Error(), want.Error()) {
		t.Fatalf("\nunexpected error (wanted: %s, got: %s)", want, got)
	}
}

func cmpOut(t *testing.T, want, got string) {
	t.Helper()

	if got != want {
		t.Fatalf("\nexpected: %q,\ngot:      %q", want, got)
	}
}

func TestHostList_Expand(t *testing.T) {
	// Testcases based on tests defined in:
	// https://github.com/LLNL/py-hostlist/blob/master/hostlist/unittest_hostlist.py
	for input, tc := range map[string]struct {
		expOut string
		expErr error
	}{
		"quartz[4-8]": {
			expOut: "quartz4,quartz5,quartz6,quartz7,quartz8",
		},
		"node[18-19,1-16,21-22]": {
			expOut: `node1,node2,node3,node4,node5,node6,node7,node8,` +
				`node9,node10,node11,node12,node13,node14,node15,` +
				`node16,node18,node19,node21,node22`,
		},
		"node[4-8,12,16-20,22,24-26]": {
			expOut: `node4,node5,node6,node7,node8,node12,node16,node17,` +
				`node18,node19,node20,node22,node24,node25,node26`,
		},
		"machine2-[02-4]vm1": {
			expOut: "machine2-02vm1,machine2-03vm1,machine2-04vm1",
		},
		"machine2-[02-3]vm1, machine4-[0003-5].vml2": {
			expOut: `machine2-02vm1,machine2-03vm1,` +
				`machine4-0003.vml2,machine4-0004.vml2,machine4-0005.vml2`,
		},
		"machine2-[009-11]vm1": {
			expOut: "machine2-009vm1,machine2-010vm1,machine2-011vm1",
		},
		"node[1,2,3]": {
			expOut: "node1,node2,node3",
		},
		"huey,dewey,louie": {
			expOut: "dewey,huey,louie",
		},
		"10.5.1.[10-15]:10001,10.5.1.42:10001": {
			expOut: `10.5.1.10:10001,10.5.1.11:10001,10.5.1.12:10001,` +
				`10.5.1.13:10001,10.5.1.14:10001,10.5.1.15:10001,10.5.1.42:10001`,
		},
	} {
		t.Run(input, func(t *testing.T) {
			gotOut, gotErr := hostlist.Expand(input)
			cmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			cmpOut(t, tc.expOut, gotOut)
		})
	}
}

func TestHostList_Compress(t *testing.T) {
	// Testcases based on tests defined in:
	// https://github.com/LLNL/py-hostlist/blob/master/hostlist/unittest_hostlist.py
	for input, tc := range map[string]struct {
		expOut string
		expErr error
	}{
		"node1,node2,node3,node4": {
			expOut: "node[1-4]",
		},
		"node1,node2,node3,node4,node5,node7,node8,node10,node11,node12": {
			expOut: "node[1-5,7-8,10-12]",
		},
		"node2.suffix2.com,node1.suffix.com,node2.suffix.com,node3.suffix.com,node1.suffix2.com": {
			expOut: "node[1-3].suffix.com,node[1-2].suffix2.com",
		},
		"node1-1.suffix.com,node1-2.suffix.com,node1-3.suffix.com,node1-4.suffix2.com": {
			expOut: "node1-[1-3].suffix.com,node1-4.suffix2.com",
		},
		"huey,dewey,louie": {
			expOut: "dewey,huey,louie",
		},
		`10.5.1.10:10001,10.5.1.11:10001,10.5.1.12:10001,` +
			`10.5.1.13:10001,10.5.1.14:10001,10.5.1.15:10001,10.5.1.42:10001`: {
			expOut: "10.5.1.[10-15,42]:10001",
		},
	} {
		t.Run(input, func(t *testing.T) {
			gotOut, gotErr := hostlist.Compress(input)
			cmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			cmpOut(t, tc.expOut, gotOut)
		})
	}
}

func TestHostList_Count(t *testing.T) {
	for input, tc := range map[string]struct {
		expCount int
		expErr   error
	}{
		"": {
			expCount: 0,
		},
		"node[1-4]": {
			expCount: 4,
		},
		"node[1-4],node[1-4]": {
			expCount: 4, // unique hosts
		},
		"node1,node2,node3,node4,node5,node7,node8,node10,node11,node12": {
			expCount: 10,
		},
		"huey,dewey,louie": {
			expCount: 3,
		},
		`10.5.1.10:10001,10.5.1.11:10001,10.5.1.12:10001,10.5.1.13:10001,` +
			`10.5.1.14:10001,10.5.1.15:10001,10.5.1.42:10001`: {
			expCount: 7,
		},
	} {
		t.Run(input, func(t *testing.T) {
			gotCount, gotErr := hostlist.Count(input)
			cmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			if gotCount != tc.expCount {
				t.Fatalf("\nexpected count to be %d, got %d", tc.expCount, gotCount)
			}
		})
	}
}
