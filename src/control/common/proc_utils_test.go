//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package common

import (
	"fmt"
	"io/ioutil"
	"os"
	"strconv"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
)

func addProcEntry(t *testing.T, root string, pid int, name string) {
	if err := os.MkdirAll(root+"/"+strconv.Itoa(pid), 0755); err != nil {
		t.Fatal(err)
	}
	if err := ioutil.WriteFile(root+"/"+strconv.Itoa(pid)+"/status", []byte("Name: "+name), 0644); err != nil {
		t.Fatal(err)
	}
	if err := os.Symlink(name, root+"/"+strconv.Itoa(pid)+"/exe"); err != nil {
		t.Fatal(err)
	}
}

func makeProcTree(t *testing.T, numEntries int) string {
	t.Helper()

	procRoot := t.TempDir()
	for i := 0; i < numEntries; i++ {
		addProcEntry(t, procRoot, i, fmt.Sprintf("test-%d", i))
	}

	return procRoot
}

func Test_Common_checkDupeProcess(t *testing.T) {
	procRoot := makeProcTree(t, 5)
	addProcEntry(t, procRoot, 5, "test-1")

	for name, tc := range map[string]struct {
		procPid int
		wantErr bool
	}{
		"duplicate process": {
			procPid: 5,
			wantErr: true,
		},
		"non-duplicate process": {
			procPid: 2,
			wantErr: false,
		},
	} {
		t.Run(name, func(t *testing.T) {
			if err := checkDupeProcess(tc.procPid, procRoot); (err != nil) != tc.wantErr {
				t.Errorf("checkDupeProcess() = %v, wantErr %v", err, tc.wantErr)
			}
		})
	}
}

func Test_Common_getProcPids(t *testing.T) {
	procRoot := makeProcTree(t, 5)
	addProcEntry(t, procRoot, 5, "test-1")

	for name, tc := range map[string]struct {
		procName string
		expPids  []int
		expErr   error
		setup    func(*testing.T)
		cleanup  func(*testing.T)
	}{
		"dupe process name": {
			procName: "test-1",
			expPids:  []int{1, 5},
			expErr:   nil,
		},
		"unique process name": {
			procName: "test-2",
			expPids:  []int{2},
			expErr:   nil,
		},
		"bad status file": {
			procName: "bad-status",
			expErr:   errors.New("status file"),
			setup: func(t *testing.T) {
				addProcEntry(t, procRoot, 6, "bad-status")
				if err := os.Truncate(procRoot+"/6/status", 0); err != nil {
					t.Fatal(err)
				}
			},
			cleanup: func(t *testing.T) {
				os.RemoveAll(procRoot + "/6")
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			if tc.setup != nil {
				tc.setup(t)
			}
			if tc.cleanup != nil {
				defer tc.cleanup(t)
			}

			gotPids, gotErr := getProcPids(procRoot, tc.procName)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(gotPids, tc.expPids); diff != "" {
				t.Fatalf("getProcPids() mismatch (-want +got):\n%s", diff)
			}
		})
	}
}
