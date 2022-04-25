//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package storage

/*
#include "stdlib.h"
#include "daos_srv/control.h"
*/
import "C"

import (
	"context"
	"fmt"
	"math/rand"
	"time"

	"github.com/dustin/go-humanize"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
)

// NvmeDevState constant definitions to represent mock bitset flag combinations.
const (
	MockNvmeStateNew      NvmeDevState = C.NVME_DEV_FL_PLUGGED
	MockNvmeStateNormal   NvmeDevState = MockNvmeStateNew | C.NVME_DEV_FL_INUSE
	MockNvmeStateEvicted  NvmeDevState = MockNvmeStateNormal | C.NVME_DEV_FL_FAULTY
	MockNvmeStateIdentify NvmeDevState = MockNvmeStateNormal | C.NVME_DEV_FL_IDENTIFY
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
	idx := common.GetIndex(varIdx...)
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
	}
}

// MockNvmeNamespace returns struct with examples values.
func MockNvmeNamespace(varIdx ...int32) *NvmeNamespace {
	idx := common.GetIndex(varIdx...)
	return &NvmeNamespace{
		ID:   uint32(idx),
		Size: uint64(humanize.TByte) * uint64(idx+1),
	}
}

// MockSmdDevice returns struct with examples values.
func MockSmdDevice(parentTrAddr string, varIdx ...int32) *SmdDevice {
	idx := common.GetIndex(varIdx...)
	startTgt := (idx * 4) + 1
	return &SmdDevice{
		UUID:      common.MockUUID(idx),
		TargetIDs: []int32{startTgt, startTgt + 1, startTgt + 2, startTgt + 3},
		NvmeState: MockNvmeStateIdentify,
		TrAddr:    parentTrAddr,
	}
}

// MockNvmeController returns struct with examples values.
func MockNvmeController(varIdx ...int32) *NvmeController {
	idx := common.GetIndex(varIdx...)
	pciAddr := concat("0000:80:00", idx, ".")

	return &NvmeController{
		Model:       concat("model", idx),
		Serial:      concat("serial", getRandIdx()),
		PciAddr:     pciAddr,
		FwRev:       concat("fwRev", idx),
		SocketID:    idx % 2,
		HealthStats: MockNvmeHealth(idx),
		Namespaces:  []*NvmeNamespace{MockNvmeNamespace(1)},
		SmdDevices:  []*SmdDevice{MockSmdDevice(pciAddr, idx)},
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
	idx := common.GetIndex(varIdx...)

	return &NvmeAioDevice{
		Path: concat("/tmp/daos-bdev-", idx),
		Size: uint64(humanize.GByte * idx),
	}
}

// MockNvmeAioKdev returns struct representing an emulated NVMe AIO-kdev device.
func MockNvmeAioKdev(varIdx ...int32) *NvmeAioDevice {
	idx := common.GetIndex(varIdx...)

	return &NvmeAioDevice{
		Path: concat("/dev/sda", idx),
		Size: uint64(humanize.GByte * idx),
	}
}

// MockScmModule returns struct with examples values.
func MockScmModule(varIdx ...int32) *ScmModule {
	idx := uint32(common.GetIndex(varIdx...))

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
	idx := common.GetIndex(varIdx...)

	return &ScmMountPoint{
		Class:      ClassDcpm,
		Path:       fmt.Sprintf("/mnt/daos%d", idx),
		DeviceList: []string{fmt.Sprintf("pmem%d", idx)},
		TotalBytes: uint64(humanize.TByte) * uint64(idx+1),
		AvailBytes: uint64(humanize.TByte/4) * uint64(idx+1), // 75% used
	}
}

// MockScmNamespace returns struct with examples values.
// Avoid creating mock with zero sizes.
func MockScmNamespace(varIdx ...int32) *ScmNamespace {
	idx := common.GetIndex(varIdx...)

	return &ScmNamespace{
		UUID:        common.MockUUID(varIdx...),
		BlockDevice: fmt.Sprintf("pmem%d", idx),
		Name:        fmt.Sprintf("namespace%d.0", idx),
		NumaNode:    uint32(idx),
		Size:        uint64(humanize.TByte) * uint64(idx+1),
	}
}

func MockProvider(log logging.Logger, idx int, engineStorage *Config, sys SystemProvider, scm ScmProvider, bdev BdevProvider) *Provider {
	p := DefaultProvider(log, idx, engineStorage)
	p.Sys = sys
	p.scm = scm
	p.bdev = bdev
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
