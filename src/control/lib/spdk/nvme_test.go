//
// (C) Copyright 2018-2022 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package spdk

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
)

var (
	sampleErr1 = errors.New("example error #1")
	sampleErr2 = errors.New("example error #2")
)

func TestSpdk_cleanLockfiles(t *testing.T) {
	testDir, clean := test.CreateTestDir(t)
	defer clean()

	for name, tc := range map[string]struct {
		pciAddrs          []string
		addrCheckMap      map[string]bool
		addrCheckErr      error
		removeErr         error
		expRemoveCalls    []string
		expNoFilesRemoved bool
		expErr            error
	}{
		"no pciAddrs": {
			addrCheckMap:   make(map[string]bool),
			expRemoveCalls: []string{},
		},
		"single pciAddr": {
			pciAddrs: []string{"0000:81:00.0"},
			addrCheckMap: map[string]bool{
				"0000:81:00.0": true,
			},
			expRemoveCalls: []string{
				filepath.Join(testDir, LockfilePrefix+"0000:81:00.0"),
			},
		},
		"multiple pciAddrs; partial selection": {
			pciAddrs: []string{"0000:81:00.0", "0000:82:00.0", "0000:83:00.0"},
			addrCheckMap: map[string]bool{
				"0000:81:00.0": false,
				"0000:82:00.0": true,
				"0000:83:00.0": true,
			},
			expRemoveCalls: []string{
				filepath.Join(testDir, LockfilePrefix+"0000:82:00.0"),
				filepath.Join(testDir, LockfilePrefix+"0000:83:00.0"),
			},
		},
		"error on remove": {
			pciAddrs: []string{"0000:81:00.0", "0000:82:00.0"},
			addrCheckMap: map[string]bool{
				"0000:81:00.0": true,
				"0000:82:00.0": true,
			},
			removeErr: sampleErr1,
			expRemoveCalls: []string{
				filepath.Join(testDir, LockfilePrefix+"0000:81:00.0"),
				filepath.Join(testDir, LockfilePrefix+"0000:82:00.0"),
			},
			expNoFilesRemoved: true,
			expErr: errors.Errorf("%s: %s: %s: %s",
				filepath.Join(testDir, LockfilePrefix+"0000:82:00.0"),
				sampleErr1.Error(),
				filepath.Join(testDir, LockfilePrefix+"0000:81:00.0"),
				sampleErr1.Error()),
		},
		"not-exist error on remove": {
			pciAddrs: []string{"0000:81:00.0", "0000:82:00.0"},
			addrCheckMap: map[string]bool{
				"0000:81:00.0": true,
				"0000:82:00.0": true,
			},
			removeErr: os.ErrNotExist,
			expRemoveCalls: []string{
				filepath.Join(testDir, LockfilePrefix+"0000:81:00.0"),
				filepath.Join(testDir, LockfilePrefix+"0000:82:00.0"),
			},
			expNoFilesRemoved: true,
		},
		"error on check": {
			pciAddrs:     []string{"0000:81:00.0", "0000:82:00.0"},
			addrCheckErr: sampleErr2,
			expErr: errors.Errorf("%s: %s: %s: %s",
				filepath.Join(testDir, LockfilePrefix+"0000:82:00.0"),
				sampleErr2.Error(),
				filepath.Join(testDir, LockfilePrefix+"0000:81:00.0"),
				sampleErr2.Error()),
			expRemoveCalls: []string{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			if err := test.RemoveContents(t, testDir); err != nil {
				t.Fatal(err)
			}

			removeCalls := []string{}

			mockAddrCheck := func(addr string) (bool, error) {
				return tc.addrCheckMap[addr], tc.addrCheckErr
			}

			mockRemove := func(name string) error {
				removeCalls = append(removeCalls, name)
				return tc.removeErr
			}

			// Create lockfiles in test directory.
			for _, addrStr := range tc.pciAddrs {
				fName := filepath.Join(testDir, LockfilePrefix+addrStr)
				if _, err := os.Create(fName); err != nil {
					t.Fatalf("error creating %s", fName)
				}
			}

			removedLocks, gotErr := cleanLockfiles(log, testDir, mockAddrCheck, mockRemove)
			test.CmpErr(t, tc.expErr, gotErr)

			if diff := cmp.Diff(tc.expRemoveCalls, removeCalls); diff != "" {
				t.Fatalf("unexpected list of remove calls (-want, +got): %s", diff)
			}
			expRemoved := tc.expRemoveCalls
			if tc.expNoFilesRemoved {
				expRemoved = []string{}
			}
			if diff := cmp.Diff(expRemoved, removedLocks); diff != "" {
				t.Fatalf("unexpected list of files removed (-want, +got): %s", diff)
			}
		})
	}
}

func TestSpdk_cleanKnownLockfiles(t *testing.T) {
	testDir, clean := test.CreateTestDir(t)
	defer clean()

	for name, tc := range map[string]struct {
		addrsToClean []string
		lfAddrsInDir []string
		expRemaining []string
		expErr       error
	}{
		"no lockflles; no addresses to clean": {
			expRemaining: []string{},
		},
		"no lockfiles": {
			addrsToClean: []string{
				"0000:81:00.0",
				"0000:82:00.0",
				"0000:83:00.0",
			},
			lfAddrsInDir: []string{},
			expRemaining: []string{},
		},
		"no addresses to clean": {
			lfAddrsInDir: []string{
				"0000:81:00.0",
				"0000:82:00.0",
				"0000:83:00.0",
			},
			addrsToClean: []string{},
			expRemaining: []string{
				LockfilePrefix + "0000:81:00.0",
				LockfilePrefix + "0000:82:00.0",
				LockfilePrefix + "0000:83:00.0",
			},
		},
		"clean some addresses": {
			lfAddrsInDir: []string{
				"0000:81:00.0",
				"0000:82:00.0",
				"0000:83:00.0",
			},
			addrsToClean: []string{
				"0000:82:00.0",
				"0000:83:00.0",
			},
			expRemaining: []string{
				LockfilePrefix + "0000:81:00.0",
			},
		},
		"clean all addresses": {
			lfAddrsInDir: []string{
				"0000:81:00.0",
				"0000:82:00.0",
				"0000:83:00.0",
			},
			addrsToClean: []string{
				"0000:84:00.0",
				"0000:82:00.0",
				"0000:83:00.0",
				"0000:81:00.0",
			},
			expRemaining: []string{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			if err := test.RemoveContents(t, testDir); err != nil {
				t.Fatal(err)
			}

			nvme := &NvmeImpl{
				LocksDir: testDir,
			}

			// Create lockfiles in test directory.
			for _, addrStr := range tc.lfAddrsInDir {
				fName := filepath.Join(testDir, LockfilePrefix+addrStr)
				t.Logf("creating %s", fName)
				if _, err := os.Create(fName); err != nil {
					t.Fatalf("error creating %s", fName)
				}
			}

			gotErr := cleanKnownLockfiles(log, nvme, tc.addrsToClean...)
			test.CmpErr(t, tc.expErr, gotErr)

			remaining, err := common.GetFilenames(testDir)
			if err != nil {
				t.Fatal(err)
			}
			if diff := cmp.Diff(tc.expRemaining, remaining); diff != "" {
				t.Fatalf("unexpected list of locks remaining (-want, +got): %s", diff)
			}
		})
	}
}

func TestSpdk_WrapCleanError(t *testing.T) {
	wrappedErr := errors.Wrap(sampleErr1, sampleErr2.Error())

	for name, tc := range map[string]struct {
		inErr     error
		cleanErr  error
		expOutErr error
	}{
		"no errors":              {nil, nil, nil},
		"clean error":            {nil, sampleErr1, sampleErr1},
		"outer error":            {sampleErr1, nil, sampleErr1},
		"outer and clean errors": {sampleErr1, sampleErr2, wrappedErr},
	} {
		t.Run(name, func(t *testing.T) {
			gotErr := wrapCleanError(tc.inErr, tc.cleanErr)
			test.CmpErr(t, tc.expOutErr, gotErr)
		})
	}
}
