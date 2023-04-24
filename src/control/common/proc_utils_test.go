//
// (C) Copyright 2022-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package common

import (
	"fmt"
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
	if err := os.WriteFile(root+"/"+strconv.Itoa(pid)+"/cmdline", []byte(fmt.Sprintf("/random/path/%s\x00-o\x00/foo/bar\x00", name)), 0644); err != nil {
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

func Test_Common_getProcName(t *testing.T) {
	procRoot := makeProcTree(t, 5)

	for name, tc := range map[string]struct {
		procPid int
		expName string
		expErr  error
	}{
		"valid process": {
			procPid: 2,
			expName: "test-2",
		},
		"invalid process": {
			procPid: 42,
			expErr:  errors.New("failed to read"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotName, gotErr := getProcName(tc.procPid, procRoot)
			test.CmpErr(t, tc.expErr, gotErr)
			if diff := cmp.Diff(tc.expName, gotName); diff != "" {
				t.Fatalf("unexpected process name (-want, +got):\n%s", diff)
			}
		})
	}
}

func Test_Common_getProcPids(t *testing.T) {
	procRoot := makeProcTree(t, 5)
	addProcEntry(t, procRoot, 5, "test-1")
	addProcEntry(t, procRoot, 6, "very-long-name-that-is-longer-than-15")
	addProcEntry(t, procRoot, 42, "weird but valid")

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
		},
		"unique process name": {
			procName: "test-2",
			expPids:  []int{2},
		},
		"long process name": {
			procName: "very-long-name-that-is-longer-than-15",
			expPids:  []int{6},
		},
		"space in process name": {
			procName: "weird but valid",
			expPids:  []int{42},
		},
		"unreadable cmdline file is skipped": {
			setup: func(t *testing.T) {
				addProcEntry(t, procRoot, 7, "missing")
				os.Chmod(procRoot+"/7/cmdline", 0000)
			},
			cleanup: func(t *testing.T) {
				os.RemoveAll(procRoot + "/7")
			},
		},
		"empty cmdline file is skipped": {
			procName: "empty",
			setup: func(t *testing.T) {
				addProcEntry(t, procRoot, 7, "empty")
				if err := os.Truncate(procRoot+"/7/cmdline", 0); err != nil {
					t.Fatal(err)
				}
			},
			cleanup: func(t *testing.T) {
				os.RemoveAll(procRoot + "/7")
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

			if diff := cmp.Diff(tc.expPids, gotPids); diff != "" {
				t.Fatalf("getProcPids() mismatch (-want +got):\n%s", diff)
			}
		})
	}
}
