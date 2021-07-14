//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package storage

import (
	"bytes"
	"fmt"
	"sort"

	"github.com/dustin/go-humanize"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/system"
)

// ScmState represents the probed state of SCM modules on the system.
//go:generate stringer -type=ScmState
type ScmState int

const (
	// ScmStateUnknown represents the default (unknown) state.
	ScmStateUnknown ScmState = iota
	// ScmStateNoRegions indicates that SCM modules exist, but
	// no regions have been created.
	ScmStateNoRegions
	// ScmStateFreeCapacity indicates that SCM modules exist with
	// configured regions that have available capacity.
	ScmStateFreeCapacity
	// ScmStateNoCapacity indicates that SCM modules exist with
	// configured regions but not available capacity.
	ScmStateNoCapacity
)

type (
	// ScmModule represents a SCM DIMM.
	//
	// This is a simplified representation of the raw struct used in the ipmctl package.
	ScmModule struct {
		ChannelID        uint32
		ChannelPosition  uint32
		ControllerID     uint32
		SocketID         uint32
		PhysicalID       uint32
		Capacity         uint64
		UID              string
		PartNumber       string
		FirmwareRevision string
	}

	// ScmModules is a type alias for []ScmModule that implements fmt.Stringer.
	ScmModules []*ScmModule

	// ScmMountPoint represents location SCM filesystem is mounted.
	ScmMountPoint struct {
		Info       string `json:"info"`
		Path       string `json:"path"`
		TotalBytes uint64 `json:"total_bytes"`
		AvailBytes uint64 `json:"avail_bytes"`
	}

	// ScmMountPoints is a type alias for []ScmMountPoint that implements fmt.Stringer.
	ScmMountPoints []*ScmMountPoint

	// ScmNamespace represents a mapping of AppDirect regions to block device files.
	ScmNamespace struct {
		UUID        string         `json:"uuid" hash:"ignore"`
		BlockDevice string         `json:"blockdev"`
		Name        string         `json:"dev"`
		NumaNode    uint32         `json:"numa_node"`
		Size        uint64         `json:"size"`
		Mount       *ScmMountPoint `json:"mount"`
	}

	// ScmNamespaces is a type alias for []ScmNamespace that implements fmt.Stringer.
	ScmNamespaces []*ScmNamespace

	// ScmFirmwareUpdateStatus represents the status of a firmware update on the module.
	ScmFirmwareUpdateStatus uint32

	// ScmFirmwareInfo describes the firmware information of an SCM module.
	ScmFirmwareInfo struct {
		ActiveVersion     string
		StagedVersion     string
		ImageMaxSizeBytes uint32
		UpdateStatus      ScmFirmwareUpdateStatus
	}

	// NvmeHealth represents a set of health statistics for a NVMe device
	// and mirrors C.struct_nvme_stats.
	NvmeHealth struct {
		Timestamp               uint64 `json:"timestamp"`
		TempWarnTime            uint32 `json:"warn_temp_time"`
		TempCritTime            uint32 `json:"crit_temp_time"`
		CtrlBusyTime            uint64 `json:"ctrl_busy_time"`
		PowerCycles             uint64 `json:"power_cycles"`
		PowerOnHours            uint64 `json:"power_on_hours"`
		UnsafeShutdowns         uint64 `json:"unsafe_shutdowns"`
		MediaErrors             uint64 `json:"media_errs"`
		ErrorLogEntries         uint64 `json:"err_log_entries"`
		ReadErrors              uint32 `json:"bio_read_errs"`
		WriteErrors             uint32 `json:"bio_write_errs"`
		UnmapErrors             uint32 `json:"bio_unmap_errs"`
		ChecksumErrors          uint32 `json:"checksum_errs"`
		Temperature             uint32 `json:"temperature"`
		TempWarn                bool   `json:"temp_warn"`
		AvailSpareWarn          bool   `json:"avail_spare_warn"`
		ReliabilityWarn         bool   `json:"dev_reliability_warn"`
		ReadOnlyWarn            bool   `json:"read_only_warn"`
		VolatileWarn            bool   `json:"volatile_mem_warn"`
		ProgFailCntNorm         uint8  `json:"program_fail_cnt_norm"`
		ProgFailCntRaw          uint64 `json:"program_fail_cnt_raw"`
		EraseFailCntNorm        uint8  `json:"erase_fail_cnt_norm"`
		EraseFailCntRaw         uint64 `json:"erase_fail_cnt_raw"`
		WearLevelingCntNorm     uint8  `json:"wear_leveling_cnt_norm"`
		WearLevelingCntMin      uint16 `json:"wear_leveling_cnt_min"`
		WearLevelingCntMax      uint16 `json:"wear_leveling_cnt_max"`
		WearLevelingCntAvg      uint16 `json:"wear_leveling_cnt_avg"`
		EndtoendErrCntRaw       uint64 `json:"endtoend_err_cnt_raw"`
		CrcErrCntRaw            uint64 `json:"crc_err_cnt_raw"`
		MediaWearRaw            uint64 `json:"media_wear_raw"`
		HostReadsRaw            uint64 `json:"host_reads_raw"`
		WorkloadTimerRaw        uint64 `json:"workload_timer_raw"`
		ThermalThrottleStatus   uint8  `json:"thermal_throttle_status"`
		ThermalThrottleEventCnt uint64 `json:"thermal_throttle_event_cnt"`
		RetryBufferOverflowCnt  uint64 `json:"retry_buffer_overflow_cnt"`
		PllLockLossCnt          uint64 `json:"pll_lock_loss_cnt"`
		NandBytesWritten        uint64 `json:"nand_bytes_written"`
		HostBytesWritten        uint64 `json:"host_bytes_written"`
	}

	// NvmeNamespace represents an individual NVMe namespace on a device and
	// mirrors C.struct_ns_t.
	NvmeNamespace struct {
		ID   uint32 `json:"id"`
		Size uint64 `json:"size"`
	}

	// SmdDevice contains DAOS storage device information, including
	// health details if requested.
	SmdDevice struct {
		UUID       string      `json:"uuid"`
		TargetIDs  []int32     `hash:"set" json:"tgt_ids"`
		State      string      `json:"state"`
		Rank       system.Rank `json:"rank"`
		TotalBytes uint64      `json:"total_bytes"`
		AvailBytes uint64      `json:"avail_bytes"`
		Health     *NvmeHealth `json:"health"`
		TrAddr     string      `json:"tr_addr"`
	}

	// NvmeController represents a NVMe device controller which includes health
	// and namespace information and mirrors C.struct_ns_t.
	NvmeController struct {
		Info        string           `json:"info"`
		Model       string           `json:"model"`
		Serial      string           `hash:"ignore" json:"serial"`
		PciAddr     string           `json:"pci_addr"`
		FwRev       string           `json:"fw_rev"`
		SocketID    int32            `json:"socket_id"`
		HealthStats *NvmeHealth      `json:"health_stats"`
		Namespaces  []*NvmeNamespace `hash:"set" json:"namespaces"`
		SmdDevices  []*SmdDevice     `hash:"set" json:"smd_devices"`
	}

	// NvmeControllers is a type alias for []*NvmeController.
	NvmeControllers []*NvmeController
)

const (
	// ScmUpdateStatusUnknown indicates that the firmware update status is unknown.
	ScmUpdateStatusUnknown ScmFirmwareUpdateStatus = iota
	// ScmUpdateStatusStaged indicates that a new firmware version has been staged.
	ScmUpdateStatusStaged
	// ScmUpdateStatusSuccess indicates that the firmware update was successfully applied.
	ScmUpdateStatusSuccess
	// ScmUpdateStatusFailed indicates that the firmware update failed.
	ScmUpdateStatusFailed
)

// String translates the update status to a string
func (s ScmFirmwareUpdateStatus) String() string {
	switch s {
	case ScmUpdateStatusStaged:
		return "Staged"
	case ScmUpdateStatusSuccess:
		return "Success"
	case ScmUpdateStatusFailed:
		return "Failed"
	}
	return "Unknown"
}

func (sm *ScmModule) String() string {
	// capacity given in IEC standard units.
	return fmt.Sprintf("UID:%s PhysicalID:%d Capacity:%s Location:(socket:%d memctrlr:%d "+
		"chan:%d pos:%d)", sm.UID, sm.PhysicalID, humanize.IBytes(sm.Capacity),
		sm.SocketID, sm.ControllerID, sm.ChannelID, sm.ChannelPosition)
}

func (sms ScmModules) String() string {
	var buf bytes.Buffer

	if len(sms) == 0 {
		return "\t\tnone\n"
	}

	sort.Slice(sms, func(i, j int) bool { return sms[i].PhysicalID < sms[j].PhysicalID })

	for _, sm := range sms {
		fmt.Fprintf(&buf, "\t\t%s\n", sm)
	}

	return buf.String()
}

// Capacity reports total storage capacity (bytes) across all modules.
func (sms ScmModules) Capacity() (tb uint64) {
	for _, sm := range sms {
		tb += sm.Capacity
	}
	return
}

// Summary reports total storage space and the number of modules.
//
// Capacity given in IEC standard units.
func (sms ScmModules) Summary() string {
	return fmt.Sprintf("%s (%d %s)", humanize.IBytes(sms.Capacity()), len(sms),
		common.Pluralise("module", len(sms)))
}

// Capacity reports total storage capacity (bytes) of SCM namespace (pmem block device).
func (sn ScmNamespace) Capacity() uint64 {
	return sn.Size
}

// Total returns the total bytes on mounted SCM namespace as reported by OS.
func (sn ScmNamespace) Total() uint64 {
	if sn.Mount == nil {
		return 0
	}
	return sn.Mount.TotalBytes
}

// Free returns the available free bytes on mounted SCM namespace as reported by OS.
func (sn ScmNamespace) Free() uint64 {
	if sn.Mount == nil {
		return 0
	}
	return sn.Mount.AvailBytes
}

// Capacity reports total storage capacity (bytes) across all namespaces.
func (sns ScmNamespaces) Capacity() (tb uint64) {
	for _, sn := range sns {
		tb += (*ScmNamespace)(sn).Capacity()
	}
	return
}

// Total returns the cumulative total bytes on all mounted SCM namespaces.
func (sns ScmNamespaces) Total() (tb uint64) {
	for _, sn := range sns {
		tb += (*ScmNamespace)(sn).Total()
	}
	return
}

// Free returns the cumulative available bytes on all mounted SCM namespaces.
func (sns ScmNamespaces) Free() (tb uint64) {
	for _, sn := range sns {
		tb += (*ScmNamespace)(sn).Free()
	}
	return
}

// PercentUsage returns the percentage of used storage space.
func (sns ScmNamespaces) PercentUsage() string {
	return common.PercentageString(sns.Total()-sns.Free(), sns.Total())
}

// Summary reports total storage space and the number of namespaces.
//
// Capacity given in IEC standard units.
func (sns ScmNamespaces) Summary() string {
	return fmt.Sprintf("%s (%d %s)", humanize.Bytes(sns.Capacity()), len(sns),
		common.Pluralise("namespace", len(sns)))
}

// TempK returns controller temperature in degrees Kelvin.
func (nch *NvmeHealth) TempK() uint32 {
	return uint32(nch.Temperature)
}

// TempC returns controller temperature in degrees Celsius.
func (nch *NvmeHealth) TempC() float32 {
	return float32(nch.Temperature) - 273.15
}

// TempF returns controller temperature in degrees Fahrenheit.
func (nch *NvmeHealth) TempF() float32 {
	return (nch.TempC() * (9.0 / 5.0)) + 32.0
}

// UpdateSmd adds or updates SMD device entry for an NVMe Controller.
func (nc *NvmeController) UpdateSmd(smdDev *SmdDevice) {
	for idx := range nc.SmdDevices {
		if smdDev.UUID == nc.SmdDevices[idx].UUID {
			nc.SmdDevices[idx] = smdDev

			return
		}
	}

	nc.SmdDevices = append(nc.SmdDevices, smdDev)
}

// Capacity returns the cumulative total bytes of all namespace sizes.
func (nc *NvmeController) Capacity() (tb uint64) {
	for _, n := range nc.Namespaces {
		tb += n.Size
	}
	return
}

// Total returns the cumulative total bytes of all blobstore clusters.
func (nc NvmeController) Total() (tb uint64) {
	for _, d := range nc.SmdDevices {
		tb += d.TotalBytes
	}
	return
}

// Free returns the cumulative available bytes of unused blobstore clusters.
func (nc NvmeController) Free() (tb uint64) {
	for _, d := range nc.SmdDevices {
		tb += d.AvailBytes
	}
	return
}

// Capacity returns the cumulative total bytes of all controller capacities.
func (ncs NvmeControllers) Capacity() (tb uint64) {
	for _, c := range ncs {
		tb += (*NvmeController)(c).Capacity()
	}
	return
}

// Total returns the cumulative total bytes of all controller blobstores.
func (ncs NvmeControllers) Total() (tb uint64) {
	for _, c := range ncs {
		tb += (*NvmeController)(c).Total()
	}
	return
}

// Free returns the cumulative available bytes of all blobstore clusters.
func (ncs NvmeControllers) Free() (tb uint64) {
	for _, c := range ncs {
		tb += (*NvmeController)(c).Free()
	}
	return
}

// PercentUsage returns the percentage of used storage space.
func (ncs NvmeControllers) PercentUsage() string {
	return common.PercentageString(ncs.Total()-ncs.Free(), ncs.Total())
}

// Summary reports accumulated storage space and the number of controllers.
func (ncs NvmeControllers) Summary() string {
	return fmt.Sprintf("%s (%d %s)", humanize.Bytes(ncs.Capacity()),
		len(ncs), common.Pluralise("controller", len(ncs)))
}

// Update adds or updates slice of NVMe Controllers.
func (ncs NvmeControllers) Update(ctrlrs ...*NvmeController) NvmeControllers {
	for _, ctrlr := range ctrlrs {
		replaced := false

		for idx, existing := range ncs {
			if ctrlr.PciAddr == existing.PciAddr {
				ncs[idx] = ctrlr
				replaced = true
				continue
			}
		}
		if !replaced {
			ncs = append(ncs, ctrlr)
		}
	}

	return ncs
}
