//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package scm

import (
	"fmt"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto"
	"github.com/daos-stack/daos/src/control/lib/ipmctl"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// MockDiscovery returns a mock SCM module of type exported from ipmctl.
func MockDiscovery() ipmctl.DeviceDiscovery {
	m := proto.MockScmModule()

	result := ipmctl.DeviceDiscovery{
		Physical_id:          uint16(m.Physicalid),
		Channel_id:           uint16(m.Channelid),
		Channel_pos:          uint16(m.Channelposition),
		Memory_controller_id: uint16(m.Controllerid),
		Socket_id:            uint16(m.Socketid),
		Capacity:             m.Capacity,
	}

	_ = copy(result.Uid[:], m.Uid)
	_ = copy(result.Part_number[:], m.PartNumber)
	_ = copy(result.Fw_revision[:], m.FirmwareRevision)

	return result
}

// MockModule converts ipmctl type SCM module and returns storage/scm
// internal type.
func MockModule(d *ipmctl.DeviceDiscovery) storage.ScmModule {
	if d == nil {
		md := MockDiscovery()
		d = &md
	}

	return storage.ScmModule{
		PhysicalID:       uint32(d.Physical_id),
		ChannelID:        uint32(d.Channel_id),
		ChannelPosition:  uint32(d.Channel_pos),
		ControllerID:     uint32(d.Memory_controller_id),
		SocketID:         uint32(d.Socket_id),
		Capacity:         d.Capacity,
		UID:              d.Uid.String(),
		PartNumber:       d.Part_number.String(),
		FirmwareRevision: d.Fw_revision.String(),
	}
}

type (
	mockIpmctlCfg struct {
		getModulesErr     error
		modules           []ipmctl.DeviceDiscovery
		getRegionsErr     error
		regions           []ipmctl.PMemRegion
		getFWInfoRet      error
		fwInfo            ipmctl.DeviceFirmwareInfo
		updateFirmwareRet error
	}

	mockIpmctl struct {
		cfg mockIpmctlCfg
	}
)

func (m *mockIpmctl) GetModules() ([]ipmctl.DeviceDiscovery, error) {
	return m.cfg.modules, m.cfg.getModulesErr
}

func (m *mockIpmctl) GetRegions() ([]ipmctl.PMemRegion, error) {
	return m.cfg.regions, m.cfg.getRegionsErr
}

func (m *mockIpmctl) GetFirmwareInfo(uid ipmctl.DeviceUID) (ipmctl.DeviceFirmwareInfo, error) {
	return m.cfg.fwInfo, m.cfg.getFWInfoRet
}

func (m *mockIpmctl) UpdateFirmware(uid ipmctl.DeviceUID, fwPath string, force bool) error {
	return m.cfg.updateFirmwareRet
}

func newMockIpmctl(cfg *mockIpmctlCfg) *mockIpmctl {
	if cfg == nil {
		cfg = &mockIpmctlCfg{}
	}

	return &mockIpmctl{
		cfg: *cfg,
	}
}

// TestIpmctl_checkIpmctl verified that bad versions trigger an error.
func TestIpmctl_checkIpmctl(t *testing.T) {
	preTxt := "Intel(R) Optane(TM) Persistent Memory Command Line Interface Version "

	for name, tc := range map[string]struct {
		verOut  string
		badVers []semVer
		expErr  error
	}{
		"no bad versions": {
			verOut:  "02.00.00.3816",
			badVers: []semVer{},
		},
		"good version": {
			verOut:  "02.00.00.3825",
			badVers: badIpmctlVers,
		},
		"bad version": {
			verOut:  "02.00.00.3816",
			badVers: badIpmctlVers,
			expErr:  FaultIpmctlBadVersion("02.00.00.3816"),
		},
		"no version": {
			expErr: errors.New("could not read ipmctl version"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			mockRun := func(_ string) (string, error) {
				return preTxt + tc.verOut, nil
			}

			cr := newCmdRunner(log, nil, mockRun, nil)
			common.CmpErr(t, tc.expErr, cr.checkIpmctl(tc.badVers))
		})
	}
}

// TestIpmctl_getState tests the internals of GetState and verifies correct behaviour based
// on different output from GetRegions() bindings call.
func TestIpmctl_getState(t *testing.T) {
	for name, tc := range map[string]struct {
		ipmctlCfg *mockIpmctlCfg
		expErr    error
		expState  storage.ScmState
	}{
		"get regions fails": {
			ipmctlCfg: &mockIpmctlCfg{
				getRegionsErr: errors.New("fail"),
			},
			expErr: errors.New("fail"),
		},
		"modules but no regions": {
			ipmctlCfg: &mockIpmctlCfg{
				regions: []ipmctl.PMemRegion{},
			},
			expState: storage.ScmStateNoRegions,
		},
		"single region with unknown type": {
			ipmctlCfg: &mockIpmctlCfg{
				regions: []ipmctl.PMemRegion{
					{Free_capacity: 111111},
				},
			},
			expErr: errors.New("unexpected PMem region type"),
		},
		"single region with not interleaved type": {
			ipmctlCfg: &mockIpmctlCfg{
				regions: []ipmctl.PMemRegion{
					{
						Free_capacity: 111111,
						Type:          uint32(ipmctl.RegionTypeNotInterleaved),
					},
				},
			},
			expState: storage.ScmStateNotInterleaved,
		},
		"single region with free capacity": {
			ipmctlCfg: &mockIpmctlCfg{
				regions: []ipmctl.PMemRegion{
					{
						Free_capacity: 111111,
						Type:          uint32(ipmctl.RegionTypeAppDirect),
					},
				},
			},
			expState: storage.ScmStateFreeCapacity,
		},
		"regions with free capacity": {
			ipmctlCfg: &mockIpmctlCfg{
				regions: []ipmctl.PMemRegion{
					{Type: uint32(ipmctl.RegionTypeAppDirect)},
					{
						Free_capacity: 111111,
						Type:          uint32(ipmctl.RegionTypeAppDirect),
					},
				},
			},
			expState: storage.ScmStateFreeCapacity,
		},
		"regions with no capacity": {
			ipmctlCfg: &mockIpmctlCfg{
				regions: []ipmctl.PMemRegion{
					{Type: uint32(ipmctl.RegionTypeAppDirect)},
					{Type: uint32(ipmctl.RegionTypeAppDirect)},
				},
			},
			expState: storage.ScmStateNoFreeCapacity,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			mockLookPath := func(string) (string, error) {
				return "", nil
			}

			mockRun := func(string) (string, error) {
				return "", nil
			}

			mockBinding := newMockIpmctl(tc.ipmctlCfg)
			cr := newCmdRunner(log, mockBinding, mockRun, mockLookPath)

			scmState, err := cr.getState()
			common.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expState, scmState); diff != "" {
				t.Fatalf("unexpected scm state (-want, +got):\n%s\n", diff)
			}
		})
	}
}

// TestIpTestIpmctl_parseNamespaces verified expected output from ndctl utility
// can be converted into native storage ScmNamespaces type.
func TestIpmctl_parseNamespaces(t *testing.T) {
	// template for `ndctl list -N` output
	listTmpl := `{
   "dev":"namespace%d.0",
   "mode":"fsdax",
   "map":"dev",
   "size":3183575302144,
   "uuid":"842fc847-28e0-4bb6-8dfc-d24afdba1528",
   "raw_uuid":"dedb4b28-dc4b-4ccd-b7d1-9bd475c91264",
   "sector_size":512,
   "blockdev":"pmem%d",
   "numa_node":%d
}`

	for name, tc := range map[string]struct {
		in            string
		expNamespaces storage.ScmNamespaces
		expErr        error
	}{
		"empty": {
			expNamespaces: storage.ScmNamespaces{},
		},
		"single": {
			in: fmt.Sprintf(listTmpl, 0, 0, 0),
			expNamespaces: storage.ScmNamespaces{
				{
					Name:        "namespace0.0",
					BlockDevice: "pmem0",
					NumaNode:    0,
					Size:        3183575302144,
					UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
				},
			},
		},
		"double": {
			in: strings.Join([]string{
				"[", fmt.Sprintf(listTmpl, 0, 0, 0), ",",
				fmt.Sprintf(listTmpl, 1, 1, 1), "]"}, ""),
			expNamespaces: storage.ScmNamespaces{
				{
					Name:        "namespace0.0",
					BlockDevice: "pmem0",
					NumaNode:    0,
					Size:        3183575302144,
					UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
				},
				{
					Name:        "namespace1.0",
					BlockDevice: "pmem1",
					NumaNode:    1,
					Size:        3183575302144,
					UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
				},
			},
		},
		"malformed": {
			in:     `{"dev":"foo`,
			expErr: errors.New("JSON input"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotNamespaces, gotErr := parseNamespaces(tc.in)

			common.CmpErr(t, tc.expErr, gotErr)
			if diff := cmp.Diff(tc.expNamespaces, gotNamespaces); diff != "" {
				t.Fatalf("unexpected namespace result (-want, +got):\n%s\n", diff)
			}
		})
	}
}

// TestIpmctl_getNamespaces tests the internals of prepScm, pass in mock runCmd to verify
// behavior. Don't use mockPrepScm as we want to test prepScm logic.
func TestIpmctl_getNamespaces(t *testing.T) {
	commands := []string{} // external commands issued
	// ndctl create-namespace command return json format
	nsOut := `{
   "dev":"namespace%d.0",
   "mode":"fsdax",
   "map":"dev",
   "size":3183575302144,
   "uuid":"842fc847-28e0-4bb6-8dfc-d24afdba1528",
   "raw_uuid":"dedb4b28-dc4b-4ccd-b7d1-9bd475c91264",
   "sector_size":512,
   "blockdev":"pmem%d",
   "numa_node":%d
}
`
	oneNs, _ := parseNamespaces(fmt.Sprintf(nsOut, 1, 1, 0))
	twoNsJSON := "[" + fmt.Sprintf(nsOut, 1, 1, 0) + "," + fmt.Sprintf(nsOut, 2, 2, 1) + "]"
	twoNs, _ := parseNamespaces(twoNsJSON)

	tests := []struct {
		desc           string
		expErrMsg      string
		cmdOut         string
		expNamespaces  storage.ScmNamespaces
		expCommands    []string
		lookPathErrMsg string
	}{
		{
			desc:          "no namespaces",
			cmdOut:        "",
			expCommands:   []string{cmdListNamespaces},
			expNamespaces: storage.ScmNamespaces{},
		},
		{
			desc:          "single pmem device",
			cmdOut:        fmt.Sprintf(nsOut, 1, 1, 0),
			expCommands:   []string{cmdListNamespaces},
			expNamespaces: oneNs,
		},
		{
			desc:          "two pmem device",
			cmdOut:        twoNsJSON,
			expCommands:   []string{cmdListNamespaces},
			expNamespaces: twoNs,
		},
		{
			desc:           "ndctl not installed",
			lookPathErrMsg: FaultMissingNdctl.Error(),
			expErrMsg:      FaultMissingNdctl.Error(),
		},
	}

	for _, tt := range tests {
		t.Run(tt.desc, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			mockLookPath := func(string) (s string, err error) {
				if tt.lookPathErrMsg != "" {
					err = errors.New(tt.lookPathErrMsg)
				}
				return
			}

			mockRun := func(in string) (string, error) {
				commands = append(commands, in)
				return tt.cmdOut, nil
			}

			commands = nil // reset to initial values between tests

			mockBinding := newMockIpmctl(&mockIpmctlCfg{
				getModulesErr: nil,
				modules:       []ipmctl.DeviceDiscovery{MockDiscovery()},
			})
			cr := newCmdRunner(log, mockBinding, mockRun, mockLookPath)

			if _, err := cr.getModules(); err != nil {
				t.Fatal(err)
			}

			namespaces, err := cr.getNamespaces()
			if err != nil {
				if tt.lookPathErrMsg != "" {
					common.ExpectError(t, err, tt.lookPathErrMsg, tt.desc)
					return
				}
				t.Fatal(tt.desc + ": GetPmemNamespaces: " + err.Error())
			}

			common.AssertEqual(t, commands, tt.expCommands, tt.desc+": unexpected list of commands run")
			common.AssertEqual(t, namespaces, tt.expNamespaces, tt.desc+": unexpected list of pmem device file names")
		})
	}
}

func TestIpmctl_getModules(t *testing.T) {
	testDevices := []ipmctl.DeviceDiscovery{
		MockDiscovery(),
		MockDiscovery(),
		MockDiscovery(),
	}

	expModules := storage.ScmModules{}
	for _, dev := range testDevices {
		mod := MockModule(&dev)
		expModules = append(expModules, &mod)
	}

	for name, tc := range map[string]struct {
		cfg       *mockIpmctlCfg
		expErr    error
		expResult storage.ScmModules
	}{
		"ipmctl GetModules failed": {
			cfg: &mockIpmctlCfg{
				getModulesErr: errors.New("mock GetModules"),
			},
			expErr: errors.New("failed to discover SCM modules: mock GetModules"),
		},
		"no modules": {
			cfg:       &mockIpmctlCfg{},
			expResult: storage.ScmModules{},
		},
		"success with modules": {
			cfg: &mockIpmctlCfg{
				modules: testDevices,
			},
			expResult: expModules,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			mockBinding := newMockIpmctl(tc.cfg)
			cr := newCmdRunner(log, mockBinding, nil, nil)

			result, err := cr.getModules()

			common.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResult, result); diff != "" {
				t.Errorf("wrong firmware info (-want, +got):\n%s\n", diff)
			}
		})
	}
}

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

			common.AssertEqual(t, tc.expResult, result, "didn't match")
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
			defer common.ShowBufferOnFailure(t, buf)

			mockBinding := newMockIpmctl(tc.cfg)
			cr := newCmdRunner(log, mockBinding, nil, nil)

			result, err := cr.GetFirmwareStatus(tc.inputUID)

			common.CmpErr(t, tc.expErr, err)
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
			defer common.ShowBufferOnFailure(t, buf)

			mockBinding := newMockIpmctl(tc.cfg)
			cr := newCmdRunner(log, mockBinding, nil, nil)

			err := cr.UpdateFirmware(tc.inputUID, "/dont/care")

			common.CmpErr(t, tc.expErr, err)
		})
	}
}
