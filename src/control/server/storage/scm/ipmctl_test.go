//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package scm

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/proto"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/ipmctl"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

type (
	mockIpmctlCfg struct {
		initErr           error
		getModulesErr     error
		modules           []ipmctl.DeviceDiscovery
		getRegionsErr     error
		regions           []ipmctl.PMemRegion
		delGoalsErr       error
		getFWInfoRet      error
		fwInfo            ipmctl.DeviceFirmwareInfo
		updateFirmwareRet error
	}

	mockIpmctl struct {
		cfg mockIpmctlCfg
	}
)

func (m *mockIpmctl) Init(_ logging.Logger) error {
	return m.cfg.initErr
}

func (m *mockIpmctl) GetModules(_ logging.Logger) ([]ipmctl.DeviceDiscovery, error) {
	return m.cfg.modules, m.cfg.getModulesErr
}

func (m *mockIpmctl) GetRegions(_ logging.Logger) ([]ipmctl.PMemRegion, error) {
	return m.cfg.regions, m.cfg.getRegionsErr
}

func (m *mockIpmctl) DeleteConfigGoals(_ logging.Logger) error {
	return m.cfg.delGoalsErr
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

// MockDiscovery returns a mock SCM module of type exported from ipmctl.
func MockDiscovery(sockID ...int) ipmctl.DeviceDiscovery {
	m := proto.MockScmModule()

	sid := m.Socketid
	if len(sockID) > 0 {
		sid = uint32(sockID[0])
	}

	result := ipmctl.DeviceDiscovery{
		Physical_id:          uint16(m.Physicalid),
		Channel_id:           uint16(m.Channelid),
		Channel_pos:          uint16(m.Channelposition),
		Memory_controller_id: uint16(m.Controllerid),
		Socket_id:            uint16(sid),
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

func TestIpmctl_getModules(t *testing.T) {
	testDevices := []ipmctl.DeviceDiscovery{
		MockDiscovery(0),
		MockDiscovery(0),
		MockDiscovery(1),
	}

	expModules := storage.ScmModules{}
	for _, dev := range testDevices {
		mod := MockModule(&dev)
		expModules = append(expModules, &mod)
	}

	for name, tc := range map[string]struct {
		cfg       *mockIpmctlCfg
		sockID    int
		expErr    error
		expResult storage.ScmModules
	}{
		"ipmctl GetModules failed": {
			cfg: &mockIpmctlCfg{
				getModulesErr: errors.New("mock GetModules"),
			},
			sockID: sockAny,
			expErr: errors.New("failed to discover pmem modules: mock GetModules"),
		},
		"no modules": {
			cfg:       &mockIpmctlCfg{},
			sockID:    sockAny,
			expResult: storage.ScmModules{},
		},
		"get modules with no socket filter": {
			cfg: &mockIpmctlCfg{
				modules: testDevices,
			},
			sockID:    sockAny,
			expResult: expModules,
		},
		"filter modules by socket 0": {
			cfg: &mockIpmctlCfg{
				modules: testDevices,
			},
			sockID:    0,
			expResult: expModules[:2],
		},
		"filter modules by socket 1": {
			cfg: &mockIpmctlCfg{
				modules: testDevices,
			},
			sockID:    1,
			expResult: expModules[2:],
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

			result, err := cr.getModules(tc.sockID)

			test.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResult, result); diff != "" {
				t.Errorf("wrong firmware info (-want, +got):\n%s\n", diff)
			}
		})
	}
}

//		"socket 0 only; free capacity": {
//			selectSock0: true,
//			runOut: []string{
//				"Intel(R) Optane(TM) Persistent Memory Command Line Interface Version 02.00.00.3825",
//				mockXMLRegions(t, "sock-zero"),
//			},
//			expState: &storage.ScmSocketState{
//				State: storage.ScmFreeCap,
//			},
//		},
//		"regions only one with free capacity": {
//			runOut: []string{
//				"Intel(R) Optane(TM) Persistent Memory Command Line Interface Version 02.00.00.3825",
//				"---ISetID=0x2aba7f4828ef2ccc---\n" +
//					"   SocketID=0x0000\n" +
//					"   PersistentMemoryType=AppDirect\n" +
//					"   FreeCapacity=0.0 GiB\n" +
//					"---ISetID=0x81187f4881f02ccc---\n" +
//					"   SocketID=0x0001\n" +
//					"   PersistentMemoryType=AppDirect\n" +
//					"   FreeCapacity=3012.0 GiB\n",
//			},
//			expState: storage.ScmFreeCap,
//		},
//		"regions with free capacity": {
//			runOut: []string{
//				"Intel(R) Optane(TM) Persistent Memory Command Line Interface Version 02.00.00.3825",
//				"---ISetID=0x2aba7f4828ef2ccc---\n" +
//					"   SocketID=0x0000\n" +
//					"   PersistentMemoryType=AppDirect\n" +
//					"   FreeCapacity=3012.0 GiB\n" +
//					"---ISetID=0x81187f4881f02ccc---\n" +
//					"   SocketID=0x0001\n" +
//					"   PersistentMemoryType=AppDirect\n" +
//					"   FreeCapacity=3012.0 GiB\n",
//			},
//			expState: storage.ScmFreeCap,
//		},
//		"regions with no free capacity": {
//			runOut: []string{
//				"Intel(R) Optane(TM) Persistent Memory Command Line Interface Version 02.00.00.3825",
//				"---ISetID=0x2aba7f4828ef2ccc---\n" +
//					"   SocketID=0x0000\n" +
//					"   PersistentMemoryType=AppDirect\n" +
//					"   FreeCapacity=0.0 GiB\n" +
//					"---ISetID=0x81187f4881f02ccb---\n" +
//					"   SocketID=0x0001\n" +
//					"   PersistentMemoryType=AppDirect\n" +
//					"   FreeCapacity=0.0 GiB\n",
//			},
//			expState: storage.ScmNoFreeCap,
//		},
//		"v2 regions with no capacity": {
//			runOut: []string{
//				"Intel(R) Optane(TM) Persistent Memory Command Line Interface Version 02.00.00.3825",
//				"---ISetID=0x2aba7f4828ef2ccc---\n" +
//					"   SocketID=0x0000\n" +
//					"   PersistentMemoryType=AppDirect\n" +
//					"   FreeCapacity=0.000 GiB\n" +
//					"---ISetID=0x81187f4881f02ccb---\n" +
//					"   SocketID=0x0001\n" +
//					"   PersistentMemoryType=AppDirect\n" +
//					"   FreeCapacity=0.000 GiB\n",
//			},
//			expState: storage.ScmNoFreeCap,
//		},
//		"unexpected output": {
//			runOut: []string{
//				"Intel(R) Optane(TM) Persistent Memory Command Line Interface Version 02.00.00.3825",
//				"---ISetID=0x2aba7f4828ef2ccc---\n",
//			},
//			expErr: errors.New("expecting at least 4 lines, got 1"),
//		},

// TestIpmctl_getPMemState verifies the appropriate PMem state is returned for either a specific
// socket region or all regions when either a specific socket is requested or a state is specific to
// a particular socket.
func TestIpmctl_getPMemState(t *testing.T) {
	verOut := `Intel(R) Optane(TM) Persistent Memory Command Line Interface Version 02.00.00.3825`

	for name, tc := range map[string]struct {
		runOut   []string
		runErr   []error
		expErr   error
		expState storage.ScmState
		expSock0 bool
		expSock1 bool
	}{
		"get regions fails": {
			runOut: []string{
				verOut, `text that is invalid xml`,
			},
			expErr: errors.New("parse show region cmd"),
		},
		"zero modules": {
			runOut: []string{
				verOut, outNoPMemModules,
			},
			expState: storage.ScmNoModules,
		},
		"modules but no regions": {
			runOut: []string{
				verOut, outNoPMemRegions,
			},
			expState: storage.ScmNoRegions,
		},
		"single region with uncorrectable error": {
			runOut: []string{
				verOut, mockXMLRegions(t, "unhealthy"),
			},
			expSock0: true,
			expState: storage.ScmNotHealthy,
		},
		"single region with free capacity": {
			runOut: []string{
				verOut, mockXMLRegions(t, "full-free"),
			},
			expState: storage.ScmFreeCap,
		},
		"single region with no free capacity": {
			runOut: []string{
				verOut, mockXMLRegions(t, "no-free"),
			},
			expState: storage.ScmNoFreeCap,
		},
		"second region has uncorrectable error": {
			runOut: []string{
				verOut, mockXMLRegions(t, "unhealthy-2nd-sock"),
			},
			expSock1: true,
			expState: storage.ScmNotHealthy,
		},
		"second region has free capacity": {
			runOut: []string{
				verOut, mockXMLRegions(t, "full-free-2nd-sock"),
			},
			expSock1: true,
			expState: storage.ScmFreeCap,
		},
		//			ipmctlCfg: &mockIpmctlCfg{
		//				regions: []ipmctl.PMemRegion{
		//					{Free_capacity: 111111},
		//				},
		//			},
		//			expErr: errors.New("unexpected PMem region type"),
		//		},
		//		"single region with not interleaved type": {
		//			ipmctlCfg: &mockIpmctlCfg{
		//				regions: []ipmctl.PMemRegion{
		//					{
		//						Free_capacity: 111111,
		//						Type:          uint32(ipmctl.RegionTypeNotInterleaved),
		//					},
		//				},
		//			},
		//			expState: storage.ScmNotInterleaved,
		//		},
		//		"single region with free capacity": {
		//			ipmctlCfg: &mockIpmctlCfg{
		//				regions: []ipmctl.PMemRegion{
		//					{
		//						Free_capacity: 111111,
		//						Type:          uint32(ipmctl.RegionTypeAppDirect),
		//					},
		//				},
		//			},
		//			expState: storage.ScmFreeCap,
		//		},
		//		"regions with free capacity": {
		//			ipmctlCfg: &mockIpmctlCfg{
		//				regions: []ipmctl.PMemRegion{
		//					{Type: uint32(ipmctl.RegionTypeAppDirect)},
		//					{
		//						Free_capacity: 111111,
		//						Type:          uint32(ipmctl.RegionTypeAppDirect),
		//					},
		//				},
		//			},
		//			expState: storage.ScmFreeCap,
		//		},
		//		"regions with no capacity": {
		//			ipmctlCfg: &mockIpmctlCfg{
		//				regions: []ipmctl.PMemRegion{
		//					{Type: uint32(ipmctl.RegionTypeAppDirect)},
		//					{Type: uint32(ipmctl.RegionTypeAppDirect)},
		//				},
		//			},
		//			expState: storage.ScmNoFreeCap,
		//		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			callIdx := 0

			mockRun := func(in string) (string, error) {
				out := ""
				if len(tc.runOut) > callIdx {
					out = tc.runOut[callIdx]
				}

				var err error = nil
				if len(tc.runErr) > callIdx {
					err = tc.runErr[callIdx]
				}

				callIdx++

				return out, err
			}

			cr, err := newCmdRunner(log, nil, mockRun, nil)
			if err != nil {
				t.Fatal(err)
			}

			resp, err := cr.getPMemState(sockAny)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			sockID := new(uint)
			if tc.expSock0 {
				*sockID = 0
			} else if tc.expSock1 {
				*sockID = 1
			}

			expResp := &storage.ScmSocketState{
				SocketID: sockID,
				State:    tc.expState,
			}
			t.Logf("socket state: %+v", expResp)

			if diff := cmp.Diff(expResp, resp); diff != "" {
				t.Fatalf("unexpected scm state (-want, +got):\n%s\n", diff)
			}
		})
	}
}

//func TestIpmctl_prep(t *testing.T) {
//	verStr := "Intel(R) Optane(TM) Persistent Memory Command Line Interface Version 02.00.00.3825"
//	ndctlNsStr := `[{
//   "dev":"namespace1.0",
//   "mode":"fsdax",
//   "map":"dev",
//   "size":3183575302144,
//   "uuid":"842fc847-28e0-4bb6-8dfc-d24afdba1528",
//   "raw_uuid":"dedb4b28-dc4b-4ccd-b7d1-9bd475c91264",
//   "sector_size":512,
//   "blockdev":"pmem1",
//   "numa_node":1
//}]`
//	ndctl2NsStr := `[{
//   "dev":"namespace1.0",
//   "mode":"fsdax",
//   "map":"dev",
//   "size":3183575302144,
//   "uuid":"842fc847-28e0-4bb6-8dfc-d24afdba1528",
//   "raw_uuid":"dedb4b28-dc4b-4ccd-b7d1-9bd475c91264",
//   "sector_size":512,
//   "blockdev":"pmem1",
//   "numa_node":1
//},{
//   "dev":"namespace0.0",
//   "mode":"fsdax",
//   "map":"dev",
//   "size":3183575302144,
//   "uuid":"842fc847-28e0-4bb6-8dfc-d24afdba1529",
//   "raw_uuid":"dedb4b28-dc4b-4ccd-b7d1-9bd475c91265",
//   "sector_size":512,
//   "blockdev":"pmem0",
//   "numa_node":0
//}]`
//	dualSockNoFreeCap := `---ISetID=0x2aba7f4828ef2ccc---
//   SocketID=0x0000
//   PersistentMemoryType=AppDirect
//   Capacity=3012.000 GiB
//   FreeCapacity=0.000 GiB
//   HealthState=Healthy
//   DimmID=0x0001, 0x0011, 0x0021, 0x0101, 0x0111, 0x0121
//---ISetID=0x81187f4881f02ccc---
//   SocketID=0x0001
//   PersistentMemoryType=AppDirect
//   Capacity=3012.000 GiB
//   FreeCapacity=0.000 GiB
//   HealthState=Healthy
//   DimmID=0x0001, 0x0011, 0x0021, 0x0101, 0x0111, 0x0121
//`
//	//	dualSockNoFreeCapSingleDecimalPlace := `---ISetID=0x2aba7f4828ef2ccc---
//	//   SocketID=0x0000
//	//   PersistentMemoryType=AppDirect
//	//   Capacity=3012.0 GiB
//	//   FreeCapacity=0.0 GiB
//	//   HealthState=Healthy
//	//   DimmID=0x0001, 0x0011, 0x0021, 0x0101, 0x0111, 0x0121
//	//---ISetID=0x81187f4881f02ccc---
//	//   SocketID=0x0001
//	//   PersistentMemoryType=AppDirect
//	//   Capacity=3012.0 GiB
//	//   FreeCapacity=0.0 GiB
//	//   HealthState=Healthy
//	//   DimmID=0x0001, 0x0011, 0x0021, 0x0101, 0x0111, 0x0121
//	//`
//	//	singleSockNoFreeCap := `---ISetID=0x2aba7f4828ef2ccc---
//	//   SocketID=0x0000
//	//   PersistentMemoryType=AppDirect
//	//   Capacity=3012.000 GiB
//	//   FreeCapacity=0.000 GiB
//	//   HealthState=Healthy
//	//   DimmID=0x0001, 0x0011, 0x0021, 0x0101, 0x0111, 0x0121
//	//`
//	//	dualSockSingleFreeCap := `---ISetID=0x2aba7f4828ef2ccc---
//	//   SocketID=0x0000
//	//   PersistentMemoryType=AppDirect
//	//   Capacity=3012.000 GiB
//	//   FreeCapacity=3012.000 GiB
//	//   HealthState=Healthy
//	//   DimmID=0x0001, 0x0011, 0x0021, 0x0101, 0x0111, 0x0121
//	//---ISetID=0x81187f4881f02ccc---
//	//   SocketID=0x0001
//	//   PersistentMemoryType=AppDirect
//	//   Capacity=3012.000 GiB
//	//   FreeCapacity=0.000 GiB
//	//   HealthState=Healthy
//	//   DimmID=0x0001, 0x0011, 0x0021, 0x0101, 0x0111, 0x0121
//	//`
//	//	dualSockDualFreeCap := `---ISetID=0x2aba7f4828ef2ccc---
//	//   SocketID=0x0000
//	//   PersistentMemoryType=AppDirect
//	//   Capacity=3012.000 GiB
//	//   FreeCapacity=3012.000 GiB
//	//   HealthState=Healthy
//	//   DimmID=0x0001, 0x0011, 0x0021, 0x0101, 0x0111, 0x0121
//	//---ISetID=0x81187f4881f02ccc---
//	//   SocketID=0x0001
//	//   PersistentMemoryType=AppDirect
//	//   Capacity=3012.000 GiB
//	//   FreeCapacity=3012.000 GiB
//	//   HealthState=Healthy
//	//   DimmID=0x0001, 0x0011, 0x0021, 0x0101, 0x0111, 0x0121
//	//`
//
//	for name, tc := range map[string]struct {
//		prepReq     *storage.ScmPrepareRequest
//		scanResp    *storage.ScmScanResponse
//		runOut      []string
//		runErr      []error
//		regions     []ipmctl.PMemRegion
//		regionsErr  error
//		delGoalsErr error
//		expErr      error
//		expPrepResp *storage.ScmPrepareResponse
//		expCalls    []string
//	}{
//		"state unknown": {
//			scanResp: &storage.ScmScanResponse{
//				State: storage.ScmStateUnknown,
//			},
//			expErr: errors.New("unhandled scm state"),
//		},
//		"state non-interleaved": {
//			scanResp: &storage.ScmScanResponse{
//				State: storage.ScmNotInterleaved,
//			},
//			expErr: storage.FaultScmNotInterleaved,
//		},
//		"state no regions": {
//			scanResp: &storage.ScmScanResponse{
//				State: storage.ScmNoRegions,
//			},
//			expPrepResp: &storage.ScmPrepareResponse{
//				State:          storage.ScmNoRegions,
//				RebootRequired: true,
//			},
//			expCalls: []string{
//				"ipmctl version", "ipmctl create -f -goal PersistentMemoryType=AppDirect",
//			},
//		},
//		"state no regions; delete goals fails": {
//			scanResp: &storage.ScmScanResponse{
//				State: storage.ScmNoRegions,
//			},
//			runOut: []string{
//				verStr,
//			},
//			delGoalsErr: errors.New("fail"),
//			expErr:      errors.New("fail"),
//		},
//		"state no regions; create regions fails": {
//			scanResp: &storage.ScmScanResponse{
//				State: storage.ScmNoRegions,
//			},
//			runErr:   []error{errors.New("cmd failed")},
//			expCalls: []string{"ipmctl version"},
//			expErr:   errors.New("cmd failed"),
//		},
//		"state free capacity": {
//			scanResp: &storage.ScmScanResponse{
//				State: storage.ScmFreeCap,
//			},
//			runOut: []string{
//				ndctlNsStr,
//				"---ISetID=0x2aba7f4828ef2ccc---\n" +
//					"   SocketID=0x0000\n" +
//					"   PersistentMemoryType=AppDirect\n" +
//					"   FreeCapacity=0.0 GiB\n" +
//					"---ISetID=0x81187f4881f02ccc---\n" +
//					"   SocketID=0x0001\n" +
//					"   PersistentMemoryType=AppDirect\n" +
//					"   FreeCapacity=3012.0 GiB\n",
//				ndctl2NsStr,
//				"---ISetID=0x2aba7f4828ef2ccc---\n" +
//					"   SocketID=0x0000\n" +
//					"   PersistentMemoryType=AppDirect\n" +
//					"   FreeCapacity=0.0 GiB\n" +
//					"---ISetID=0x81187f4881f02ccc---\n" +
//					"   SocketID=0x0001\n" +
//					"   PersistentMemoryType=AppDirect\n" +
//					"   FreeCapacity=0.0 GiB\n",
//				ndctl2NsStr,
//			},
//			// TODO DAOS-10173: re-enable when bindings can be used instead of cli
//			//regions: []ipmctl.PMemRegion{{Type: uint32(ipmctl.RegionTypeAppDirect)}},
//			expPrepResp: &storage.ScmPrepareResponse{
//				State: storage.ScmNoFreeCap,
//				Namespaces: storage.ScmNamespaces{
//					{
//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
//						BlockDevice: "pmem1",
//						Name:        "namespace1.0",
//						NumaNode:    1,
//						Size:        3183575302144,
//					},
//					{
//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1529",
//						BlockDevice: "pmem0",
//						Name:        "namespace0.0",
//						NumaNode:    0,
//						Size:        3183575302144,
//					},
//				},
//			},
//			expCalls: []string{
//				"ndctl create-namespace",
//				"ipmctl show -a -region",
//				"ndctl create-namespace",
//				"ipmctl show -a -region",
//				"ndctl list -N -v",
//			},
//		},
//		"state free capacity; create namespaces fails": {
//			scanResp: &storage.ScmScanResponse{
//				State: storage.ScmFreeCap,
//			},
//			runErr:   []error{errors.New("cmd failed")},
//			expErr:   errors.New("cmd failed"),
//			expCalls: []string{"ndctl create-namespace"},
//		},
//		// TODO DAOS-10173: re-enable
//		//"state free capacity; get regions fails": {
//		//	scanResp: &storage.ScmScanResponse{
//		//		State: storage.ScmFreeCap,
//		//	},
//		//	runOut:     []string{ndctlNsStr},
//		//	regionsErr: errors.New("fail"),
//		//	expCalls:   []string{"ndctl create-namespace"},
//		//	expErr:     errors.New("discover PMem regions: fail"),
//		//},
//		"state no free capacity; missing namespace": {
//			scanResp: &storage.ScmScanResponse{
//				State: storage.ScmNoFreeCap,
//				Namespaces: storage.ScmNamespaces{
//					{
//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
//						BlockDevice: "pmem1",
//						Name:        "namespace1.0",
//						NumaNode:    1,
//						Size:        3183575302144,
//					},
//				},
//			},
//			runOut: []string{
//				verStr,
//				"---ISetID=0x2aba7f4828ef2ccc---\n" +
//					"   SocketID=0x0000\n" +
//					"   PersistentMemoryType=AppDirect\n" +
//					"   FreeCapacity=0.0 GiB\n" +
//					"---ISetID=0x81187f4881f02ccc---\n" +
//					"   SocketID=0x0001\n" +
//					"   PersistentMemoryType=AppDirect\n" +
//					"   FreeCapacity=0.0 GiB\n",
//			},
//			expErr: errors.New("want 2 PMem namespaces but got 1"),
//			expCalls: []string{
//				"ipmctl version",
//				"ipmctl show -a -region",
//			},
//		},
//		"state no free capacity; requested number of namespaces does not match": {
//			prepReq: &storage.ScmPrepareRequest{
//				NrNamespacesPerSocket: 2,
//			},
//			scanResp: &storage.ScmScanResponse{
//				State: storage.ScmNoFreeCap,
//				Namespaces: storage.ScmNamespaces{
//					{
//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
//						BlockDevice: "pmem1",
//						Name:        "namespace1.0",
//						NumaNode:    1,
//						Size:        3183575302144,
//					},
//					{
//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1529",
//						BlockDevice: "pmem0",
//						Name:        "namespace0.0",
//						NumaNode:    0,
//						Size:        3183575302144,
//					},
//				},
//			},
//			runOut: []string{
//				verStr,
//				"---ISetID=0x2aba7f4828ef2ccc---\n" +
//					"   SocketID=0x0000\n" +
//					"   PersistentMemoryType=AppDirect\n" +
//					"   FreeCapacity=0.0 GiB\n" +
//					"---ISetID=0x81187f4881f02ccc---\n" +
//					"   SocketID=0x0001\n" +
//					"   PersistentMemoryType=AppDirect\n" +
//					"   FreeCapacity=0.0 GiB\n",
//			},
//			expErr: errors.New("want 4 PMem namespaces but got 2"),
//			expCalls: []string{
//				"ipmctl version",
//				"ipmctl show -a -region",
//			},
//		},
//		"state no free capacity": {
//			scanResp: &storage.ScmScanResponse{
//				State: storage.ScmNoFreeCap,
//				Namespaces: storage.ScmNamespaces{
//					{
//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
//						BlockDevice: "pmem1",
//						Name:        "namespace1.0",
//						NumaNode:    1,
//						Size:        3183575302144,
//					},
//					{
//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1529",
//						BlockDevice: "pmem0",
//						Name:        "namespace0.0",
//						NumaNode:    0,
//						Size:        3183575302144,
//					},
//				},
//			},
//			runOut: []string{
//				verStr,
//				"---ISetID=0x2aba7f4828ef2ccc---\n" +
//					"   SocketID=0x0000\n" +
//					"   PersistentMemoryType=AppDirect\n" +
//					"   FreeCapacity=0.0 GiB\n" +
//					"---ISetID=0x81187f4881f02ccc---\n" +
//					"   SocketID=0x0001\n" +
//					"   PersistentMemoryType=AppDirect\n" +
//					"   FreeCapacity=0.0 GiB\n",
//			},
//			expPrepResp: &storage.ScmPrepareResponse{
//				State: storage.ScmNoFreeCap,
//				Namespaces: storage.ScmNamespaces{
//					{
//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
//						BlockDevice: "pmem1",
//						Name:        "namespace1.0",
//						NumaNode:    1,
//						Size:        3183575302144,
//					},
//					{
//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1529",
//						BlockDevice: "pmem0",
//						Name:        "namespace0.0",
//						NumaNode:    0,
//						Size:        3183575302144,
//					},
//				},
//			},
//			expCalls: []string{
//				"ipmctl version",
//				"ipmctl show -a -region",
//			},
//		},
//		"state no free capacity; multiple namespaces per socket; requested number does not match": {
//			prepReq: &storage.ScmPrepareRequest{
//				NrNamespacesPerSocket: 1,
//			},
//			scanResp: &storage.ScmScanResponse{
//				State: storage.ScmNoFreeCap,
//				Namespaces: storage.ScmNamespaces{
//					{
//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
//						BlockDevice: "pmem1",
//						Name:        "namespace1.0",
//						NumaNode:    1,
//						Size:        3183575302144,
//					},
//					{
//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1520",
//						BlockDevice: "pmem1.1",
//						Name:        "namespace1.1",
//						NumaNode:    1,
//						Size:        3183575302144,
//					},
//					{
//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1527",
//						BlockDevice: "pmem0",
//						Name:        "namespace0.0",
//						NumaNode:    0,
//						Size:        3183575302144,
//					},
//					{
//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1529",
//						BlockDevice: "pmem0.1",
//						Name:        "namespace0.1",
//						NumaNode:    0,
//						Size:        3183575302144,
//					},
//				},
//			},
//			runOut: []string{
//				verStr,
//				"---ISetID=0x2aba7f4828ef2ccc---\n" +
//					"   SocketID=0x0000\n" +
//					"   PersistentMemoryType=AppDirect\n" +
//					"   FreeCapacity=0.0 GiB\n" +
//					"---ISetID=0x81187f4881f02ccc---\n" +
//					"   SocketID=0x0001\n" +
//					"   PersistentMemoryType=AppDirect\n" +
//					"   FreeCapacity=0.0 GiB\n",
//			},
//			expErr: errors.New("want 2 PMem namespaces but got 4"),
//			expCalls: []string{
//				"ipmctl version",
//				"ipmctl show -a -region",
//			},
//		},
//		"state no free capacity; multiple namespaces per socket; one region has no capacity": {
//			prepReq: &storage.ScmPrepareRequest{
//				NrNamespacesPerSocket: 2,
//			},
//			scanResp: &storage.ScmScanResponse{
//				State: storage.ScmNoFreeCap,
//				Namespaces: storage.ScmNamespaces{
//					{
//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1527",
//						BlockDevice: "pmem0",
//						Name:        "namespace0.0",
//						NumaNode:    0,
//						Size:        3183575302144,
//					},
//				},
//			},
//			runOut: []string{
//				verStr,
//				"---ISetID=0x2aba7f4828ef2ccc---\n" +
//					"   SocketID=0x0000\n" +
//					"   PersistentMemoryType=AppDirect\n" +
//					"   FreeCapacity=0.0 GiB\n" +
//					"---ISetID=0x81187f4881f02ccc---\n" +
//					"   SocketID=0x0001\n" +
//					"   PersistentMemoryType=AppDirect\n" +
//					"   FreeCapacity=3012.0 GiB\n",
//			},
//			expErr: errors.New("want 4 PMem namespaces but got 1"),
//			expCalls: []string{
//				"ipmctl version",
//				"ipmctl show -a -region",
//			},
//		},
//		"state no free capacity; multiple namespaces per socket": {
//			prepReq: &storage.ScmPrepareRequest{
//				NrNamespacesPerSocket: 2,
//			},
//			scanResp: &storage.ScmScanResponse{
//				State: storage.ScmNoFreeCap,
//				Namespaces: storage.ScmNamespaces{
//					{
//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
//						BlockDevice: "pmem1",
//						Name:        "namespace1.0",
//						NumaNode:    1,
//						Size:        3183575302144,
//					},
//					{
//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1520",
//						BlockDevice: "pmem1.1",
//						Name:        "namespace1.1",
//						NumaNode:    1,
//						Size:        3183575302144,
//					},
//					{
//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1527",
//						BlockDevice: "pmem0",
//						Name:        "namespace0.0",
//						NumaNode:    0,
//						Size:        3183575302144,
//					},
//					{
//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1529",
//						BlockDevice: "pmem0.1",
//						Name:        "namespace0.1",
//						NumaNode:    0,
//						Size:        3183575302144,
//					},
//				},
//			},
//			runOut: []string{
//				verStr,
//				dualSockNoFreeCap,
//			},
//			expPrepResp: &storage.ScmPrepareResponse{
//				State: storage.ScmNoFreeCap,
//				Namespaces: storage.ScmNamespaces{
//					{
//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
//						BlockDevice: "pmem1",
//						Name:        "namespace1.0",
//						NumaNode:    1,
//						Size:        3183575302144,
//					},
//					{
//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1520",
//						BlockDevice: "pmem1.1",
//						Name:        "namespace1.1",
//						NumaNode:    1,
//						Size:        3183575302144,
//					},
//					{
//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1527",
//						BlockDevice: "pmem0",
//						Name:        "namespace0.0",
//						NumaNode:    0,
//						Size:        3183575302144,
//					},
//					{
//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1529",
//						BlockDevice: "pmem0.1",
//						Name:        "namespace0.1",
//						NumaNode:    0,
//						Size:        3183575302144,
//					},
//				},
//			},
//			expCalls: []string{
//				"ipmctl version",
//				"ipmctl show -a -region",
//			},
//		},
//	} {
//		t.Run(name, func(t *testing.T) {
//			log, buf := logging.NewTestLogger(t.Name())
//			defer test.ShowBufferOnFailure(t, buf)
//
//			var calls []string
//			var callIdx int
//
//			mockBinding := newMockIpmctl(&mockIpmctlCfg{
//				regions:       tc.regions,
//				getRegionsErr: tc.regionsErr,
//				delGoalsErr:   tc.delGoalsErr,
//			})
//
//			mockRun := func(cmd string) (string, error) {
//				calls = append(calls, cmd)
//
//				o := verStr
//				if callIdx < len(tc.runOut) {
//					o = tc.runOut[callIdx]
//				}
//				var e error = nil
//				if callIdx < len(tc.runErr) {
//					e = tc.runErr[callIdx]
//				}
//
//				log.Debugf("mockRun call %d: ret/err %v/%v", callIdx, o, e)
//				callIdx++
//				return o, e
//			}
//
//			mockLookPath := func(string) (string, error) {
//				return "", nil
//			}
//
//			cr, err := newCmdRunner(log, mockBinding, mockRun, mockLookPath)
//			if err != nil {
//				t.Fatal(err)
//			}
//
//			if tc.prepReq == nil {
//				tc.prepReq = &storage.ScmPrepareRequest{}
//			}
//
//			resp, err := cr.prep(*tc.prepReq, tc.scanResp)
//			log.Debugf("calls made %+v", calls)
//			test.CmpErr(t, tc.expErr, err)
//
//			if diff := cmp.Diff(tc.expPrepResp, resp); diff != "" {
//				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
//			}
//			if diff := cmp.Diff(tc.expCalls, calls); diff != "" {
//				t.Fatalf("unexpected cli calls (-want, +got):\n%s\n", diff)
//			}
//		})
//	}
//}

//func TestIpmctl_prepReset(t *testing.T) {
//	verStr := "Intel(R) Optane(TM) Persistent Memory Command Line Interface Version 02.00.00.3825"
//
//	for name, tc := range map[string]struct {
//		scanResp    *storage.ScmScanResponse
//		runOut      string
//		runErr      error
//		regions     []ipmctl.PMemRegion
//		regionsErr  error
//		delGoalsErr error
//		expErr      error
//		expPrepResp *storage.ScmPrepareResponse
//		expCalls    []string
//	}{
//		"state unknown": {
//			scanResp: &storage.ScmScanResponse{
//				State: storage.ScmStateUnknown,
//			},
//			expErr: errors.New("unhandled scm state"),
//		},
//		"state no regions": {
//			scanResp: &storage.ScmScanResponse{
//				State: storage.ScmNoRegions,
//			},
//			expPrepResp: &storage.ScmPrepareResponse{
//				State: storage.ScmNoRegions,
//			},
//		},
//		"state regions": {
//			scanResp: &storage.ScmScanResponse{
//				State: storage.ScmFreeCap,
//			},
//			expPrepResp: &storage.ScmPrepareResponse{
//				State:          storage.ScmFreeCap,
//				RebootRequired: true,
//			},
//			expCalls: []string{
//				"ipmctl version", "ipmctl create -f -goal MemoryMode=100",
//			},
//		},
//		"state regions; delete goals fails": {
//			scanResp: &storage.ScmScanResponse{
//				State: storage.ScmFreeCap,
//			},
//			delGoalsErr: errors.New("fail"),
//			expErr:      errors.New("fail"),
//		},
//		"state regions; remove regions fails": {
//			scanResp: &storage.ScmScanResponse{
//				State: storage.ScmFreeCap,
//			},
//			runErr: errors.New("cmd failed"),
//			expErr: errors.New("cmd failed"),
//			expCalls: []string{
//				"ipmctl version",
//			},
//		},
//		"state no free capacity": {
//			scanResp: &storage.ScmScanResponse{
//				State: storage.ScmNoFreeCap,
//				Namespaces: storage.ScmNamespaces{
//					{
//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
//						BlockDevice: "pmem1",
//						Name:        "namespace1.0",
//						NumaNode:    1,
//						Size:        3183575302144,
//					},
//				},
//			},
//			expPrepResp: &storage.ScmPrepareResponse{
//				State:          storage.ScmNoFreeCap,
//				RebootRequired: true,
//			},
//			expCalls: []string{
//				"ndctl disable-namespace namespace1.0",
//				"ndctl destroy-namespace namespace1.0",
//				"ipmctl create -f -goal MemoryMode=100",
//			},
//		},
//		"state no free capacity; remove namespace fails": {
//			scanResp: &storage.ScmScanResponse{
//				State: storage.ScmNoFreeCap,
//				Namespaces: storage.ScmNamespaces{
//					{
//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
//						BlockDevice: "pmem1",
//						Name:        "namespace1.0",
//						NumaNode:    1,
//						Size:        3183575302144,
//					},
//				},
//			},
//			runErr:   errors.New("cmd failed"),
//			expErr:   errors.New("cmd failed"),
//			expCalls: []string{"ndctl disable-namespace namespace1.0"},
//		},
//		"state no free capacity; multiple namespaces per socket": {
//			scanResp: &storage.ScmScanResponse{
//				State: storage.ScmNoFreeCap,
//				Namespaces: storage.ScmNamespaces{
//					{
//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
//						BlockDevice: "pmem1",
//						Name:        "namespace1.0",
//						NumaNode:    1,
//						Size:        3183575302144,
//					},
//					{
//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1520",
//						BlockDevice: "pmem1.1",
//						Name:        "namespace1.1",
//						NumaNode:    1,
//						Size:        3183575302144,
//					},
//					{
//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1527",
//						BlockDevice: "pmem0",
//						Name:        "namespace0.0",
//						NumaNode:    0,
//						Size:        3183575302144,
//					},
//					{
//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1529",
//						BlockDevice: "pmem0.1",
//						Name:        "namespace0.1",
//						NumaNode:    0,
//						Size:        3183575302144,
//					},
//				},
//			},
//			expPrepResp: &storage.ScmPrepareResponse{
//				State:          storage.ScmNoFreeCap,
//				RebootRequired: true,
//			},
//			expCalls: []string{
//				"ndctl disable-namespace namespace1.0",
//				"ndctl destroy-namespace namespace1.0",
//				"ndctl disable-namespace namespace1.1",
//				"ndctl destroy-namespace namespace1.1",
//				"ndctl disable-namespace namespace0.0",
//				"ndctl destroy-namespace namespace0.0",
//				"ndctl disable-namespace namespace0.1",
//				"ndctl destroy-namespace namespace0.1",
//				"ipmctl create -f -goal MemoryMode=100",
//			},
//		},
//	} {
//		t.Run(name, func(t *testing.T) {
//			log, buf := logging.NewTestLogger(t.Name())
//			defer test.ShowBufferOnFailure(t, buf)
//
//			var calls []string
//
//			if tc.runOut == "" {
//				tc.runOut = verStr
//			}
//
//			mockBinding := newMockIpmctl(&mockIpmctlCfg{
//				regions:       tc.regions,
//				getRegionsErr: tc.regionsErr,
//				delGoalsErr:   tc.delGoalsErr,
//			})
//
//			mockRun := func(cmd string) (string, error) {
//				calls = append(calls, cmd)
//				return tc.runOut, tc.runErr
//			}
//
//			mockLookPath := func(string) (string, error) {
//				return "", nil
//			}
//
//			cr, err := newCmdRunner(log, mockBinding, mockRun, mockLookPath)
//			if err != nil {
//				t.Fatal(err)
//			}
//
//			resp, err := cr.prepReset(tc.scanResp)
//			test.CmpErr(t, tc.expErr, err)
//
//			if diff := cmp.Diff(tc.expPrepResp, resp); diff != "" {
//				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
//			}
//			if diff := cmp.Diff(tc.expCalls, calls); diff != "" {
//				t.Fatalf("unexpected cli calls (-want, +got):\n%s\n", diff)
//			}
//		})
//	}
//}
