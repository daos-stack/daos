//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package storage

import (
	"fmt"
	"math/rand"
	"time"

	"github.com/dustin/go-humanize"

	"github.com/daos-stack/daos/src/control/common"
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
		TempWarnTime:    uint32(idx),
		TempCritTime:    uint32(idx),
		CtrlBusyTime:    uint64(idx),
		PowerCycles:     uint64(idx),
		PowerOnHours:    uint64(idx),
		UnsafeShutdowns: uint64(idx),
		MediaErrors:     uint64(idx),
		ErrorLogEntries: uint64(idx),
		ReadErrors:      uint32(idx),
		WriteErrors:     uint32(idx),
		UnmapErrors:     uint32(idx),
		ChecksumErrors:  uint32(idx),
		Temperature:     uint32(idx),
		TempWarn:        tWarn,
		AvailSpareWarn:  tWarn,
		ReliabilityWarn: tWarn,
		ReadOnlyWarn:    tWarn,
		VolatileWarn:    tWarn,
	}
}

// MockNvmeNamespace returns struct with examples values.
func MockNvmeNamespace(varIdx ...int32) *NvmeNamespace {
	idx := common.GetIndex(varIdx...)
	return &NvmeNamespace{
		ID:   uint32(idx),
		Size: uint64(idx),
	}
}

// MockSmdDevice returns struct with examples values.
func MockSmdDevice(parentTrAddr string, varIdx ...int32) *SmdDevice {
	idx := common.GetIndex(varIdx...)
	startTgt := (idx * 4) + 1
	return &SmdDevice{
		UUID:      common.MockUUID(idx),
		TargetIDs: []int32{startTgt, startTgt + 1, startTgt + 2, startTgt + 3},
		State:     "NORMAL",
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
		SocketID:    idx,
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

// MockScmModule returns struct with examples values.
func MockScmModule(varIdx ...int32) *ScmModule {
	idx := uint32(common.GetIndex(varIdx...))

	return &ScmModule{
		ChannelID:        idx,
		ChannelPosition:  idx,
		ControllerID:     idx,
		SocketID:         idx,
		PhysicalID:       idx,
		Capacity:         uint64(idx),
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
		Path:       fmt.Sprintf("/mnt/daos%d", idx),
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
		Size:        uint64(idx + 1),
	}
}
