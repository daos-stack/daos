//
// (C) Copyright 2019-2024 Intel Corporation.
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package storage

import (
	"context"
	"fmt"
	"math/rand"
	"sync"
	"time"

	"github.com/dustin/go-humanize"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
)

func concat(base string, idx int32, altSep ...string) string {
	sep := "-"
	if len(altSep) == 1 {
		sep = altSep[0]
	}

	return fmt.Sprintf("%s%s%d", base, sep, idx)
}

func getRandIdx(n ...int32) int32 {
	rand.Seed(time.Now().UnixNano())
	if len(n) > 0 {
		return rand.Int31n(n[0])
	}
	return rand.Int31()
}

// MockNvmeHealth returns struct with examples values.
func MockNvmeHealth(varIdx ...int32) *NvmeHealth {
	idx := test.GetIndex(varIdx...)
	tWarn := false
	if idx > 0 {
		tWarn = true
	}
	return &NvmeHealth{
		TempWarnTime:            uint32(idx),
		TempCritTime:            uint32(idx),
		CtrlBusyTime:            uint64(idx),
		PowerCycles:             uint64(idx),
		PowerOnHours:            uint64(idx),
		UnsafeShutdowns:         uint64(idx),
		MediaErrors:             uint64(idx),
		ErrorLogEntries:         uint64(idx),
		ReadErrors:              uint32(idx),
		WriteErrors:             uint32(idx),
		UnmapErrors:             uint32(idx),
		ChecksumErrors:          uint32(idx),
		Temperature:             uint32(idx),
		TempWarn:                tWarn,
		AvailSpareWarn:          tWarn,
		ReliabilityWarn:         tWarn,
		ReadOnlyWarn:            tWarn,
		VolatileWarn:            tWarn,
		ProgFailCntNorm:         uint8(idx),
		ProgFailCntRaw:          uint64(idx),
		EraseFailCntNorm:        uint8(idx),
		EraseFailCntRaw:         uint64(idx),
		WearLevelingCntNorm:     uint8(idx),
		WearLevelingCntMin:      uint16(idx),
		WearLevelingCntMax:      uint16(idx),
		WearLevelingCntAvg:      uint16(idx),
		EndtoendErrCntRaw:       uint64(idx),
		CrcErrCntRaw:            uint64(idx),
		MediaWearRaw:            uint64(idx),
		HostReadsRaw:            uint64(idx),
		WorkloadTimerRaw:        uint64(idx),
		ThermalThrottleStatus:   uint8(idx),
		ThermalThrottleEventCnt: uint64(idx),
		RetryBufferOverflowCnt:  uint64(idx),
		PllLockLossCnt:          uint64(idx),
		NandBytesWritten:        uint64(idx),
		HostBytesWritten:        uint64(idx),
		LinkPortId:              uint32(idx),
		LinkMaxSpeed:            float32(idx) * 1e+9,
		LinkMaxWidth:            uint32(idx) * 4,
		LinkNegSpeed:            float32(idx) * 1e+9,
		LinkNegWidth:            uint32(idx) * 4,
	}
}

// MockNvmeNamespace returns struct with examples values.
func MockNvmeNamespace(varIdx ...int32) *NvmeNamespace {
	idx := test.GetIndex(varIdx...)
	return &NvmeNamespace{
		ID:   uint32(idx),
		Size: uint64(humanize.TByte) * uint64(idx+1),
	}
}

// MockSmdDevice returns struct with examples values.
func MockSmdDevice(c *NvmeController, varIdx ...int32) *SmdDevice {
	idx := test.GetIndex(varIdx...)
	startTgt := (idx * 4) + 1
	sd := SmdDevice{
		UUID:      test.MockUUID(idx),
		TargetIDs: []int32{startTgt, startTgt + 1, startTgt + 2, startTgt + 3},
		Roles:     BdevRolesFromBits(BdevRoleAll),
	}
	if c != nil {
		sd.Ctrlr = *c
	}
	return &sd
}

// MockNvmeController returns struct with examples values.
func MockNvmeController(varIdx ...int32) *NvmeController {
	idx := test.GetIndex(varIdx...)
	pciAddr := test.MockPCIAddr(idx)

	return &NvmeController{
		Model:       concat("model", idx),
		Serial:      concat("serial", getRandIdx()),
		PciAddr:     pciAddr,
		FwRev:       concat("fwRev", idx),
		SocketID:    idx % 2,
		NvmeState:   NvmeStateNormal,
		LedState:    LedStateNormal,
		HealthStats: MockNvmeHealth(idx),
		Namespaces:  []*NvmeNamespace{MockNvmeNamespace(1)},
	}
}

// MockNvmeControllers returns slice of example NvmeController structs with
// examples values.
func MockNvmeControllers(length int) NvmeControllers {
	result := NvmeControllers{}
	for i := 0; i < length; i++ {
		result = append(result, MockNvmeController(int32(i)))
	}

	return result
}

// MockNvmeAioFile returns struct representing an emulated NVMe AIO-file device.
func MockNvmeAioFile(varIdx ...int32) *NvmeAioDevice {
	idx := test.GetIndex(varIdx...)

	return &NvmeAioDevice{
		Path: concat("/tmp/daos-bdev-", idx),
		Size: uint64(humanize.GByte * idx),
	}
}

// MockNvmeAioKdev returns struct representing an emulated NVMe AIO-kdev device.
func MockNvmeAioKdev(varIdx ...int32) *NvmeAioDevice {
	idx := test.GetIndex(varIdx...)

	return &NvmeAioDevice{
		Path: concat("/dev/sda", idx),
		Size: uint64(humanize.GByte * idx),
	}
}

// MockScmModule returns struct with examples values.
func MockScmModule(varIdx ...int32) *ScmModule {
	idx := uint32(test.GetIndex(varIdx...))

	return &ScmModule{
		ChannelID:        idx,
		ChannelPosition:  idx,
		ControllerID:     idx,
		SocketID:         idx,
		PhysicalID:       idx,
		Capacity:         uint64(humanize.GByte),
		UID:              fmt.Sprintf("Device%d", idx),
		PartNumber:       fmt.Sprintf("PartNumber%d", idx),
		FirmwareRevision: fmt.Sprintf("FWRev%d", idx),
		HealthState:      "Healthy",
	}
}

// MockScmModules returns slice of example ScmModule structs with examples
// values.
func MockScmModules(length int) ScmModules {
	result := ScmModules{}
	for i := 0; i < length; i++ {
		result = append(result, MockScmModule(int32(i)))
	}

	return result
}

// MockScmMountPoint returns struct with examples values.
// Avoid creating mock with zero sizes.
func MockScmMountPoint(varIdx ...int32) *ScmMountPoint {
	idx := test.GetIndex(varIdx...)

	return &ScmMountPoint{
		Class:      ClassDcpm,
		Path:       fmt.Sprintf("/mnt/daos%d", idx),
		DeviceList: []string{fmt.Sprintf("pmem%d", idx)},
		TotalBytes: uint64(humanize.TByte) * uint64(idx+1),
		AvailBytes: uint64(humanize.TByte/4) * uint64(idx+1), // 25% available
		Rank:       ranklist.Rank(uint32(idx)),
	}
}

// MockScmNamespace returns struct with examples values.
// Avoid creating mock with zero sizes.
func MockScmNamespace(varIdx ...int32) *ScmNamespace {
	idx := test.GetIndex(varIdx...)

	return &ScmNamespace{
		UUID:        test.MockUUID(varIdx...),
		BlockDevice: fmt.Sprintf("pmem%d", idx),
		Name:        fmt.Sprintf("namespace%d.0", idx),
		NumaNode:    uint32(idx),
		Size:        uint64(humanize.TByte) * uint64(idx+1),
	}
}

func MockProvider(log logging.Logger, idx int, engineStorage *Config, sys SystemProvider, scm ScmProvider, bdev BdevProvider, meta MetadataProvider) *Provider {
	p := DefaultProvider(log, idx, engineStorage)
	p.Sys = sys
	p.scm = scm
	p.bdev = bdev
	p.metadata = meta
	return p
}

func MockGetTopology(context.Context) (*hardware.Topology, error) {
	return &hardware.Topology{
		NUMANodes: map[uint]*hardware.NUMANode{
			0: hardware.MockNUMANode(0, 14).
				WithPCIBuses(
					[]*hardware.PCIBus{
						{
							LowAddress:  *hardware.MustNewPCIAddress("0000:00:00.0"),
							HighAddress: *hardware.MustNewPCIAddress("0000:07:00.0"),
						},
					},
				),
			1: hardware.MockNUMANode(0, 14).
				WithPCIBuses(
					[]*hardware.PCIBus{
						{
							LowAddress:  *hardware.MustNewPCIAddress("0000:80:00.0"),
							HighAddress: *hardware.MustNewPCIAddress("0000:8f:00.0"),
						},
					},
				),
		},
	}, nil
}

type (
	// MockMountProviderConfig is a configuration for a mock MountProvider.
	MockMountProviderConfig struct {
		MountErr           error
		UnmountErr         error
		IsMountedRes       bool
		IsMountedErr       error
		IsMountedCount     int
		ClearMountpointErr error
		MakeMountPathErr   error
	}

	mountMap struct {
		sync.RWMutex
		mounts map[string]string
	}

	// MockMountProvider is a mocked version of a MountProvider that can be used for testing.
	MockMountProvider struct {
		cfg    *MockMountProviderConfig
		mounts mountMap
	}
)

func (mm *mountMap) Add(target, opts string) {
	mm.Lock()
	defer mm.Unlock()

	if mm.mounts == nil {
		mm.mounts = make(map[string]string)
	}
	mm.mounts[target] = opts
}

func (mm *mountMap) Remove(target string) {
	mm.Lock()
	defer mm.Unlock()

	delete(mm.mounts, target)
}

func (mm *mountMap) Get(target string) (string, bool) {
	mm.RLock()
	defer mm.RUnlock()

	opts, ok := mm.mounts[target]
	return opts, ok
}

// Mount is a mock implementation.
func (m *MockMountProvider) Mount(req MountRequest) (*MountResponse, error) {
	if m.cfg == nil || m.cfg.MountErr == nil {
		m.mounts.Add(req.Target, req.Options)
		return &MountResponse{
			Target:  req.Target,
			Mounted: true,
		}, nil
	}
	return nil, m.cfg.MountErr
}

// Unmount is a mock implementation.
func (m *MockMountProvider) Unmount(req MountRequest) (*MountResponse, error) {
	if m.cfg == nil || m.cfg.UnmountErr == nil {
		m.mounts.Remove(req.Target)
		return &MountResponse{
			Target:  req.Target,
			Mounted: false,
		}, nil
	}
	return nil, m.cfg.UnmountErr
}

// GetMountOpts returns the mount options for the given target.
func (m *MockMountProvider) GetMountOpts(target string) (string, bool) {
	opts, exists := m.mounts.Get(target)
	return opts, exists
}

// IsMounted is a mock implementation.
func (m *MockMountProvider) IsMounted(target string) (bool, error) {
	if m.cfg == nil {
		opts, exists := m.mounts.Get(target)
		return exists && opts != "", nil
	}
	return m.cfg.IsMountedRes, m.cfg.IsMountedErr
}

// ClearMountpoint is a mock implementation.
func (m *MockMountProvider) ClearMountpoint(_ string) error {
	if m.cfg == nil {
		return nil
	}
	return m.cfg.ClearMountpointErr
}

// MakeMountPath is a mock implementation.
func (m *MockMountProvider) MakeMountPath(_ string, _, _ int) error {
	if m.cfg == nil {
		return nil
	}
	return m.cfg.MakeMountPathErr
}

// NewMockMountProvider creates a new MockProvider.
func NewMockMountProvider(cfg *MockMountProviderConfig) *MockMountProvider {
	return &MockMountProvider{
		cfg: cfg,
	}
}

// DefaultMockMountProvider creates a mock provider in which all requests succeed.
func DefaultMockMountProvider() *MockMountProvider {
	return NewMockMountProvider(nil)
}

// MockMetadataProvider defines a mock version of a MetadataProvider.
type MockMetadataProvider struct {
	MountRes       *MountResponse
	MountErr       error
	UnmountRes     *MountResponse
	UnmountErr     error
	FormatErr      error
	NeedsFormatRes bool
	NeedsFormatErr error
}

// Format mocks a MetadataProvider format call.
func (m *MockMetadataProvider) Format(_ MetadataFormatRequest) error {
	return m.FormatErr
}

// Mount mocks a MetadataProvider mount call.
func (m *MockMetadataProvider) Mount(_ MetadataMountRequest) (*MountResponse, error) {
	return m.MountRes, m.MountErr
}

// Unmount mocks a MetadataProvider unmount call.
func (m *MockMetadataProvider) Unmount(MetadataMountRequest) (*MountResponse, error) {
	return m.UnmountRes, m.UnmountErr
}

// NeedsFormat mocks a MetadataProvider format check.
func (m *MockMetadataProvider) NeedsFormat(MetadataFormatRequest) (bool, error) {
	return m.NeedsFormatRes, m.NeedsFormatErr
}

// MockScmProvider defines a mock version of an ScmProvider.
type MockScmProvider struct {
	MountRes          *MountResponse
	MountErr          error
	UnmountRes        *MountResponse
	UnmountErr        error
	FormatRes         *ScmFormatResponse
	FormatErr         error
	CheckFormatRes    *ScmFormatResponse
	CheckFormatErr    error
	ScanRes           *ScmScanResponse
	ScanErr           error
	PrepareRes        *ScmPrepareResponse
	PrepareErr        error
	FirmwareQueryRes  *ScmFirmwareQueryResponse
	FirmwareQueryErr  error
	FirmwareUpdateRes *ScmFirmwareUpdateResponse
	FirmwareUpdateErr error
}

func (m *MockScmProvider) Mount(ScmMountRequest) (*MountResponse, error) {
	return m.MountRes, m.MountErr
}

func (m *MockScmProvider) Unmount(ScmMountRequest) (*MountResponse, error) {
	return m.UnmountRes, m.UnmountErr
}

func (m *MockScmProvider) Format(ScmFormatRequest) (*ScmFormatResponse, error) {
	return m.FormatRes, m.FormatErr
}

func (m *MockScmProvider) CheckFormat(ScmFormatRequest) (*ScmFormatResponse, error) {
	return m.CheckFormatRes, m.CheckFormatErr
}

func (m *MockScmProvider) Scan(ScmScanRequest) (*ScmScanResponse, error) {
	return m.ScanRes, m.ScanErr
}

func (m *MockScmProvider) Prepare(ScmPrepareRequest) (*ScmPrepareResponse, error) {
	return m.PrepareRes, m.PrepareErr
}

func (m *MockScmProvider) QueryFirmware(ScmFirmwareQueryRequest) (*ScmFirmwareQueryResponse, error) {
	return m.FirmwareQueryRes, m.FirmwareQueryErr
}

func (m *MockScmProvider) UpdateFirmware(ScmFirmwareUpdateRequest) (*ScmFirmwareUpdateResponse, error) {
	return m.FirmwareUpdateRes, m.FirmwareUpdateErr
}

type mockBdevProvider struct {
	callCounts         map[string]int
	PrepareErr         error
	PrepareResp        *BdevPrepareResponse
	ScanErr            error
	ScanResp           *BdevScanResponse
	FormatErr          error
	FormatResp         *BdevFormatResponse
	WriteConfigErr     error
	WriteConfigResp    *BdevWriteConfigResponse
	ReadConfigErr      error
	ReadConfigResp     *BdevReadConfigResponse
	QueryFirmwareErr   error
	QueryFirmwareResp  *NVMeFirmwareQueryResponse
	UpdateFirmwareErr  error
	UpdateFirmwareResp *NVMeFirmwareUpdateResponse
}

func (m *mockBdevProvider) addCall(name string) {
	if m.callCounts == nil {
		m.callCounts = make(map[string]int)
	}
	m.callCounts[name]++
}

func (m *mockBdevProvider) Prepare(BdevPrepareRequest) (*BdevPrepareResponse, error) {
	m.addCall("Prepare")
	return m.PrepareResp, m.PrepareErr
}

func (m *mockBdevProvider) Scan(BdevScanRequest) (*BdevScanResponse, error) {
	m.addCall("Scan")
	return m.ScanResp, m.ScanErr
}

func (m *mockBdevProvider) Format(BdevFormatRequest) (*BdevFormatResponse, error) {
	m.addCall("Format")
	return m.FormatResp, m.FormatErr
}

func (m *mockBdevProvider) WriteConfig(BdevWriteConfigRequest) (*BdevWriteConfigResponse, error) {
	m.addCall("WriteConfig")
	return m.WriteConfigResp, m.WriteConfigErr
}

func (m *mockBdevProvider) ReadConfig(BdevReadConfigRequest) (*BdevReadConfigResponse, error) {
	m.addCall("ReadConfig")
	return m.ReadConfigResp, m.ReadConfigErr
}

func (m *mockBdevProvider) QueryFirmware(NVMeFirmwareQueryRequest) (*NVMeFirmwareQueryResponse, error) {
	m.addCall("QueryFirmware")
	return m.QueryFirmwareResp, m.QueryFirmwareErr
}

func (m *mockBdevProvider) UpdateFirmware(NVMeFirmwareUpdateRequest) (*NVMeFirmwareUpdateResponse, error) {
	m.addCall("UpdateFirmware")
	return m.UpdateFirmwareResp, m.UpdateFirmwareErr
}
