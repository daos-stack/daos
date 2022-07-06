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

	"github.com/daos-stack/daos/src/control/common/proto"
	"github.com/daos-stack/daos/src/control/common/test"
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
			defer test.ShowBufferOnFailure(t, buf)

			mockRun := func(_ string) (string, error) {
				return preTxt + tc.verOut, nil
			}

			cr, err := newCmdRunner(log, nil, mockRun, nil)
			if err != nil {
				t.Fatal(err)
			}
			test.CmpErr(t, tc.expErr, cr.checkIpmctl(tc.badVers))
		})
	}
}

func TestIpmctl_getRegionStateFromCLI(t *testing.T) {
	for name, tc := range map[string]struct {
		runOut   []string
		runErr   []error
		expErr   error
		expState storage.ScmState
	}{
		"show regions fails": {
			runErr: []error{
				errors.New("fail"),
			},
			expErr: errors.New("fail"),
		},
		"modules but no regions": {
			runOut: []string{
				"Intel(R) Optane(TM) Persistent Memory Command Line Interface Version 02.00.00.3825",
				outNoPMemRegions,
			},
			expState: storage.ScmStateNoRegions,
		},
		"single region with free capacity": {
			runOut: []string{
				"Intel(R) Optane(TM) Persistent Memory Command Line Interface Version 02.00.00.3825",
				"---ISetID=0x2aba7f4828ef2ccc---\n" +
					"   PersistentMemoryType=AppDirect\n" +
					"   FreeCapacity=3012.0 GiB\n",
			},
			expState: storage.ScmStateFreeCapacity,
		},
		"regions only one with free capacity": {
			runOut: []string{
				"Intel(R) Optane(TM) Persistent Memory Command Line Interface Version 02.00.00.3825",
				"---ISetID=0x2aba7f4828ef2ccc---\n" +
					"   PersistentMemoryType=AppDirect\n" +
					"   FreeCapacity=0.0 GiB\n" +
					"---ISetID=0x81187f4881f02ccc---\n" +
					"   PersistentMemoryType=AppDirect\n" +
					"   FreeCapacity=3012.0 GiB\n",
			},
			expState: storage.ScmStateFreeCapacity,
		},
		"regions with free capacity": {
			runOut: []string{
				"Intel(R) Optane(TM) Persistent Memory Command Line Interface Version 02.00.00.3825",
				"---ISetID=0x2aba7f4828ef2ccc---\n" +
					"   PersistentMemoryType=AppDirect\n" +
					"   FreeCapacity=3012.0 GiB\n" +
					"---ISetID=0x81187f4881f02ccc---\n" +
					"   PersistentMemoryType=AppDirect\n" +
					"   FreeCapacity=3012.0 GiB\n",
			},
			expState: storage.ScmStateFreeCapacity,
		},
		"regions with no free capacity": {
			runOut: []string{
				"Intel(R) Optane(TM) Persistent Memory Command Line Interface Version 02.00.00.3825",
				"---ISetID=0x2aba7f4828ef2ccc---\n" +
					"   PersistentMemoryType=AppDirect\n" +
					"   FreeCapacity=0.0 GiB\n" +
					"---ISetID=0x81187f4881f02ccb---\n" +
					"   PersistentMemoryType=AppDirect\n" +
					"   FreeCapacity=0.0 GiB\n",
			},
			expState: storage.ScmStateNoFreeCapacity,
		},
		"v2 regions with no capacity": {
			runOut: []string{
				"Intel(R) Optane(TM) Persistent Memory Command Line Interface Version 02.00.00.3825",
				"---ISetID=0x2aba7f4828ef2ccc---\n" +
					"   PersistentMemoryType=AppDirect\n" +
					"   FreeCapacity=0.000 GiB\n" +
					"---ISetID=0x81187f4881f02ccb---\n" +
					"   PersistentMemoryType=AppDirect\n" +
					"   FreeCapacity=0.000 GiB\n",
			},
			expState: storage.ScmStateNoFreeCapacity,
		},
		"unexpected output": {
			runOut: []string{
				"Intel(R) Optane(TM) Persistent Memory Command Line Interface Version 02.00.00.3825",
				"---ISetID=0x2aba7f4828ef2ccc---\n",
			},
			expErr: errors.New("expecting at least 3 lines, got 1"),
		},
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

			scmState, err := cr.getRegionState()
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expState, scmState); diff != "" {
				t.Fatalf("unexpected scm state (-want, +got):\n%s\n", diff)
			}
		})
	}
}

// TestIpmctl_getRegionState tests the internals of GetState and verifies correct behavior based
// on different output from GetRegions() bindings call.
func TestIpmctl_getRegionState(t *testing.T) {
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
			defer test.ShowBufferOnFailure(t, buf)

			mockLookPath := func(string) (string, error) {
				return "", nil
			}

			mockRun := func(string) (string, error) {
				return "", nil
			}

			mockBinding := newMockIpmctl(tc.ipmctlCfg)
			cr, err := newCmdRunner(log, mockBinding, mockRun, mockLookPath)
			if err != nil {
				t.Fatal(err)
			}

			scmState, err := cr.getRegionStateFromBindings()
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expState, scmState); diff != "" {
				t.Fatalf("unexpected scm state (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestIpmctl_prep(t *testing.T) {
	verStr := "Intel(R) Optane(TM) Persistent Memory Command Line Interface Version 02.00.00.3825"
	ndctlNsStr := `[{
   "dev":"namespace1.0",
   "mode":"fsdax",
   "map":"dev",
   "size":3183575302144,
   "uuid":"842fc847-28e0-4bb6-8dfc-d24afdba1528",
   "raw_uuid":"dedb4b28-dc4b-4ccd-b7d1-9bd475c91264",
   "sector_size":512,
   "blockdev":"pmem1",
   "numa_node":1
}]`
	ndctl2NsStr := `[{
   "dev":"namespace1.0",
   "mode":"fsdax",
   "map":"dev",
   "size":3183575302144,
   "uuid":"842fc847-28e0-4bb6-8dfc-d24afdba1528",
   "raw_uuid":"dedb4b28-dc4b-4ccd-b7d1-9bd475c91264",
   "sector_size":512,
   "blockdev":"pmem1",
   "numa_node":1
},{
   "dev":"namespace0.0",
   "mode":"fsdax",
   "map":"dev",
   "size":3183575302144,
   "uuid":"842fc847-28e0-4bb6-8dfc-d24afdba1529",
   "raw_uuid":"dedb4b28-dc4b-4ccd-b7d1-9bd475c91265",
   "sector_size":512,
   "blockdev":"pmem0",
   "numa_node":0
}]`

	for name, tc := range map[string]struct {
		prepReq     *storage.ScmPrepareRequest
		scanResp    *storage.ScmScanResponse
		runOut      []string
		runErr      []error
		regions     []ipmctl.PMemRegion
		regionsErr  error
		delGoalsErr error
		expErr      error
		expPrepResp *storage.ScmPrepareResponse
		expCalls    []string
	}{
		"state unknown": {
			scanResp: &storage.ScmScanResponse{
				State: storage.ScmStateUnknown,
			},
			expErr: errors.New("unhandled scm state"),
		},
		"state non-interleaved": {
			scanResp: &storage.ScmScanResponse{
				State: storage.ScmStateNotInterleaved,
			},
			expErr: storage.FaultScmNotInterleaved,
		},
		"state no regions": {
			scanResp: &storage.ScmScanResponse{
				State: storage.ScmStateNoRegions,
			},
			expPrepResp: &storage.ScmPrepareResponse{
				State:          storage.ScmStateNoRegions,
				RebootRequired: true,
			},
			expCalls: []string{
				"ipmctl version", "ipmctl create -f -goal PersistentMemoryType=AppDirect",
			},
		},
		"state no regions; delete goals fails": {
			scanResp: &storage.ScmScanResponse{
				State: storage.ScmStateNoRegions,
			},
			runOut: []string{
				verStr,
			},
			delGoalsErr: errors.New("fail"),
			expErr:      errors.New("fail"),
		},
		"state no regions; create regions fails": {
			scanResp: &storage.ScmScanResponse{
				State: storage.ScmStateNoRegions,
			},
			runErr:   []error{errors.New("cmd failed")},
			expCalls: []string{"ipmctl version"},
			expErr:   errors.New("cmd failed"),
		},
		"state free capacity": {
			scanResp: &storage.ScmScanResponse{
				State: storage.ScmStateFreeCapacity,
			},
			runOut: []string{
				ndctlNsStr,
				"---ISetID=0x2aba7f4828ef2ccc---\n" +
					"   PersistentMemoryType=AppDirect\n" +
					"   FreeCapacity=0.0 GiB\n" +
					"---ISetID=0x81187f4881f02ccc---\n" +
					"   PersistentMemoryType=AppDirect\n" +
					"   FreeCapacity=3012.0 GiB\n",
				ndctl2NsStr,
				"---ISetID=0x2aba7f4828ef2ccc---\n" +
					"   PersistentMemoryType=AppDirect\n" +
					"   FreeCapacity=0.0 GiB\n" +
					"---ISetID=0x81187f4881f02ccc---\n" +
					"   PersistentMemoryType=AppDirect\n" +
					"   FreeCapacity=0.0 GiB\n",
				ndctl2NsStr,
			},
			// TODO DAOS-10173: re-enable when bindings can be used instead of cli
			//regions: []ipmctl.PMemRegion{{Type: uint32(ipmctl.RegionTypeAppDirect)}},
			expPrepResp: &storage.ScmPrepareResponse{
				State: storage.ScmStateNoFreeCapacity,
				Namespaces: storage.ScmNamespaces{
					{
						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
						BlockDevice: "pmem1",
						Name:        "namespace1.0",
						NumaNode:    1,
						Size:        3183575302144,
					},
					{
						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1529",
						BlockDevice: "pmem0",
						Name:        "namespace0.0",
						NumaNode:    0,
						Size:        3183575302144,
					},
				},
			},
			expCalls: []string{
				"ndctl create-namespace",
				"ipmctl show -d PersistentMemoryType,FreeCapacity -region",
				"ndctl create-namespace",
				"ipmctl show -d PersistentMemoryType,FreeCapacity -region",
				"ndctl list -N -v",
			},
		},
		"state free capacity; create namespaces fails": {
			scanResp: &storage.ScmScanResponse{
				State: storage.ScmStateFreeCapacity,
			},
			runErr:   []error{errors.New("cmd failed")},
			expErr:   errors.New("cmd failed"),
			expCalls: []string{"ndctl create-namespace"},
		},
		// TODO DAOS-10173: re-enable
		//"state free capacity; get regions fails": {
		//	scanResp: &storage.ScmScanResponse{
		//		State: storage.ScmStateFreeCapacity,
		//	},
		//	runOut:     []string{ndctlNsStr},
		//	regionsErr: errors.New("fail"),
		//	expCalls:   []string{"ndctl create-namespace"},
		//	expErr:     errors.New("discover PMem regions: fail"),
		//},
		"state no free capacity; missing namespace": {
			scanResp: &storage.ScmScanResponse{
				State: storage.ScmStateNoFreeCapacity,
				Namespaces: storage.ScmNamespaces{
					{
						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
						BlockDevice: "pmem1",
						Name:        "namespace1.0",
						NumaNode:    1,
						Size:        3183575302144,
					},
				},
			},
			runOut: []string{
				verStr,
				"---ISetID=0x2aba7f4828ef2ccc---\n" +
					"   PersistentMemoryType=AppDirect\n" +
					"   FreeCapacity=0.0 GiB\n" +
					"---ISetID=0x81187f4881f02ccc---\n" +
					"   PersistentMemoryType=AppDirect\n" +
					"   FreeCapacity=0.0 GiB\n",
			},
			expErr: errors.New("want 2 PMem namespaces but got 1"),
			expCalls: []string{
				"ipmctl version",
				"ipmctl show -d PersistentMemoryType,FreeCapacity -region",
			},
		},
		"state no free capacity; requested number of namespaces does not match": {
			prepReq: &storage.ScmPrepareRequest{
				NrNamespacesPerSocket: 2,
			},
			scanResp: &storage.ScmScanResponse{
				State: storage.ScmStateNoFreeCapacity,
				Namespaces: storage.ScmNamespaces{
					{
						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
						BlockDevice: "pmem1",
						Name:        "namespace1.0",
						NumaNode:    1,
						Size:        3183575302144,
					},
					{
						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1529",
						BlockDevice: "pmem0",
						Name:        "namespace0.0",
						NumaNode:    0,
						Size:        3183575302144,
					},
				},
			},
			runOut: []string{
				verStr,
				"---ISetID=0x2aba7f4828ef2ccc---\n" +
					"   PersistentMemoryType=AppDirect\n" +
					"   FreeCapacity=0.0 GiB\n" +
					"---ISetID=0x81187f4881f02ccc---\n" +
					"   PersistentMemoryType=AppDirect\n" +
					"   FreeCapacity=0.0 GiB\n",
			},
			expErr: errors.New("want 4 PMem namespaces but got 2"),
			expCalls: []string{
				"ipmctl version",
				"ipmctl show -d PersistentMemoryType,FreeCapacity -region",
			},
		},
		"state no free capacity": {
			scanResp: &storage.ScmScanResponse{
				State: storage.ScmStateNoFreeCapacity,
				Namespaces: storage.ScmNamespaces{
					{
						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
						BlockDevice: "pmem1",
						Name:        "namespace1.0",
						NumaNode:    1,
						Size:        3183575302144,
					},
					{
						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1529",
						BlockDevice: "pmem0",
						Name:        "namespace0.0",
						NumaNode:    0,
						Size:        3183575302144,
					},
				},
			},
			runOut: []string{
				verStr,
				"---ISetID=0x2aba7f4828ef2ccc---\n" +
					"   PersistentMemoryType=AppDirect\n" +
					"   FreeCapacity=0.0 GiB\n" +
					"---ISetID=0x81187f4881f02ccc---\n" +
					"   PersistentMemoryType=AppDirect\n" +
					"   FreeCapacity=0.0 GiB\n",
			},
			expPrepResp: &storage.ScmPrepareResponse{
				State: storage.ScmStateNoFreeCapacity,
				Namespaces: storage.ScmNamespaces{
					{
						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
						BlockDevice: "pmem1",
						Name:        "namespace1.0",
						NumaNode:    1,
						Size:        3183575302144,
					},
					{
						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1529",
						BlockDevice: "pmem0",
						Name:        "namespace0.0",
						NumaNode:    0,
						Size:        3183575302144,
					},
				},
			},
			expCalls: []string{
				"ipmctl version",
				"ipmctl show -d PersistentMemoryType,FreeCapacity -region",
			},
		},
		"state no free capacity; multiple namespaces per socket; requested number does not match": {
			prepReq: &storage.ScmPrepareRequest{
				NrNamespacesPerSocket: 1,
			},
			scanResp: &storage.ScmScanResponse{
				State: storage.ScmStateNoFreeCapacity,
				Namespaces: storage.ScmNamespaces{
					{
						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
						BlockDevice: "pmem1",
						Name:        "namespace1.0",
						NumaNode:    1,
						Size:        3183575302144,
					},
					{
						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1520",
						BlockDevice: "pmem1.1",
						Name:        "namespace1.1",
						NumaNode:    1,
						Size:        3183575302144,
					},
					{
						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1527",
						BlockDevice: "pmem0",
						Name:        "namespace0.0",
						NumaNode:    0,
						Size:        3183575302144,
					},
					{
						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1529",
						BlockDevice: "pmem0.1",
						Name:        "namespace0.1",
						NumaNode:    0,
						Size:        3183575302144,
					},
				},
			},
			runOut: []string{
				verStr,
				"---ISetID=0x2aba7f4828ef2ccc---\n" +
					"   PersistentMemoryType=AppDirect\n" +
					"   FreeCapacity=0.0 GiB\n" +
					"---ISetID=0x81187f4881f02ccc---\n" +
					"   PersistentMemoryType=AppDirect\n" +
					"   FreeCapacity=0.0 GiB\n",
			},
			expErr: errors.New("want 2 PMem namespaces but got 4"),
			expCalls: []string{
				"ipmctl version",
				"ipmctl show -d PersistentMemoryType,FreeCapacity -region",
			},
		},
		"state no free capacity; multiple namespaces per socket; one region has no capacity": {
			prepReq: &storage.ScmPrepareRequest{
				NrNamespacesPerSocket: 2,
			},
			scanResp: &storage.ScmScanResponse{
				State: storage.ScmStateNoFreeCapacity,
				Namespaces: storage.ScmNamespaces{
					{
						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1527",
						BlockDevice: "pmem0",
						Name:        "namespace0.0",
						NumaNode:    0,
						Size:        3183575302144,
					},
				},
			},
			runOut: []string{
				verStr,
				"---ISetID=0x2aba7f4828ef2ccc---\n" +
					"   PersistentMemoryType=AppDirect\n" +
					"   FreeCapacity=0.0 GiB\n" +
					"---ISetID=0x81187f4881f02ccc---\n" +
					"   PersistentMemoryType=AppDirect\n" +
					"   FreeCapacity=3012.0 GiB\n",
			},
			expErr: errors.New("want 4 PMem namespaces but got 1"),
			expCalls: []string{
				"ipmctl version",
				"ipmctl show -d PersistentMemoryType,FreeCapacity -region",
			},
		},
		"state no free capacity; multiple namespaces per socket": {
			prepReq: &storage.ScmPrepareRequest{
				NrNamespacesPerSocket: 2,
			},
			scanResp: &storage.ScmScanResponse{
				State: storage.ScmStateNoFreeCapacity,
				Namespaces: storage.ScmNamespaces{
					{
						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
						BlockDevice: "pmem1",
						Name:        "namespace1.0",
						NumaNode:    1,
						Size:        3183575302144,
					},
					{
						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1520",
						BlockDevice: "pmem1.1",
						Name:        "namespace1.1",
						NumaNode:    1,
						Size:        3183575302144,
					},
					{
						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1527",
						BlockDevice: "pmem0",
						Name:        "namespace0.0",
						NumaNode:    0,
						Size:        3183575302144,
					},
					{
						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1529",
						BlockDevice: "pmem0.1",
						Name:        "namespace0.1",
						NumaNode:    0,
						Size:        3183575302144,
					},
				},
			},
			runOut: []string{
				verStr,
				"---ISetID=0x2aba7f4828ef2ccc---\n" +
					"   PersistentMemoryType=AppDirect\n" +
					"   FreeCapacity=0.0 GiB\n" +
					"---ISetID=0x81187f4881f02ccc---\n" +
					"   PersistentMemoryType=AppDirect\n" +
					"   FreeCapacity=0.0 GiB\n",
			},
			expPrepResp: &storage.ScmPrepareResponse{
				State: storage.ScmStateNoFreeCapacity,
				Namespaces: storage.ScmNamespaces{
					{
						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
						BlockDevice: "pmem1",
						Name:        "namespace1.0",
						NumaNode:    1,
						Size:        3183575302144,
					},
					{
						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1520",
						BlockDevice: "pmem1.1",
						Name:        "namespace1.1",
						NumaNode:    1,
						Size:        3183575302144,
					},
					{
						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1527",
						BlockDevice: "pmem0",
						Name:        "namespace0.0",
						NumaNode:    0,
						Size:        3183575302144,
					},
					{
						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1529",
						BlockDevice: "pmem0.1",
						Name:        "namespace0.1",
						NumaNode:    0,
						Size:        3183575302144,
					},
				},
			},
			expCalls: []string{
				"ipmctl version",
				"ipmctl show -d PersistentMemoryType,FreeCapacity -region",
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			var calls []string
			var callIdx int

			mockBinding := newMockIpmctl(&mockIpmctlCfg{
				regions:       tc.regions,
				getRegionsErr: tc.regionsErr,
				delGoalsErr:   tc.delGoalsErr,
			})

			mockRun := func(cmd string) (string, error) {
				calls = append(calls, cmd)

				o := verStr
				if callIdx < len(tc.runOut) {
					o = tc.runOut[callIdx]
				}
				var e error = nil
				if callIdx < len(tc.runErr) {
					e = tc.runErr[callIdx]
				}

				log.Debugf("mockRun call %d: ret/err %v/%v", callIdx, o, e)
				callIdx++
				return o, e
			}

			mockLookPath := func(string) (string, error) {
				return "", nil
			}

			cr, err := newCmdRunner(log, mockBinding, mockRun, mockLookPath)
			if err != nil {
				t.Fatal(err)
			}

			if tc.prepReq == nil {
				tc.prepReq = &storage.ScmPrepareRequest{}
			}

			resp, err := cr.prep(*tc.prepReq, tc.scanResp)
			log.Debugf("calls made %+v", calls)
			test.CmpErr(t, tc.expErr, err)

			if diff := cmp.Diff(tc.expPrepResp, resp); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
			if diff := cmp.Diff(tc.expCalls, calls); diff != "" {
				t.Fatalf("unexpected cli calls (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestIpmctl_prepReset(t *testing.T) {
	verStr := "Intel(R) Optane(TM) Persistent Memory Command Line Interface Version 02.00.00.3825"

	for name, tc := range map[string]struct {
		scanResp    *storage.ScmScanResponse
		runOut      string
		runErr      error
		regions     []ipmctl.PMemRegion
		regionsErr  error
		delGoalsErr error
		expErr      error
		expPrepResp *storage.ScmPrepareResponse
		expCalls    []string
	}{
		"state unknown": {
			scanResp: &storage.ScmScanResponse{
				State: storage.ScmStateUnknown,
			},
			expErr: errors.New("unhandled scm state"),
		},
		"state no regions": {
			scanResp: &storage.ScmScanResponse{
				State: storage.ScmStateNoRegions,
			},
			expPrepResp: &storage.ScmPrepareResponse{
				State: storage.ScmStateNoRegions,
			},
		},
		"state regions": {
			scanResp: &storage.ScmScanResponse{
				State: storage.ScmStateFreeCapacity,
			},
			expPrepResp: &storage.ScmPrepareResponse{
				State:          storage.ScmStateFreeCapacity,
				RebootRequired: true,
			},
			expCalls: []string{
				"ipmctl version", "ipmctl create -f -goal MemoryMode=100",
			},
		},
		"state regions; delete goals fails": {
			scanResp: &storage.ScmScanResponse{
				State: storage.ScmStateFreeCapacity,
			},
			delGoalsErr: errors.New("fail"),
			expErr:      errors.New("fail"),
		},
		"state regions; remove regions fails": {
			scanResp: &storage.ScmScanResponse{
				State: storage.ScmStateFreeCapacity,
			},
			runErr: errors.New("cmd failed"),
			expErr: errors.New("cmd failed"),
			expCalls: []string{
				"ipmctl version",
			},
		},
		"state no free capacity": {
			scanResp: &storage.ScmScanResponse{
				State: storage.ScmStateNoFreeCapacity,
				Namespaces: storage.ScmNamespaces{
					{
						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
						BlockDevice: "pmem1",
						Name:        "namespace1.0",
						NumaNode:    1,
						Size:        3183575302144,
					},
				},
			},
			expPrepResp: &storage.ScmPrepareResponse{
				State:          storage.ScmStateNoFreeCapacity,
				RebootRequired: true,
			},
			expCalls: []string{
				"ndctl disable-namespace namespace1.0",
				"ndctl destroy-namespace namespace1.0",
				"ipmctl create -f -goal MemoryMode=100",
			},
		},
		"state no free capacity; remove namespace fails": {
			scanResp: &storage.ScmScanResponse{
				State: storage.ScmStateNoFreeCapacity,
				Namespaces: storage.ScmNamespaces{
					{
						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
						BlockDevice: "pmem1",
						Name:        "namespace1.0",
						NumaNode:    1,
						Size:        3183575302144,
					},
				},
			},
			runErr:   errors.New("cmd failed"),
			expErr:   errors.New("cmd failed"),
			expCalls: []string{"ndctl disable-namespace namespace1.0"},
		},
		"state no free capacity; multiple namespaces per socket": {
			scanResp: &storage.ScmScanResponse{
				State: storage.ScmStateNoFreeCapacity,
				Namespaces: storage.ScmNamespaces{
					{
						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
						BlockDevice: "pmem1",
						Name:        "namespace1.0",
						NumaNode:    1,
						Size:        3183575302144,
					},
					{
						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1520",
						BlockDevice: "pmem1.1",
						Name:        "namespace1.1",
						NumaNode:    1,
						Size:        3183575302144,
					},
					{
						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1527",
						BlockDevice: "pmem0",
						Name:        "namespace0.0",
						NumaNode:    0,
						Size:        3183575302144,
					},
					{
						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1529",
						BlockDevice: "pmem0.1",
						Name:        "namespace0.1",
						NumaNode:    0,
						Size:        3183575302144,
					},
				},
			},
			expPrepResp: &storage.ScmPrepareResponse{
				State:          storage.ScmStateNoFreeCapacity,
				RebootRequired: true,
			},
			expCalls: []string{
				"ndctl disable-namespace namespace1.0",
				"ndctl destroy-namespace namespace1.0",
				"ndctl disable-namespace namespace1.1",
				"ndctl destroy-namespace namespace1.1",
				"ndctl disable-namespace namespace0.0",
				"ndctl destroy-namespace namespace0.0",
				"ndctl disable-namespace namespace0.1",
				"ndctl destroy-namespace namespace0.1",
				"ipmctl create -f -goal MemoryMode=100",
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			var calls []string

			if tc.runOut == "" {
				tc.runOut = verStr
			}

			mockBinding := newMockIpmctl(&mockIpmctlCfg{
				regions:       tc.regions,
				getRegionsErr: tc.regionsErr,
				delGoalsErr:   tc.delGoalsErr,
			})

			mockRun := func(cmd string) (string, error) {
				calls = append(calls, cmd)
				return tc.runOut, tc.runErr
			}

			mockLookPath := func(string) (string, error) {
				return "", nil
			}

			cr, err := newCmdRunner(log, mockBinding, mockRun, mockLookPath)
			if err != nil {
				t.Fatal(err)
			}

			resp, err := cr.prepReset(tc.scanResp)
			test.CmpErr(t, tc.expErr, err)

			if diff := cmp.Diff(tc.expPrepResp, resp); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
			if diff := cmp.Diff(tc.expCalls, calls); diff != "" {
				t.Fatalf("unexpected cli calls (-want, +got):\n%s\n", diff)
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

			test.CmpErr(t, tc.expErr, gotErr)
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
			defer test.ShowBufferOnFailure(t, buf)

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
			cr, err := newCmdRunner(log, mockBinding, mockRun, mockLookPath)
			if err != nil {
				t.Fatal(err)
			}

			if _, err := cr.getModules(); err != nil {
				t.Fatal(err)
			}

			namespaces, err := cr.getNamespaces()
			if err != nil {
				if tt.lookPathErrMsg != "" {
					test.ExpectError(t, err, tt.lookPathErrMsg, tt.desc)
					return
				}
				t.Fatal(tt.desc + ": GetPmemNamespaces: " + err.Error())
			}

			test.AssertEqual(t, commands, tt.expCommands, tt.desc+": unexpected list of commands run")
			test.AssertEqual(t, namespaces, tt.expNamespaces, tt.desc+": unexpected list of pmem device file names")
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
			expErr: errors.New("failed to discover pmem modules: mock GetModules"),
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
			defer test.ShowBufferOnFailure(t, buf)

			mockBinding := newMockIpmctl(tc.cfg)
			cr, err := newCmdRunner(log, mockBinding, nil, nil)
			if err != nil {
				t.Fatal(err)
			}

			result, err := cr.getModules()

			test.CmpErr(t, tc.expErr, err)
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
