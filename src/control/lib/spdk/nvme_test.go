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

	"github.com/daos-stack/daos/src/control/common/test"
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
			addrCheckMap: make(map[string]bool),
		},
		"single pciAddr": {
			pciAddrs: []string{"0000:81:00.0"},
			addrCheckMap: map[string]bool{
				"0000:81:00.0": true,
			},
			expRemoveCalls: []string{
				filepath.Join(testDir, lockfilePrefix+"0000:81:00.0"),
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
				filepath.Join(testDir, lockfilePrefix+"0000:82:00.0"),
				filepath.Join(testDir, lockfilePrefix+"0000:83:00.0"),
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
				filepath.Join(testDir, lockfilePrefix+"0000:81:00.0"),
				filepath.Join(testDir, lockfilePrefix+"0000:82:00.0"),
			},
			expErr: errors.Errorf("%s: %s: %s: %s",
				filepath.Join(testDir, lockfilePrefix+"0000:82:00.0"),
				sampleErr1.Error(),
				filepath.Join(testDir, lockfilePrefix+"0000:81:00.0"),
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
				filepath.Join(testDir, lockfilePrefix+"0000:81:00.0"),
				filepath.Join(testDir, lockfilePrefix+"0000:82:00.0"),
			},
			expNoFilesRemoved: true,
		},
		"error on check": {
			pciAddrs:     []string{"0000:81:00.0", "0000:82:00.0"},
			addrCheckErr: sampleErr2,
			expErr: errors.Errorf("%s: %s: %s: %s",
				filepath.Join(testDir, lockfilePrefix+"0000:82:00.0"),
				sampleErr2.Error(),
				filepath.Join(testDir, lockfilePrefix+"0000:81:00.0"),
				sampleErr2.Error()),
		},
	} {
		t.Run(name, func(t *testing.T) {
			var removeCalls []string

			mockAddrCheck := func(addr string) (bool, error) {
				return tc.addrCheckMap[addr], tc.addrCheckErr
			}

			mockRemove := func(name string) error {
				removeCalls = append(removeCalls, name)
				return tc.removeErr
			}

			// Create lockfiles in test directory.
			for _, addrStr := range tc.pciAddrs {
				fName := filepath.Join(testDir, lockfilePrefix+addrStr)
				if _, err := os.Create(fName); err != nil {
					t.Fatalf("error creating %s", fName)
				}
			}

			removedLocks, gotErr := cleanLockfiles(testDir, mockAddrCheck, mockRemove)
			test.CmpErr(t, tc.expErr, gotErr)

			if diff := cmp.Diff(tc.expRemoveCalls, removeCalls); diff != "" {
				t.Fatalf("unexpected list of remove calls (-want, +got): %s", diff)
			}
			expRemoved := tc.expRemoveCalls
			if tc.expNoFilesRemoved {
				expRemoved = nil
			}
			if diff := cmp.Diff(expRemoved, removedLocks); diff != "" {
				t.Fatalf("unexpected list of files removed (-want, +got): %s", diff)
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
