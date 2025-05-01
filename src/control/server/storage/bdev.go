//
// (C) Copyright 2019-2024 Intel Corporation.
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package storage

import (
	"context"
	"encoding/json"
	"fmt"
	"strconv"
	"strings"
	"sync"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/pbin"
)

/*
#include "stdlib.h"
#include "daos_srv/control.h"
*/
import "C"

// BdevPciAddrSep defines the separator used between PCI addresses in string lists.
const (
	BdevPciAddrSep = " "
	NilBdevAddress = "<nil>"
	sysXSTgtID     = 1024
	// Minimum amount of hugepage memory (in bytes) needed for each target.
	memHugepageMinPerTarget = 1 << 30 // 1GiB

	// DefaultMemoryFileRatio (mem_size:meta_size) describes the behavior of MD-on-SSD in
	// phase-1 mode where the per-target-meta-blob size is equal to the per-target-VOS-file
	// size. In phase-2 mode where the per-target-meta-blob size is greater than
	// per-target-VOS-file size, the memory file ratio will be less than one.
	DefaultMemoryFileRatio = 1.0
)

// JSON config file constants.
const (
	ConfBdevSetOptions           = "bdev_set_options"
	ConfBdevNvmeSetOptions       = "bdev_nvme_set_options"
	ConfBdevNvmeSetHotplug       = "bdev_nvme_set_hotplug"
	ConfBdevAioCreate            = "bdev_aio_create"
	ConfBdevNvmeAttachController = C.NVME_CONF_ATTACH_CONTROLLER
	ConfVmdEnable                = C.NVME_CONF_ENABLE_VMD
	ConfSetHotplugBusidRange     = C.NVME_CONF_SET_HOTPLUG_RANGE
	ConfSetAccelProps            = C.NVME_CONF_SET_ACCEL_PROPS
	ConfSetSpdkRpcServer         = C.NVME_CONF_SET_SPDK_RPC_SERVER
	ConfSetAutoFaultyProps       = C.NVME_CONF_SET_AUTO_FAULTY
)

// Acceleration related constants for engine setting and optional capabilities.
const (
	AccelEngineNone  = C.NVME_ACCEL_NONE
	AccelEngineSPDK  = C.NVME_ACCEL_SPDK
	AccelEngineDML   = C.NVME_ACCEL_DML
	AccelOptMoveFlag = C.NVME_ACCEL_FLAG_MOVE
	AccelOptCRCFlag  = C.NVME_ACCEL_FLAG_CRC
)

// Role assignments for NVMe SSDs related to type of storage (enables Metadata-on-SSD capability).
const (
	BdevRoleData = C.NVME_ROLE_DATA
	BdevRoleMeta = C.NVME_ROLE_META
	BdevRoleWAL  = C.NVME_ROLE_WAL
	BdevRoleAll  = BdevRoleData | BdevRoleMeta | BdevRoleWAL
)

// NvmeDevState represents the operation state of an NVMe device.
type NvmeDevState int32

// NvmeDevState values representing the operational device state.
const (
	NvmeStateUnknown NvmeDevState = iota
	NvmeStateNormal
	NvmeStateNew
	NvmeStateFaulty
	NvmeStateUnplugged
)

func (nds NvmeDevState) String() string {
	ndss, ok := ctlpb.NvmeDevState_name[int32(nds)]
	if !ok {
		return "UNKNOWN"
	}
	return strings.ToUpper(ndss)
}

func (nds NvmeDevState) MarshalJSON() ([]byte, error) {
	stateStr, ok := ctlpb.NvmeDevState_name[int32(nds)]
	if !ok {
		return nil, errors.Errorf("invalid nvme dev state %d", nds)
	}
	return []byte(`"` + strings.ToUpper(stateStr) + `"`), nil
}

func (nds *NvmeDevState) UnmarshalJSON(data []byte) error {
	stateStr := strings.Trim(strings.ToUpper(string(data)), "\"")

	state, ok := ctlpb.NvmeDevState_value[stateStr]
	if !ok {
		// Try converting the string to an int32, to handle the
		// conversion from protobuf message using convert.Types().
		si, err := strconv.ParseInt(stateStr, 0, 32)
		if err != nil {
			return errors.Errorf("invalid nvme dev state number parse %q", stateStr)
		}

		if _, ok = ctlpb.NvmeDevState_name[int32(si)]; !ok {
			return errors.Errorf("invalid nvme dev state name lookup %q", stateStr)
		}
		state = int32(si)
	}
	*nds = NvmeDevState(state)

	return nil
}

// LedState represents the LED state of device.
type LedState int32

// LedState values representing the VMD LED state (see src/proto/ctl/smd.proto).
const (
	LedStateUnknown LedState = iota
	LedStateIdentify
	LedStateFaulty
	LedStateRebuild
	LedStateNormal
)

func (vls LedState) String() string {
	vlss, ok := ctlpb.LedState_name[int32(vls)]
	if !ok {
		return "UNKNOWN"
	}
	return strings.ToUpper(vlss)
}

func (vls LedState) MarshalJSON() ([]byte, error) {
	stateStr, ok := ctlpb.LedState_name[int32(vls)]
	if !ok {
		return nil, errors.Errorf("invalid vmd led state %d", vls)
	}
	return []byte(`"` + strings.ToUpper(stateStr) + `"`), nil
}

func (vls *LedState) UnmarshalJSON(data []byte) error {
	stateStr := strings.Trim(strings.ToUpper(string(data)), "\"")

	state, ok := ctlpb.LedState_value[stateStr]
	if !ok {
		// Try converting the string to an int32, to handle the
		// conversion from protobuf message using convert.Types().
		si, err := strconv.ParseInt(stateStr, 0, 32)
		if err != nil {
			return errors.Errorf("invalid vmd led state number parse %q", stateStr)
		}

		if _, ok = ctlpb.LedState_name[int32(si)]; !ok {
			return errors.Errorf("invalid vmd led state name lookup %q", stateStr)
		}
		state = int32(si)
	}
	*vls = LedState(state)

	return nil
}

// NvmeHealth represents a set of health statistics for a NVMe device
// and mirrors C.struct_nvme_stats.
type NvmeHealth struct {
	Timestamp               uint64  `json:"timestamp"`
	TempWarnTime            uint32  `json:"warn_temp_time"`
	TempCritTime            uint32  `json:"crit_temp_time"`
	CtrlBusyTime            uint64  `json:"ctrl_busy_time"`
	PowerCycles             uint64  `json:"power_cycles"`
	PowerOnHours            uint64  `json:"power_on_hours"`
	UnsafeShutdowns         uint64  `json:"unsafe_shutdowns"`
	MediaErrors             uint64  `json:"media_errs"`
	ErrorLogEntries         uint64  `json:"err_log_entries"`
	ReadErrors              uint32  `json:"bio_read_errs"`
	WriteErrors             uint32  `json:"bio_write_errs"`
	UnmapErrors             uint32  `json:"bio_unmap_errs"`
	ChecksumErrors          uint32  `json:"checksum_errs"`
	Temperature             uint32  `json:"temperature"`
	TempWarn                bool    `json:"temp_warn"`
	AvailSpareWarn          bool    `json:"avail_spare_warn"`
	ReliabilityWarn         bool    `json:"dev_reliability_warn"`
	ReadOnlyWarn            bool    `json:"read_only_warn"`
	VolatileWarn            bool    `json:"volatile_mem_warn"`
	ProgFailCntNorm         uint8   `json:"program_fail_cnt_norm"`
	ProgFailCntRaw          uint64  `json:"program_fail_cnt_raw"`
	EraseFailCntNorm        uint8   `json:"erase_fail_cnt_norm"`
	EraseFailCntRaw         uint64  `json:"erase_fail_cnt_raw"`
	WearLevelingCntNorm     uint8   `json:"wear_leveling_cnt_norm"`
	WearLevelingCntMin      uint16  `json:"wear_leveling_cnt_min"`
	WearLevelingCntMax      uint16  `json:"wear_leveling_cnt_max"`
	WearLevelingCntAvg      uint16  `json:"wear_leveling_cnt_avg"`
	EndtoendErrCntRaw       uint64  `json:"endtoend_err_cnt_raw"`
	CrcErrCntRaw            uint64  `json:"crc_err_cnt_raw"`
	MediaWearRaw            uint64  `json:"media_wear_raw"`
	HostReadsRaw            uint64  `json:"host_reads_raw"`
	WorkloadTimerRaw        uint64  `json:"workload_timer_raw"`
	ThermalThrottleStatus   uint8   `json:"thermal_throttle_status"`
	ThermalThrottleEventCnt uint64  `json:"thermal_throttle_event_cnt"`
	RetryBufferOverflowCnt  uint64  `json:"retry_buffer_overflow_cnt"`
	PllLockLossCnt          uint64  `json:"pll_lock_loss_cnt"`
	NandBytesWritten        uint64  `json:"nand_bytes_written"`
	HostBytesWritten        uint64  `json:"host_bytes_written"`
	ClusterSize             uint64  `json:"cluster_size"`
	MetaWalSize             uint64  `json:"meta_wal_size"`
	RdbWalSize              uint64  `json:"rdb_wal_size"`
	LinkPortId              uint32  `json:"link_port_id"`
	LinkMaxSpeed            float32 `json:"link_max_speed"`
	LinkMaxWidth            uint32  `json:"link_max_width"`
	LinkNegSpeed            float32 `json:"link_neg_speed"`
	LinkNegWidth            uint32  `json:"link_neg_width"`
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

// NvmeNamespace represents an individual NVMe namespace on a device and
// mirrors C.struct_ns_t.
type NvmeNamespace struct {
	ID   uint32 `json:"id"`
	Size uint64 `json:"size"`
}

// SmdDevice contains DAOS storage device information, including
// health details if requested.
type SmdDevice struct {
	UUID             string         `json:"uuid"`
	TargetIDs        []int32        `hash:"set" json:"tgt_ids"`
	Rank             ranklist.Rank  `json:"rank"`
	TotalBytes       uint64         `json:"total_bytes"`
	AvailBytes       uint64         `json:"avail_bytes"`
	UsableBytes      uint64         `json:"usable_bytes"`
	ClusterSize      uint64         `json:"cluster_size"`
	MetaSize         uint64         `json:"meta_size"`
	MetaWalSize      uint64         `json:"meta_wal_size"`
	RdbSize          uint64         `json:"rdb_size"`
	RdbWalSize       uint64         `json:"rdb_wal_size"`
	Roles            BdevRoles      `json:"roles"`
	HasSysXS         bool           `json:"has_sys_xs"`
	Ctrlr            NvmeController `json:"ctrlr"`
	CtrlrNamespaceID uint32         `json:"ctrlr_namespace_id"`
}

func (sd *SmdDevice) String() string {
	if sd == nil {
		return "<nil>"
	}
	return fmt.Sprintf("%+v", *sd)
}

// MarshalJSON handles the special case where native SmdDevice converts BdevRoles to bitmask in
// proto SmdDevice type. Native BdevRoles type en/decodes to/from human readable JSON strings.
func (sd *SmdDevice) MarshalJSON() ([]byte, error) {
	if sd == nil {
		return nil, errors.New("tried to marshal nil SmdDevice")
	}

	type toJSON SmdDevice
	return json.Marshal(&struct {
		RoleBits uint32 `json:"role_bits"`
		*toJSON
	}{
		RoleBits: uint32(sd.Roles.OptionBits),
		toJSON:   (*toJSON)(sd),
	})
}

// UnmarshalJSON handles the special case where proto SmdDevice converts BdevRoles bitmask to
// native BdevRoles type. Native BdevRoles type en/decodes to/from human readable JSON strings.
func (sd *SmdDevice) UnmarshalJSON(data []byte) error {
	if string(data) == "null" {
		return nil
	}

	type fromJSON SmdDevice
	from := &struct {
		RoleBits uint32 `json:"role_bits"`
		*fromJSON
	}{
		fromJSON: (*fromJSON)(sd),
	}

	if err := json.Unmarshal(data, from); err != nil {
		return err
	}

	if from.Roles.IsEmpty() && from.RoleBits != 0 {
		sd.Roles.OptionBits = OptionBits(from.RoleBits)
	}

	// Handle any duplicate target IDs and set flag instead of sysXS target ID.
	seen := make(map[int32]bool)
	newTgts := make([]int32, 0, len(sd.TargetIDs))
	for _, i := range sd.TargetIDs {
		if !seen[i] {
			if i == sysXSTgtID {
				sd.HasSysXS = true
			} else {
				newTgts = append(newTgts, i)
			}
			seen[i] = true
		}
	}
	sd.TargetIDs = newTgts

	return nil
}

// NvmeController represents a NVMe device controller which includes health
// and namespace information and mirrors C.struct_ns_t.
type NvmeController struct {
	Info        string           `json:"info"`
	Model       string           `json:"model"`
	Serial      string           `hash:"ignore" json:"serial"`
	PciAddr     string           `json:"pci_addr"`
	FwRev       string           `json:"fw_rev"`
	VendorID    string           `json:"vendor_id"`
	PciType     string           `json:"pci_type"`
	SocketID    int32            `json:"socket_id"`
	HealthStats *NvmeHealth      `json:"health_stats"`
	Namespaces  []*NvmeNamespace `hash:"set" json:"namespaces"`
	SmdDevices  []*SmdDevice     `hash:"set" json:"smd_devices"`
	NvmeState   NvmeDevState     `json:"dev_state"`
	LedState    LedState         `json:"led_state"`
}

// UpdateSmd adds or updates SMD device entry for an NVMe Controller.
func (nc *NvmeController) UpdateSmd(newDev *SmdDevice) {
	if nc == nil {
		return
	}
	for _, exstDev := range nc.SmdDevices {
		if newDev.UUID == exstDev.UUID {
			*exstDev = *newDev
			return
		}
	}

	nc.SmdDevices = append(nc.SmdDevices, newDev)
}

// Capacity returns the cumulative total bytes of all namespace sizes.
func (nc *NvmeController) Capacity() (tb uint64) {
	if nc == nil {
		return 0
	}
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

// Usable returns the cumulative usable bytes of blobstore clusters. This is a projected data
// capacity calculated whilst taking into account future pool metadata overheads.
func (nc NvmeController) Usable() (tb uint64) {
	for _, d := range nc.SmdDevices {
		tb += d.UsableBytes
	}
	return
}

// Roles returns bdev_roles for NVMe controller being used in MD-on-SSD mode. Assume that all SMD
// devices on a controller have the same roles.
func (nc *NvmeController) Roles() *BdevRoles {
	if len(nc.SmdDevices) > 0 {
		return &nc.SmdDevices[0].Roles
	}

	return &BdevRoles{}
}

// Rank returns rank on which this NVMe controller is being used. Assume that all SMD devices on a
// controller have the same rank.
func (nc *NvmeController) Rank() ranklist.Rank {
	if len(nc.SmdDevices) > 0 {
		return nc.SmdDevices[0].Rank
	}

	return ranklist.NilRank
}

// NvmeControllers is a type alias for []*NvmeController.
type NvmeControllers []*NvmeController

func (ncs NvmeControllers) String() string {
	var ss []string
	for _, c := range ncs {
		s := c.PciAddr
		for _, sd := range c.SmdDevices {
			s += fmt.Sprintf("-nsid%d-%s", sd.CtrlrNamespaceID, sd.Roles.String())
		}
		ss = append(ss, s)
	}
	return strings.Join(ss, ", ")
}

// Len returns the length of the NvmeController reference slice.
func (ncs NvmeControllers) Len() int {
	return len(ncs)
}

// Capacity returns the cumulative total bytes of all controller capacities.
func (ncs NvmeControllers) Capacity() (tb uint64) {
	for _, c := range ncs {
		tb += c.Capacity()
	}
	return
}

// Total returns the cumulative total bytes of all controller blobstores.
func (ncs NvmeControllers) Total() (tb uint64) {
	for _, c := range ncs {
		tb += c.Total()
	}
	return
}

// Free returns the cumulative available bytes of all blobstore clusters.
func (ncs NvmeControllers) Free() (tb uint64) {
	for _, c := range ncs {
		tb += c.Free()
	}
	return
}

// Usable returns the cumulative usable bytes of all blobstore clusters. This is a projected data
// capacity calculated whilst taking into account future pool metadata overheads.
func (ncs NvmeControllers) Usable() (tb uint64) {
	for _, c := range ncs {
		tb += c.Usable()
	}
	return
}

// Summary reports accumulated storage space and the number of controllers.
// Storage capacity printed with SI (decimal representation) units.
func (ncs NvmeControllers) Summary() string {
	return fmt.Sprintf("%s (%d %s)", humanize.Bytes(ncs.Capacity()),
		len(ncs), common.Pluralise("controller", len(ncs)))
}

// Update adds or updates slice of NVMe Controllers.
func (ncs *NvmeControllers) Update(ctrlrs ...NvmeController) {
	if ncs == nil {
		return
	}

	for _, ctrlr := range ctrlrs {
		replaced := false
		for _, existing := range *ncs {
			if ctrlr.PciAddr == existing.PciAddr {
				*existing = ctrlr
				replaced = true
				break
			}
		}
		if !replaced {
			newCtrlr := ctrlr
			*ncs = append(*ncs, &newCtrlr)
		}
	}
}

// Addresses returns a hardware.PCIAddressSet pointer to controller addresses.
func (ncs NvmeControllers) Addresses() (*hardware.PCIAddressSet, error) {
	pas := hardware.MustNewPCIAddressSet()
	for _, c := range ncs {
		if err := pas.AddStrings(c.PciAddr); err != nil {
			return nil, err
		}
	}
	return pas, nil
}

// HaveMdOnSsdRoles returns true if bdev MD-on-SSD roles are configured on NVMe SSDs.
func (ncs NvmeControllers) HaveMdOnSsdRoles() bool {
	if ncs.Len() > 0 && !ncs[0].Roles().IsEmpty() {
		return true
	}
	return false
}

// NvmeAioDevice returns struct representing an emulated NVMe AIO device (file or kdev).
type NvmeAioDevice struct {
	Path string `json:"path"`
	Size uint64 `json:"size"` // in unit of bytes
}

type (
	// BdevProvider defines an interface to be implemented by a Block Device provider.
	BdevProvider interface {
		Prepare(BdevPrepareRequest) (*BdevPrepareResponse, error)
		Scan(BdevScanRequest) (*BdevScanResponse, error)
		Format(BdevFormatRequest) (*BdevFormatResponse, error)
		WriteConfig(BdevWriteConfigRequest) (*BdevWriteConfigResponse, error)
		ReadConfig(BdevReadConfigRequest) (*BdevReadConfigResponse, error)
		QueryFirmware(NVMeFirmwareQueryRequest) (*NVMeFirmwareQueryResponse, error)
		UpdateFirmware(NVMeFirmwareUpdateRequest) (*NVMeFirmwareUpdateResponse, error)
	}

	// BdevPrepareRequest defines the parameters for a Prepare operation.
	BdevPrepareRequest struct {
		pbin.ForwardableRequest
		HugepageCount      int
		HugeNodes          string
		CleanHugepagesOnly bool
		PCIAllowList       string
		PCIBlockList       string
		TargetUser         string
		Reset_             bool
		DisableVFIO        bool
		EnableVMD          bool
	}

	// BdevPrepareResponse contains the results of a successful Prepare operation.
	BdevPrepareResponse struct {
		NrHugepagesRemoved uint
		VMDPrepared        bool
	}

	// BdevScanRequest defines the parameters for a Scan operation.
	BdevScanRequest struct {
		pbin.ForwardableRequest
		DeviceList *BdevDeviceList
		VMDEnabled bool
	}

	// BdevScanResponse contains information gleaned during a successful Scan operation.
	BdevScanResponse struct {
		Controllers NvmeControllers
		VMDEnabled  bool
	}

	// BdevTierProperties contains basic configuration properties of a bdev tier.
	BdevTierProperties struct {
		Class          Class
		DeviceList     *BdevDeviceList
		DeviceFileSize uint64 // size in bytes for NVMe device emulation
		Tier           int
		DeviceRoles    BdevRoles // NVMe SSD role assignments
	}

	// BdevFormatRequest defines the parameters for a Format operation.
	BdevFormatRequest struct {
		pbin.ForwardableRequest
		Properties   BdevTierProperties
		OwnerUID     int
		OwnerGID     int
		Hostname     string
		VMDEnabled   bool
		ScannedBdevs NvmeControllers // VMD needs address mapping for backing devices.
	}

	// BdevWriteConfigRequest defines the parameters for a WriteConfig operation.
	BdevWriteConfigRequest struct {
		pbin.ForwardableRequest
		ConfigOutputPath  string
		OwnerUID          int
		OwnerGID          int
		TierProps         []BdevTierProperties
		HotplugEnabled    bool
		HotplugBusidBegin uint8
		HotplugBusidEnd   uint8
		Hostname          string
		AccelProps        AccelProps
		SpdkRpcSrvProps   SpdkRpcServer
		AutoFaultyProps   BdevAutoFaulty
		VMDEnabled        bool
		ScannedBdevs      NvmeControllers // VMD needs address mapping for backing devices.
	}

	// BdevWriteConfigResponse contains the result of a WriteConfig operation.
	BdevWriteConfigResponse struct{}

	// BdevReadConfigRequest defines the parameters for a ReadConfig operation.
	BdevReadConfigRequest struct {
		pbin.ForwardableRequest
		ConfigPath string
	}

	// BdevReadConfigResponse contains the result of a ReadConfig operation.
	BdevReadConfigResponse struct{}

	// BdevDeviceFormatRequest designs the parameters for a device-specific format.
	BdevDeviceFormatRequest struct {
		Device string
		Class  Class
	}

	// BdevDeviceFormatResponse contains device-specific Format operation results.
	BdevDeviceFormatResponse struct {
		Formatted bool
		Error     *fault.Fault
	}

	// BdevDeviceFormatResponses is a map of device identifiers to device Format results.
	BdevDeviceFormatResponses map[string]*BdevDeviceFormatResponse

	// BdevFormatResponse contains the results of a Format operation.
	BdevFormatResponse struct {
		DeviceResponses BdevDeviceFormatResponses
	}

	// NVMeFirmwareQueryRequest defines the parameters for a Nvme firmware query.
	NVMeFirmwareQueryRequest struct {
		pbin.ForwardableRequest
		DeviceAddrs []string // requested device PCI addresses, empty for all
		ModelID     string   // filter devices by model ID
		FirmwareRev string   // filter devices by current FW revision
	}

	// NVMeDeviceFirmwareQueryResult represents the result of a firmware query for
	// a specific NVMe controller.
	NVMeDeviceFirmwareQueryResult struct {
		Device NvmeController
	}

	// NVMeFirmwareQueryResponse contains the results of the firmware query.
	NVMeFirmwareQueryResponse struct {
		Results []NVMeDeviceFirmwareQueryResult
	}

	// NVMeFirmwareUpdateRequest defines the parameters for a firmware update.
	NVMeFirmwareUpdateRequest struct {
		pbin.ForwardableRequest
		DeviceAddrs  []string // requested device PCI addresses, empty for all
		FirmwarePath string   // location of the firmware binary
		ModelID      string   // filter devices by model ID
		FirmwareRev  string   // filter devices by current FW revision
	}

	// NVMeDeviceFirmwareUpdateResult represents the result of a firmware update for
	// a specific NVMe controller.
	NVMeDeviceFirmwareUpdateResult struct {
		Device NvmeController
		Error  string
	}

	// NVMeFirmwareUpdateResponse contains the results of the firmware update.
	NVMeFirmwareUpdateResponse struct {
		Results []NVMeDeviceFirmwareUpdateResult
	}
)

// getNumaNodeBusidRange sets range parameters in the input request either to user configured
// values if provided in the server config file, or automatically derive them by querying
// hardware configuration.
func getNumaNodeBusidRange(ctx context.Context, getTopology topologyGetter, numaNodeIdx uint) (uint8, uint8, error) {
	topo, err := getTopology(ctx)
	if err != nil {
		return 0, 0, err
	}
	if topo == nil {
		return 0, 0, errors.New("nil topology")
	}

	// Take the lowest and highest buses in all of the ranges for an engine following the
	// assumption that bus-IDs assigned to each NUMA node will always be contiguous.
	// TODO: add each range individually if unsure about assumption

	nodes := topo.NUMANodes
	if len(nodes) <= int(numaNodeIdx) {
		return 0, 0, errors.Errorf("insufficient numa nodes in topology, want %d got %d",
			numaNodeIdx+1, len(nodes))
	}

	buses := nodes[numaNodeIdx].PCIBuses
	if len(buses) == 0 {
		return 0, 0, errors.Errorf("no PCI buses found on numa node %d in topology",
			numaNodeIdx)
	}

	lowAddr := topo.NUMANodes[numaNodeIdx].PCIBuses[0].LowAddress
	highAddr := topo.NUMANodes[numaNodeIdx].PCIBuses[len(buses)-1].HighAddress

	return lowAddr.Bus, highAddr.Bus, nil
}

// filterBdevScanResponse removes controllers that are not in the input list from the scan response.
// As the response contains controller references which may be shared elsewhere, copy them to avoid
// accessing the same references in multiple code paths and return a new BdevScanResponse objecst.
func filterBdevScanResponse(incBdevs *BdevDeviceList, resp *BdevScanResponse) (*BdevScanResponse, error) {
	oldCtrlrRefs := resp.Controllers
	newCtrlrRefs := make(NvmeControllers, 0, len(oldCtrlrRefs))

	for _, oldCtrlrRef := range oldCtrlrRefs {
		addr, err := hardware.NewPCIAddress(oldCtrlrRef.PciAddr)
		if err != nil {
			continue // If we cannot parse the address, leave it out.
		}

		if addr.IsVMDBackingAddress() {
			vmdAddr, err := addr.BackingToVMDAddress()
			if err != nil {
				return nil, errors.Wrap(err, "convert pci address of vmd backing device")
			}
			// If addr is a VMD backing address, use the VMD endpoint instead as that is the
			// address that will be in the config.
			addr = vmdAddr
		}

		// Retain controller details if address is in config.
		if incBdevs.Contains(addr) {
			newCtrlrRef := &NvmeController{}
			*newCtrlrRef = *oldCtrlrRef
			newCtrlrRefs = append(newCtrlrRefs, newCtrlrRef)
		}
	}

	return &BdevScanResponse{
		Controllers: newCtrlrRefs,
		VMDEnabled:  resp.VMDEnabled,
	}, nil
}

type BdevForwarder struct {
	BdevAdminForwarder
	NVMeFirmwareForwarder
}

func NewBdevForwarder(log logging.Logger) *BdevForwarder {
	return &BdevForwarder{
		BdevAdminForwarder:    *NewBdevAdminForwarder(log),
		NVMeFirmwareForwarder: *NewNVMeFirmwareForwarder(log),
	}
}

type BdevAdminForwarder struct {
	pbin.Forwarder
	reqMutex sync.Mutex
}

func NewBdevAdminForwarder(log logging.Logger) *BdevAdminForwarder {
	pf := pbin.NewForwarder(log, pbin.DaosPrivHelperName)

	return &BdevAdminForwarder{
		Forwarder: *pf,
	}
}

func (f *BdevAdminForwarder) SendReq(method string, fwdReq interface{}, fwdRes interface{}) error {
	f.reqMutex.Lock()
	defer f.reqMutex.Unlock()

	return f.Forwarder.SendReq(method, fwdReq, fwdRes)
}

func (f *BdevAdminForwarder) Scan(req BdevScanRequest) (*BdevScanResponse, error) {
	req.Forwarded = true

	res := new(BdevScanResponse)
	if err := f.SendReq("BdevScan", req, res); err != nil {
		return nil, err
	}

	return res, nil
}

func (f *BdevAdminForwarder) Prepare(req BdevPrepareRequest) (*BdevPrepareResponse, error) {
	req.Forwarded = true

	res := new(BdevPrepareResponse)
	if err := f.SendReq("BdevPrepare", req, res); err != nil {
		return nil, err
	}

	return res, nil
}

func (f *BdevAdminForwarder) Format(req BdevFormatRequest) (*BdevFormatResponse, error) {
	req.Forwarded = true

	res := new(BdevFormatResponse)
	if err := f.SendReq("BdevFormat", req, res); err != nil {
		return nil, err
	}

	return res, nil
}

func (f *BdevAdminForwarder) WriteConfig(req BdevWriteConfigRequest) (*BdevWriteConfigResponse, error) {
	req.Forwarded = true

	res := new(BdevWriteConfigResponse)
	if err := f.SendReq("BdevWriteConfig", req, res); err != nil {
		return nil, err
	}

	return res, nil
}

func (f *BdevAdminForwarder) ReadConfig(req BdevReadConfigRequest) (*BdevReadConfigResponse, error) {
	req.Forwarded = true

	res := new(BdevReadConfigResponse)
	if err := f.SendReq("BdevReadConfig", req, res); err != nil {
		return nil, err
	}

	return res, nil
}

const (
	// NVMeFirmwareQueryMethod is the name of the method used to forward the request to
	// update NVMe device firmware.
	NVMeFirmwareQueryMethod = "NvmeFirmwareQuery"

	// NVMeFirmwareUpdateMethod is the name of the method used to forward the request to
	// update NVMe device firmware.
	NVMeFirmwareUpdateMethod = "NvmeFirmwareUpdate"
)

// NVMeFirmwareForwarder forwards firmware requests to a privileged binary.
type NVMeFirmwareForwarder struct {
	pbin.Forwarder
}

// NewNVMeFirmwareForwarder returns a new bdev FirmwareForwarder.
func NewNVMeFirmwareForwarder(log logging.Logger) *NVMeFirmwareForwarder {
	pf := pbin.NewForwarder(log, pbin.DaosFWName)

	return &NVMeFirmwareForwarder{
		Forwarder: *pf,
	}
}

// checkSupport verifies that the firmware support binary is installed.
func (f *NVMeFirmwareForwarder) checkSupport() error {
	if f.CanForward() {
		return nil
	}

	return errors.Errorf("NVMe firmware operations are not supported on this system")
}

// QueryFirmware forwards a request to query firmware on the NVMe device.
func (f *NVMeFirmwareForwarder) QueryFirmware(req NVMeFirmwareQueryRequest) (*NVMeFirmwareQueryResponse, error) {
	if err := f.checkSupport(); err != nil {
		return nil, err
	}
	req.Forwarded = true

	res := new(NVMeFirmwareQueryResponse)
	if err := f.SendReq(NVMeFirmwareQueryMethod, req, res); err != nil {
		return nil, err
	}

	return res, nil
}

// UpdateFirmware forwards a request to update firmware on the NVMe device.
func (f *NVMeFirmwareForwarder) UpdateFirmware(req NVMeFirmwareUpdateRequest) (*NVMeFirmwareUpdateResponse, error) {
	if err := f.checkSupport(); err != nil {
		return nil, err
	}
	req.Forwarded = true

	res := new(NVMeFirmwareUpdateResponse)
	if err := f.SendReq(NVMeFirmwareUpdateMethod, req, res); err != nil {
		return nil, err
	}

	return res, nil
}

// CalcMinHugepages returns the minimum number of hugepages that should be
// requested for the given number of targets.
func CalcMinHugepages(hugepageSizeKb int, numTargets int) (int, error) {
	if numTargets < 1 {
		return 0, errors.New("numTargets must be > 0")
	}

	hugepageSizeBytes := hugepageSizeKb * humanize.KiByte // KiB to B
	if hugepageSizeBytes == 0 {
		return 0, errors.New("invalid system hugepage size")
	}
	minHugeMem := memHugepageMinPerTarget * numTargets

	return minHugeMem / hugepageSizeBytes, nil
}
