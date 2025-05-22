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

func TestCommon_parseMemInfo(t *testing.T) {
	for name, tc := range map[string]struct {
		input     string
		expOut    *MemInfo
		expFreeMB int
		expErr    error
	}{
		"none parsed": {
			expOut: &MemInfo{
				NumaNodeIndex: -1,
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
			expOut: &MemInfo{
				NumaNodeIndex:   -1,
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
			expOut: &MemInfo{
				NumaNodeIndex:   -1,
				HugepagesTotal:  16,
				HugepagesFree:   16,
				HugepageSizeKiB: 1048576,
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

			gotOut, gotErr := parseMemInfo(rdr)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expOut, gotOut); diff != "" {
				t.Fatalf("unexpected output (-want, +got)\n%s\n", diff)
			}

			mi := &SysMemInfo{MemInfo: *gotOut}
			if mi.HugepagesFreeMB() != tc.expFreeMB {
				t.Fatalf("expected FreeMB() to be %d, got %d",
					tc.expFreeMB, mi.HugepagesFreeMB())
			}
		})
	}
}

func createMemInfoTestFile(t *testing.T, extDataDir, srcName, dstDir string) {
	srcPath := filepath.Join(extDataDir, srcName)
	if _, err := os.Stat(srcPath); err != nil {
		t.Fatal(err)
	}
	if err := os.MkdirAll(dstDir, 0750); err != nil {
		t.Fatal(err)
	}
	dstPath := filepath.Join(dstDir, "meminfo")
	test.CopyFile(t, srcPath, dstPath)
	if _, err := os.Stat(dstPath); err != nil {
		t.Fatal(err)
	}
}

func TestCommon_getMemInfoNodes(t *testing.T) {
	extTestDataDir := "./testdata"

	testDir, clean := test.CreateTestDir(t)
	defer clean()

	pathRootSysOrig := pathRootSys
	pathRootSys = filepath.Join(testDir, "sys")
	defer func() {
		pathRootSys = pathRootSysOrig
	}()

	for name, tc := range map[string]struct {
		skipCreate bool
		expNodes   []MemInfo
		expErr     error
	}{
		"zero nodes found": {
			skipCreate: true,
			expNodes:   []MemInfo{},
		},
		"two nodes found": {
			expNodes: []MemInfo{
				{
					HugepagesTotal: 2048,
					HugepagesFree:  2048,
					MemTotalKiB:    97673756,
					MemFreeKiB:     87210384,
					MemUsedKiB:     10463372,
				},
				{
					NumaNodeIndex:  1,
					HugepagesTotal: 512,
					HugepagesFree:  512,
					MemTotalKiB:    99030076,
					MemFreeKiB:     95526628,
					MemUsedKiB:     3503448,
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			if !tc.skipCreate {
				createMemInfoTestFile(t, extTestDataDir, "meminfo_0",
					filepath.Join(pathRootSys, "devices", "system", "node",
						"node0"))
				createMemInfoTestFile(t, extTestDataDir, "meminfo_1",
					filepath.Join(pathRootSys, "devices", "system", "node",
						"node1"))
			}

			gotNodes, gotErr := getMemInfoNodes()
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expNodes, gotNodes); diff != "" {
				t.Fatalf("unexpected output (-want, +got)\n%s\n", diff)
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
		expOut *SysMemInfo
	}{
		"no numa nodes": {
			files: map[string]string{
				"meminfo": pathRootProc,
			},
			expOut: &SysMemInfo{
				MemInfo: MemInfo{
					NumaNodeIndex:   -1,
					HugepagesTotal:  2560,
					HugepagesFree:   2560,
					HugepageSizeKiB: 2048,
					MemTotalKiB:     196703832,
					MemFreeKiB:      182740080,
					MemAvailableKiB: 184708468,
				},
				NumaNodes: []MemInfo{},
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
			expOut: &SysMemInfo{
				MemInfo: MemInfo{
					NumaNodeIndex:   -1,
					HugepagesTotal:  2560,
					HugepagesFree:   2560,
					HugepageSizeKiB: 2048,
					MemTotalKiB:     196703832,
					MemFreeKiB:      182740080,
					MemAvailableKiB: 184708468,
				},
				NumaNodes: []MemInfo{
					{
						HugepagesTotal: 2048,
						HugepagesFree:  2048,
						MemTotalKiB:    97673756,
						MemFreeKiB:     87210384,
						MemUsedKiB:     10463372,
					},
					{
						NumaNodeIndex:  1,
						HugepagesTotal: 512,
						HugepagesFree:  512,
						MemTotalKiB:    99030076,
						MemFreeKiB:     95526628,
						MemUsedKiB:     3503448,
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

			for srcName, dstDir := range tc.files {
				createMemInfoTestFile(t, extTestDataDir, srcName, dstDir)
			}

			gotOut, gotErr := GetSysMemInfo()
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
