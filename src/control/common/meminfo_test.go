//
// (C) Copyright 2019-2023 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package common

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
)

func TestCommon_getMemInfo(t *testing.T) {
	// Just a simple test to verify that we get something -- it should
	// pretty much never error.
	_, err := GetMemInfo()
	if err != nil {
		t.Fatal(err)
	}
}

func TestCommon_parseMemInfoT(t *testing.T) {
	for name, tc := range map[string]struct {
		input     string
		expOut    *MemInfoT
		expFreeMB int
		expErr    error
	}{
		"none parsed": {
			expOut: &MemInfoT{
				NumaNodeID: -1,
			},
			expFreeMB: 0,
		},
		"2MB pagesize": {
			input: `
MemTotal:           1024 kB
MemFree:            1024 kB
MemAvailable:       1024 kB
HugePages_Total:    1024
HugePages_Free:     1023
HugePages_Rsvd:        0
HugePages_Surp:        0
Hugepagesize:       2048 kB
			`,
			expOut: &MemInfoT{
				NumaNodeID:      -1,
				HugepagesTotal:  1024,
				HugepagesFree:   1023,
				HugepageSizeKiB: 2048,
				MemTotalKiB:     1024,
				MemFreeKiB:      1024,
				MemAvailableKiB: 1024,
			},
			expFreeMB: 2046,
		},
		"1GB pagesize": {
			input: `
HugePages_Total:      16
HugePages_Free:       16
HugePages_Rsvd:        0
HugePages_Surp:        0
Hugepagesize:       1048576 kB
			`,
			expOut: &MemInfoT{
				HugepagesTotal:  16,
				HugepagesFree:   16,
				HugepageSizeKiB: 1048576,
				NumaNodeID:      -1,
			},
			expFreeMB: 16384,
		},
		"weird pagesize": {
			input: `
Hugepagesize:       blerble 1 GB
			`,
			expErr: errors.New("unable to parse"),
		},
		"weird pagesize unit": {
			input: `
Hugepagesize:       1 GB
			`,
			expErr: errors.New("unhandled size unit \"GB\""),
		},
	} {
		t.Run(name, func(t *testing.T) {
			rdr := strings.NewReader(tc.input)

			gotOut, gotErr := parseMemInfoT(rdr)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expOut, gotOut); diff != "" {
				t.Fatalf("unexpected output (-want, +got)\n%s\n", diff)
			}

			mi := &MemInfo{MemInfoT: *gotOut}
			if mi.HugepagesFreeMB() != tc.expFreeMB {
				t.Fatalf("expected FreeMB() to be %d, got %d",
					tc.expFreeMB, mi.HugepagesFreeMB())
			}
		})
	}
}

func TestCommon_GetMemInfo(t *testing.T) {
	extTestDataDir := "./testdata"

	testDir, clean := test.CreateTestDir(t)
	defer clean()

	pathRootProcOrig := pathRootProc
	pathRootProc = filepath.Join(testDir, "proc")
	defer func() {
		pathRootProc = pathRootProcOrig
	}()
	if err := os.Mkdir(pathRootProc, 0750); err != nil {
		t.Fatal(err)
	}

	pathRootSysOrig := pathRootSys
	pathRootSys = filepath.Join(testDir, "sys")
	defer func() {
		pathRootSys = pathRootSysOrig
	}()

	for name, tc := range map[string]struct {
		// Mock file tree, map contains source file to destination dir locations.
		files  map[string]string
		expErr error
		expOut *MemInfo
	}{
		"no numa nodes": {
			files: map[string]string{
				"meminfo": pathRootProc,
			},
			expOut: &MemInfo{
				MemInfoT: MemInfoT{
					NumaNodeID:      -1,
					HugepagesTotal:  2560,
					HugepagesFree:   2560,
					HugepageSizeKiB: 2048,
					MemTotalKiB:     196703832,
					MemFreeKiB:      182740080,
					MemAvailableKiB: 184708468,
				},
				NumaNodes: []MemInfoT{},
			},
		},
		"dual numa nodes": {
			files: map[string]string{
				"meminfo": pathRootProc,
				"meminfo_0": filepath.Join(pathRootSys, "devices", "system", "node",
					"node0"),
				"meminfo_1": filepath.Join(pathRootSys, "devices", "system", "node",
					"node1"),
			},
			expOut: &MemInfo{
				MemInfoT: MemInfoT{
					NumaNodeID:      -1,
					HugepagesTotal:  2560,
					HugepagesFree:   2560,
					HugepageSizeKiB: 2048,
					MemTotalKiB:     196703832,
					MemFreeKiB:      182740080,
					MemAvailableKiB: 184708468,
				},
				NumaNodes: []MemInfoT{
					{
						HugepagesTotal: 2048,
						HugepagesFree:  2048,
						MemTotalKiB:    97673756,
						MemFreeKiB:     87210384,
						MemUsedKiB:     10463372,
					},
					{
						HugepagesTotal: 512,
						HugepagesFree:  512,
						MemTotalKiB:    99030076,
						MemFreeKiB:     95526628,
						MemUsedKiB:     3503448,
						NumaNodeID:     1,
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			if err := test.RemoveContents(t, pathRootProc); err != nil {
				t.Fatal(err)
			}
			if err := test.RemoveContents(t, pathRootSys); err != nil {
				t.Fatal(err)
			}

			for src, dst := range tc.files {
				srcPath := filepath.Join(extTestDataDir, src)
				if _, err := os.Stat(srcPath); err != nil {
					t.Fatal(err)
				}
				if err := os.MkdirAll(dst, 0750); err != nil {
					t.Fatal(err)
				}
				dstPath := filepath.Join(dst, "meminfo")
				test.CopyFile(t, srcPath, dstPath)
				if _, err := os.Stat(dstPath); err != nil {
					t.Fatal(err)
				}
			}

			gotOut, gotErr := GetMemInfo()
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expOut, gotOut); diff != "" {
				t.Fatalf("unexpected output (-want, +got)\n%s\n", diff)
			}
		})
	}
}
