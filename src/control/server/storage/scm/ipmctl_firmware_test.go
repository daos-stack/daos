//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package scm

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/ipmctl"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

func TestIpmctl_fwInfoStatusToUpdateStatus(t *testing.T) {
	for name, tc := range map[string]struct {
		input     uint32
		expResult storage.ScmFirmwareUpdateStatus
	}{
		"unknown": {
			input:     ipmctl.FWUpdateStatusUnknown,
			expResult: storage.ScmUpdateStatusUnknown,
		},
		"success": {
			input:     ipmctl.FWUpdateStatusSuccess,
			expResult: storage.ScmUpdateStatusSuccess,
		},
		"failure": {
			input:     ipmctl.FWUpdateStatusFailed,
			expResult: storage.ScmUpdateStatusFailed,
		},
		"staged": {
			input:     ipmctl.FWUpdateStatusStaged,
			expResult: storage.ScmUpdateStatusStaged,
		},
		"out of range": {
			input:     uint32(500),
			expResult: storage.ScmUpdateStatusUnknown,
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := scmFirmwareUpdateStatusFromIpmctl(tc.input)

			test.AssertEqual(t, tc.expResult, result, "didn't match")
		})
	}
}

func TestIpmctl_GetFirmwareStatus(t *testing.T) {
	testUID := "TestUID"
	testActiveVersion := "1.0.0.1"
	testStagedVersion := "2.0.0.2"
	fwInfo := ipmctl.DeviceFirmwareInfo{
		FWImageMaxSize: 65,
		FWUpdateStatus: ipmctl.FWUpdateStatusStaged,
	}
	_ = copy(fwInfo.ActiveFWVersion[:], testActiveVersion)
	_ = copy(fwInfo.StagedFWVersion[:], testStagedVersion)

	// Representing a DIMM without a staged FW version
	fwInfoUnstaged := ipmctl.DeviceFirmwareInfo{
		FWImageMaxSize: 1,
		FWUpdateStatus: ipmctl.FWUpdateStatusSuccess,
	}
	_ = copy(fwInfoUnstaged.ActiveFWVersion[:], testActiveVersion)
	_ = copy(fwInfoUnstaged.StagedFWVersion[:], noFirmwareVersion)

	for name, tc := range map[string]struct {
		inputUID  string
		cfg       *mockIpmctlCfg
		expErr    error
		expResult *storage.ScmFirmwareInfo
	}{
		"empty deviceUID": {
			expErr: errors.New("invalid SCM module UID"),
		},
		"ipmctl.GetFirmwareInfo failed": {
			inputUID: testUID,
			cfg: &mockIpmctlCfg{
				getFWInfoRet: errors.New("mock GetFirmwareInfo failed"),
			},
			expErr: errors.Errorf("failed to get firmware info for device %q: mock GetFirmwareInfo failed", testUID),
		},
		"success": {
			inputUID: testUID,
			cfg: &mockIpmctlCfg{
				fwInfo: fwInfo,
			},
			expResult: &storage.ScmFirmwareInfo{
				ActiveVersion:     testActiveVersion,
				StagedVersion:     testStagedVersion,
				ImageMaxSizeBytes: fwInfo.FWImageMaxSize * 4096,
				UpdateStatus:      storage.ScmUpdateStatusStaged,
			},
		},
		"nothing staged": {
			inputUID: testUID,
			cfg: &mockIpmctlCfg{
				fwInfo: fwInfoUnstaged,
			},
			expResult: &storage.ScmFirmwareInfo{
				ActiveVersion:     testActiveVersion,
				ImageMaxSizeBytes: 4096,
				UpdateStatus:      storage.ScmUpdateStatusSuccess,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mockBinding := newMockIpmctl(tc.cfg)
			cr, err := newCmdRunner(log, mockBinding, nil, nil)
			if err != nil {
				t.Fatal(err)
			}

			result, err := cr.GetFirmwareStatus(tc.inputUID)

			test.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResult, result); diff != "" {
				t.Errorf("wrong firmware info (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestIpmctl_UpdateFirmware(t *testing.T) {
	testUID := "testUID"
	for name, tc := range map[string]struct {
		inputUID string
		cfg      *mockIpmctlCfg
		expErr   error
	}{
		"bad UID": {
			cfg:    &mockIpmctlCfg{},
			expErr: errors.New("invalid SCM module UID"),
		},
		"success": {
			inputUID: testUID,
			cfg:      &mockIpmctlCfg{},
		},
		"ipmctl UpdateFirmware failed": {
			inputUID: testUID,
			cfg: &mockIpmctlCfg{
				updateFirmwareRet: errors.New("mock UpdateFirmware failed"),
			},
			expErr: errors.Errorf("failed to update firmware for device %q: mock UpdateFirmware failed", testUID),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mockBinding := newMockIpmctl(tc.cfg)
			cr, err := newCmdRunner(log, mockBinding, nil, nil)
			if err != nil {
				t.Fatal(err)
			}

			err = cr.UpdateFirmware(tc.inputUID, "/dont/care")
			test.CmpErr(t, tc.expErr, err)
		})
	}
}
