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

package scm

import (
	"fmt"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	. "github.com/daos-stack/daos/src/control/common"
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
		discoverModulesRet error
		modules            []ipmctl.DeviceDiscovery
		getFWInfoRet       error
		fwInfo             ipmctl.DeviceFirmwareInfo
		updateFirmwareRet  error
	}

	mockIpmctl struct {
		cfg mockIpmctlCfg
	}
)

func (m *mockIpmctl) Discover() ([]ipmctl.DeviceDiscovery, error) {
	return m.cfg.modules, m.cfg.discoverModulesRet
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

func defaultMockIpmctl() *mockIpmctl {
	return newMockIpmctl(nil)
}

// TestGetState tests the internals of ipmCtlRunner, pass in mock runCmd to verify
// behavior. Don't use mockPrepScm as we want to test prepScm logic.
func TestGetState(t *testing.T) {
	var regionsOut string  // variable cmd output
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
	twoNsJson := "[" + fmt.Sprintf(nsOut, 1, 1, 0) + "," + fmt.Sprintf(nsOut, 2, 2, 1) + "]"
	twoNs, _ := parseNamespaces(twoNsJson)
	createRegionsOut := "hooray it worked\n"
	pmemId := 1

	mockRun := func(in string) (string, error) {
		retString := in

		switch in {
		case cmdScmCreateRegions:
			retString = createRegionsOut // example successful output
		case cmdScmShowRegions:
			retString = regionsOut
		case cmdScmCreateNamespace:
			// stimulate free capacity of region being used
			regionsOut = strings.Replace(regionsOut, "3012.0", "0.0", 1)
			retString = fmt.Sprintf(nsOut, pmemId, pmemId, pmemId-1)
			pmemId += 1
		case cmdScmListNamespaces:
			retString = twoNsJson
		}

		commands = append(commands, in)
		return retString, nil
	}

	tests := []struct {
		desc              string
		showRegionOut     string
		expGetStateErrMsg string
		expErrMsg         string
		expRebootRequired bool
		expNamespaces     storage.ScmNamespaces
		expCommands       []string
		lookPathErrMsg    string
	}{
		{
			desc:              "modules but no regions",
			showRegionOut:     outScmNoRegions,
			expRebootRequired: true,
			expCommands:       []string{cmdScmShowRegions, cmdScmDeleteGoal, cmdScmCreateRegions},
		},
		{
			desc: "single region with free capacity",
			showRegionOut: "\n" +
				"---ISetID=0x2aba7f4828ef2ccc---\n" +
				"   PersistentMemoryType=AppDirect\n" +
				"   FreeCapacity=0.0 GiB\n" +
				"---ISetID=0x81187f4881f02ccc---\n" +
				"   PersistentMemoryType=AppDirect\n" +
				"   FreeCapacity=3012.0 GiB\n" +
				"\n",
			expCommands:   []string{cmdScmShowRegions, cmdScmCreateNamespace, cmdScmShowRegions},
			expNamespaces: oneNs,
		},
		{
			desc: "regions with free capacity",
			showRegionOut: "\n" +
				"---ISetID=0x2aba7f4828ef2ccc---\n" +
				"   PersistentMemoryType=AppDirect\n" +
				"   FreeCapacity=3012.0 GiB\n" +
				"---ISetID=0x81187f4881f02ccc---\n" +
				"   PersistentMemoryType=AppDirect\n" +
				"   FreeCapacity=3012.0 GiB\n" +
				"\n",
			expCommands: []string{
				cmdScmShowRegions, cmdScmCreateNamespace, cmdScmShowRegions,
				cmdScmCreateNamespace, cmdScmShowRegions,
			},
			expNamespaces: twoNs,
		},
		{
			desc: "regions with no capacity",
			showRegionOut: "\n" +
				"---ISetID=0x2aba7f4828ef2ccc---\n" +
				"   PersistentMemoryType=AppDirect\n" +
				"   FreeCapacity=0.0 GiB\n" +
				"---ISetID=0x81187f4881f02ccb---\n" +
				"   PersistentMemoryType=AppDirect\n" +
				"   FreeCapacity=0.0 GiB\n" +
				"\n",
			expCommands:   []string{cmdScmShowRegions, cmdScmListNamespaces},
			expNamespaces: twoNs,
		},
		{
			desc: "v2 regions with no capacity",
			showRegionOut: "\n" +
				"---ISetID=0x2aba7f4828ef2ccc---\n" +
				"   PersistentMemoryType=AppDirect\n" +
				"   FreeCapacity=0.000 GiB\n" +
				"---ISetID=0x81187f4881f02ccb---\n" +
				"   PersistentMemoryType=AppDirect\n" +
				"   FreeCapacity=0.000 GiB\n" +
				"\n",
			expCommands:   []string{cmdScmShowRegions, cmdScmListNamespaces},
			expNamespaces: twoNs,
		},
		{
			desc: "unexpected output",
			showRegionOut: "\n" +
				"---ISetID=0x2aba7f4828ef2ccc---\n",
			expGetStateErrMsg: "checking scm region capacity: expecting at least 4 lines, got 3",
		},
	}

	for _, tt := range tests {
		t.Run(tt.desc, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)

			mockLookPath := func(string) (s string, err error) {
				return
			}
			mockBinding := newMockIpmctl(&mockIpmctlCfg{
				discoverModulesRet: nil,
				modules:            []ipmctl.DeviceDiscovery{MockDiscovery()},
			})
			cr := newCmdRunner(log, mockBinding, mockRun, mockLookPath)
			if _, err := cr.Discover(); err != nil {
				t.Fatal(err)
			}

			// reset to initial values between tests
			regionsOut = tt.showRegionOut
			pmemId = 1
			commands = nil

			scmState, err := cr.GetState()
			ExpectError(t, err, tt.expGetStateErrMsg, tt.desc)
			if tt.expGetStateErrMsg != "" {
				return
			}

			needsReboot, namespaces, err := cr.Prep(scmState)
			if tt.expErrMsg != "" {
				ExpectError(t, err, tt.expErrMsg, tt.desc)
				return
			}
			if err != nil {
				t.Fatal(tt.desc + ": " + err.Error())
			}

			AssertEqual(t, commands, tt.expCommands, tt.desc+": unexpected list of commands run")
			AssertEqual(t, needsReboot, tt.expRebootRequired, tt.desc+": unexpected value for is reboot required")
			AssertEqual(t, namespaces, tt.expNamespaces, tt.desc+": unexpected list of pmem device file names")
		})
	}
}

func TestParseNamespaces(t *testing.T) {
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

			CmpErr(t, tc.expErr, gotErr)
			if diff := cmp.Diff(tc.expNamespaces, gotNamespaces); diff != "" {
				t.Fatalf("unexpected namespace result (-want, +got):\n%s\n", diff)
			}
		})
	}
}

// TestGetNamespaces tests the internals of prepScm, pass in mock runCmd to verify
// behavior. Don't use mockPrepScm as we want to test prepScm logic.
func TestGetNamespaces(t *testing.T) {
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
	twoNsJson := "[" + fmt.Sprintf(nsOut, 1, 1, 0) + "," + fmt.Sprintf(nsOut, 2, 2, 1) + "]"
	twoNs, _ := parseNamespaces(twoNsJson)

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
			expCommands:   []string{cmdScmListNamespaces},
			expNamespaces: storage.ScmNamespaces{},
		},
		{
			desc:          "single pmem device",
			cmdOut:        fmt.Sprintf(nsOut, 1, 1, 0),
			expCommands:   []string{cmdScmListNamespaces},
			expNamespaces: oneNs,
		},
		{
			desc:          "two pmem device",
			cmdOut:        twoNsJson,
			expCommands:   []string{cmdScmListNamespaces},
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
			defer ShowBufferOnFailure(t, buf)

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
				discoverModulesRet: nil,
				modules:            []ipmctl.DeviceDiscovery{MockDiscovery()},
			})
			cr := newCmdRunner(log, mockBinding, mockRun, mockLookPath)

			if _, err := cr.Discover(); err != nil {
				t.Fatal(err)
			}

			namespaces, err := cr.GetNamespaces()
			if err != nil {
				if tt.lookPathErrMsg != "" {
					ExpectError(t, err, tt.lookPathErrMsg, tt.desc)
					return
				}
				t.Fatal(tt.desc + ": GetNamespaces: " + err.Error())
			}

			AssertEqual(t, commands, tt.expCommands, tt.desc+": unexpected list of commands run")
			AssertEqual(t, namespaces, tt.expNamespaces, tt.desc+": unexpected list of pmem device file names")
		})
	}
}

func TestIpmctl_Discover(t *testing.T) {
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
		"ipmctl.Discovery failed": {
			cfg: &mockIpmctlCfg{
				discoverModulesRet: errors.New("mock Discover"),
			},
			expErr: errors.New("failed to discover SCM modules: mock Discover"),
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
			defer ShowBufferOnFailure(t, buf)

			mockBinding := newMockIpmctl(tc.cfg)
			cr := newCmdRunner(log, mockBinding, nil, nil)

			result, err := cr.Discover()

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

			AssertEqual(t, tc.expResult, result, "didn't match")
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
			defer ShowBufferOnFailure(t, buf)

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
			defer ShowBufferOnFailure(t, buf)

			mockBinding := newMockIpmctl(tc.cfg)
			cr := newCmdRunner(log, mockBinding, nil, nil)

			err := cr.UpdateFirmware(tc.inputUID, "/dont/care")

			common.CmpErr(t, tc.expErr, err)
		})
	}
}
