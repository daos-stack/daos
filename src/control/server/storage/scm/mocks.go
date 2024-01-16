//
// (C) Copyright 2019-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package scm

import (
	"encoding/xml"
	"math"
	"sync"
	"testing"

	"github.com/daos-stack/daos/src/control/common/proto"
	"github.com/daos-stack/daos/src/control/lib/ipmctl"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/provider/system"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/mount"
)

const (
	defaultMountOpts = "defaults"
)

const testXMLRegions = `<?xml version="1.0"?>
 <RegionList>
  <Region>
   <SocketID>0x0000</SocketID>
   <PersistentMemoryType>AppDirect</PersistentMemoryType>
   <Capacity>1008.000 GiB</Capacity>
   <FreeCapacity>0.000 GiB</FreeCapacity>
   <HealthState>Healthy</HealthState>
   <DimmID>0x0001, 0x0011, 0x0101, 0x0111, 0x0201, 0x0211, 0x0301, 0x0311</DimmID>
   <RegionID>0x0001</RegionID>
   <ISetID>0xb8c12120c7bd1110</ISetID>
  </Region>
 </RegionList>
`

func mockXMLRegions(t *testing.T, variant string) string {
	t.Helper()

	var rl RegionList
	if err := xml.Unmarshal([]byte(testXMLRegions), &rl); err != nil {
		t.Fatal(err)
	}

	switch variant {
	case "sock-zero", "no-free":
	case "sock-one":
		rl.Regions[0].ID = 2
		rl.Regions[0].SocketID = 1
		rl.Regions[0].ISetID++
	case "sock-one-full-free":
		rl.Regions[0].ID = 2
		rl.Regions[0].SocketID = 1
		rl.Regions[0].ISetID++
		rl.Regions[0].FreeCapacity = rl.Regions[0].Capacity
	case "unhealthy":
		rl.Regions[0].Health = regionHealth(ipmctl.RegionHealthError)
	case "not-interleaved":
		rl.Regions[0].PersistentMemoryType = regionType(ipmctl.RegionTypeNotInterleaved)
	case "unknown-memtype":
		rl.Regions[0].PersistentMemoryType = regionType(math.MaxInt32)
	case "part-free":
		rl.Regions[0].FreeCapacity = rl.Regions[0].Capacity / 2
	case "full-free":
		rl.Regions[0].FreeCapacity = rl.Regions[0].Capacity
	case "dual-sock", "dual-sock-no-free":
		rl.Regions = append(rl.Regions, rl.Regions[0])
		rl.Regions[1].ID = 2
		rl.Regions[1].SocketID = 1
		rl.Regions[1].ISetID++
	case "dual-sock-full-free":
		rl.Regions[0].FreeCapacity = rl.Regions[0].Capacity
		rl.Regions = append(rl.Regions, rl.Regions[0])
		rl.Regions[1].ID = 2
		rl.Regions[1].SocketID = 1
		rl.Regions[1].ISetID++
	case "dual-sock-isetid-switch":
		rl.Regions[0].FreeCapacity = rl.Regions[0].Capacity
		rl.Regions = append(rl.Regions, rl.Regions[0])
		rl.Regions[1].ID = 2
		rl.Regions[1].SocketID = 1
		rl.Regions[0].ISetID = 0x04a32120b4fe1110
		rl.Regions[1].ISetID = 0x3a7b2120bb081110
	case "same-sock":
		rl.Regions = append(rl.Regions, rl.Regions[0])
		rl.Regions[1].ISetID++
	case "unhealthy-2nd-sock":
		rl.Regions = append(rl.Regions, rl.Regions[0])
		rl.Regions[1].ID = 2
		rl.Regions[1].SocketID = 1
		rl.Regions[1].Health = regionHealth(ipmctl.RegionHealthError)
	case "full-free-2nd-sock":
		rl.Regions = append(rl.Regions, rl.Regions[0])
		rl.Regions[1].ID = 2
		rl.Regions[1].SocketID = 1
		rl.Regions[1].FreeCapacity = rl.Regions[1].Capacity
	default:
		t.Fatalf("unknown variant %q", variant)
	}

	out, err := xml.Marshal(&rl)
	if err != nil {
		t.Fatal(err)
	}

	return string(out)
}

const (
	// JSON output from "ndctl list -Rv" illustrating mismatch issue.
	// See: https://github.com/pmem/ndctl/issues/235
	ndctlRegionsDual = `[
  {
    "dev":"region1",
    "size":1082331758592,
    "align":16777216,
    "available_size":1082331758592,
    "max_available_extent":1082331758592,
    "type":"pmem",
    "numa_node":0,
    "target_node":3,
    "iset_id":13312958398157623569,
    "persistence_domain":"memory_controller"
  },
  {
    "dev":"region0",
    "size":1082331758592,
    "align":16777216,
    "available_size":1082331758592,
    "max_available_extent":1082331758592,
    "type":"pmem",
    "numa_node":1,
    "target_node":2,
    "iset_id":13312958398157623568,
    "persistence_domain":"memory_controller"
  }
]
`

	// JSON output from "ndctl list -Nv" illustrating mismatch issue.
	// See: https://github.com/pmem/ndctl/issues/235
	ndctlNamespaceDualR1 = `[
  {
    "dev":"namespace1.0",
    "mode":"fsdax",
    "map":"dev",
    "size":532708065280,
    "uuid":"8075b7f1-2c68-45b9-81a9-7bc411ca0743",
    "raw_uuid":"bac5a182-f9c3-49df-93fb-64fb1f7864d4",
    "sector_size":512,
    "align":2097152,
    "blockdev":"pmem1",
    "numa_node":0,
    "target_node":3
  },
  {
    "dev":"namespace1.1",
    "mode":"fsdax",
    "map":"dev",
    "size":532708065280,
    "uuid":"1057916f-b8a2-4aba-bd00-0cc9f843b1d9",
    "raw_uuid":"07df4036-5728-4340-bc6d-b8bd682acd52",
    "sector_size":512,
    "align":2097152,
    "blockdev":"pmem1.1",
    "numa_node":0,
    "target_node":3
  }
]
`

	ndctlNamespaceDualR0 = `[
  {
    "dev":"namespace0.0",
    "mode":"fsdax",
    "map":"dev",
    "size":532708065280,
    "uuid":"8075b7f1-2c68-45b9-81a9-7bc411ca0743",
    "raw_uuid":"bac5a182-f9c3-49df-93fb-64fb1f7864d4",
    "sector_size":512,
    "align":2097152,
    "blockdev":"pmem0",
    "numa_node":1,
    "target_node":2
  },
  {
    "dev":"namespace0.1",
    "mode":"fsdax",
    "map":"dev",
    "size":532708065280,
    "uuid":"1057916f-b8a2-4aba-bd00-0cc9f843b1d9",
    "raw_uuid":"07df4036-5728-4340-bc6d-b8bd682acd52",
    "sector_size":512,
    "align":2097152,
    "blockdev":"pmem0.1",
    "numa_node":1,
    "target_node":2
  }
]
`

	// iset_ids swapped to illustrate mapping regions by it.
	ndctlRegionsSwapISet = `[
  {
    "dev":"region1",
    "size":1082331758592,
    "align":16777216,
    "available_size":1082331758592,
    "max_available_extent":1082331758592,
    "type":"pmem",
    "numa_node":0,
    "target_node":3,
    "iset_id":13312958398157623568,
    "persistence_domain":"memory_controller"
  },
  {
    "dev":"region0",
    "size":1082331758592,
    "align":16777216,
    "available_size":1082331758592,
    "max_available_extent":1082331758592,
    "type":"pmem",
    "numa_node":1,
    "target_node":2,
    "iset_id":13312958398157623569,
    "persistence_domain":"memory_controller"
  }
]
`

	// JSON output from "ndctl list -Nv" illustrating ISetID overflow.
	ndctlRegionsNegISet = `[
  {
    "dev":"region1",
    "size":1082331758592,
    "align":16777216,
    "available_size":1082331758592,
    "max_available_extent":1082331758592,
    "type":"pmem",
    "numa_node":0,
    "target_node":3,
    "iset_id":-1989147235780849392,
    "persistence_domain":"memory_controller"
  },
  {
    "dev":"region0",
    "size":1082331758592,
    "align":16777216,
    "available_size":1082331758592,
    "max_available_extent":1082331758592,
    "type":"pmem",
    "numa_node":1,
    "target_node":2,
    "iset_id":13312958398157623569,
    "persistence_domain":"memory_controller"
  }
]
`
)

type (
	mockIpmctlCfg struct {
		initErr           error
		getModulesErr     error
		modules           []ipmctl.DeviceDiscovery
		delGoalsErr       error
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

func (m *mockIpmctl) Init(_ logging.Logger) error {
	return m.cfg.initErr
}

func (m *mockIpmctl) GetModules(_ logging.Logger) ([]ipmctl.DeviceDiscovery, error) {
	return m.cfg.modules, m.cfg.getModulesErr
}

func (m *mockIpmctl) DeleteConfigGoals(_ logging.Logger) error {
	return m.cfg.delGoalsErr
}

func (m *mockIpmctl) GetRegions(_ logging.Logger) ([]ipmctl.PMemRegion, error) {
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

// mockDiscovery returns a mock SCM module of type exported from ipmctl.
func mockDiscovery(sockID ...int) ipmctl.DeviceDiscovery {
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

// mockModule converts ipmctl type SCM module and returns storage/scm
// internal type.
func mockModule(dIn ...ipmctl.DeviceDiscovery) *storage.ScmModule {
	d := mockDiscovery()
	if len(dIn) > 0 {
		d = dIn[0]
	}

	return &storage.ScmModule{
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

// MockBackendConfig specifies behavior for a mock SCM backend
// implementation providing capability to access and configure
// SCM modules and namespaces.
type MockBackendConfig struct {
	GetModulesRes        storage.ScmModules
	GetModulesErr        error
	GetNamespacesRes     storage.ScmNamespaces
	GetNamespacesErr     error
	PrepRes              *storage.ScmPrepareResponse
	PrepErr              error
	PrepResetRes         *storage.ScmPrepareResponse
	PrepResetErr         error
	GetFirmwareStatusErr error
	GetFirmwareStatusRes *storage.ScmFirmwareInfo
	UpdateFirmwareErr    error
}

type MockBackend struct {
	sync.RWMutex
	cfg                MockBackendConfig
	PrepareCalls       []storage.ScmPrepareRequest
	ResetCalls         []storage.ScmPrepareRequest
	GetModulesCalls    []int
	GetNamespacesCalls []int
}

func (mb *MockBackend) getModules(sockID int) (storage.ScmModules, error) {
	mb.Lock()
	mb.GetModulesCalls = append(mb.GetModulesCalls, sockID)
	mb.Unlock()
	return mb.cfg.GetModulesRes, mb.cfg.GetModulesErr
}

func (mb *MockBackend) getNamespaces(sockID int) (storage.ScmNamespaces, error) {
	mb.Lock()
	mb.GetNamespacesCalls = append(mb.GetNamespacesCalls, sockID)
	mb.Unlock()
	return mb.cfg.GetNamespacesRes, mb.cfg.GetNamespacesErr
}

func (mb *MockBackend) prep(req storage.ScmPrepareRequest, _ *storage.ScmScanResponse) (*storage.ScmPrepareResponse, error) {
	mb.Lock()
	mb.PrepareCalls = append(mb.PrepareCalls, req)
	mb.Unlock()

	if mb.cfg.PrepErr != nil {
		return nil, mb.cfg.PrepErr
	} else if mb.cfg.PrepRes == nil {
		return &storage.ScmPrepareResponse{
			Socket: &storage.ScmSocketState{},
		}, nil
	}
	return mb.cfg.PrepRes, mb.cfg.PrepErr
}

func (mb *MockBackend) prepReset(req storage.ScmPrepareRequest, _ *storage.ScmScanResponse) (*storage.ScmPrepareResponse, error) {
	mb.Lock()
	mb.ResetCalls = append(mb.ResetCalls, req)
	mb.Unlock()

	if mb.cfg.PrepResetErr != nil {
		return nil, mb.cfg.PrepResetErr
	} else if mb.cfg.PrepResetRes == nil {
		return &storage.ScmPrepareResponse{
			Socket: &storage.ScmSocketState{},
		}, nil
	}
	return mb.cfg.PrepResetRes, mb.cfg.PrepResetErr
}

func (mb *MockBackend) GetFirmwareStatus(deviceUID string) (*storage.ScmFirmwareInfo, error) {
	return mb.cfg.GetFirmwareStatusRes, mb.cfg.GetFirmwareStatusErr
}

func (mb *MockBackend) UpdateFirmware(deviceUID string, firmwarePath string) error {
	return mb.cfg.UpdateFirmwareErr
}

func NewMockBackend(cfg *MockBackendConfig) *MockBackend {
	if cfg == nil {
		cfg = &MockBackendConfig{}
	}
	return &MockBackend{
		cfg: *cfg,
	}
}

func DefaultMockBackend() *MockBackend {
	return NewMockBackend(nil)
}

func NewMockProvider(log logging.Logger, mbc *MockBackendConfig, msc *system.MockSysConfig) *Provider {
	sysProv := system.NewMockSysProvider(log, msc)
	mountProv := mount.NewProvider(log, sysProv)
	return NewProvider(log, NewMockBackend(mbc), sysProv, mountProv)
}

func DefaultMockProvider(log logging.Logger) *Provider {
	sysProv := system.DefaultMockSysProvider(log)
	mountProv := mount.NewProvider(log, sysProv)
	return NewProvider(log, DefaultMockBackend(), sysProv, mountProv)
}
