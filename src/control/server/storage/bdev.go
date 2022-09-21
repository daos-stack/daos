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
	"encoding/json"
	"fmt"
	"strings"
	"sync"
	"unsafe"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/pbin"
	"github.com/daos-stack/daos/src/control/system"
)

// BdevPciAddrSep defines the separator used between PCI addresses in string lists.
const (
	BdevPciAddrSep = " "
	NilBdevAddress = "<nil>"
)

// NvmeDevState represents the health state of NVMe device as reported by DAOS engine BIO module.
type NvmeDevState uint32

// NvmeDevState values representing individual bit-flags.
const (
	NvmeStatePlugged  NvmeDevState = C.NVME_DEV_FL_PLUGGED
	NvmeStateInUse    NvmeDevState = C.NVME_DEV_FL_INUSE
	NvmeStateFaulty   NvmeDevState = C.NVME_DEV_FL_FAULTY
	NvmeStateIdentify NvmeDevState = C.NVME_DEV_FL_IDENTIFY
	NvmeStateUnknown  NvmeDevState = C.NVME_DEV_STATE_INVALID
)

// IsNew returns true if SSD is not in use by DAOS.
func (bs NvmeDevState) IsNew() bool {
	return bs&NvmeStatePlugged != 0 && bs&NvmeStateInUse == 0
}

// IsNormal returns true if SSD is in a normal, non-faulty state.
func (bs NvmeDevState) IsNormal() bool {
	return bs&NvmeStatePlugged != 0 && bs&NvmeStateFaulty == 0 && bs&NvmeStateInUse != 0
}

// IsFaulty returns true if SSD is in a faulty state.
func (bs NvmeDevState) IsFaulty() bool {
	return bs&NvmeStatePlugged != 0 && bs&NvmeStateFaulty != 0
}

func (bs NvmeDevState) String() string {
	return C.GoString(C.nvme_state2str(C.int(bs)))
}

// States lists each flag in state bitset.
func (bs NvmeDevState) States() string {
	return fmt.Sprintf("plugged: %v, in-use: %v, faulty: %v, identify: %v",
		bs&C.NVME_DEV_FL_PLUGGED != 0, bs&C.NVME_DEV_FL_INUSE != 0,
		bs&C.NVME_DEV_FL_FAULTY != 0, bs&C.NVME_DEV_FL_IDENTIFY != 0)
}

// Uint32 returns uint32 representation of BIO device state.
func (bs NvmeDevState) Uint32() uint32 {
	return uint32(bs)
}

// NvmeDevStateFromString converts a status string into a state bitset.
func NvmeDevStateFromString(status string) NvmeDevState {
	cStr := C.CString(status)
	defer C.free(unsafe.Pointer(cStr))

	return NvmeDevState(C.nvme_str2state(cStr))

}

// NvmeHealth represents a set of health statistics for a NVMe device
// and mirrors C.struct_nvme_stats.
type NvmeHealth struct {
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
	ClusterSize             uint64 `json:"cluster_size"`
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
	UUID        string       `json:"uuid"`
	TargetIDs   []int32      `hash:"set" json:"tgt_ids"`
	NvmeState   NvmeDevState `json:"-"`
	Rank        system.Rank  `json:"rank"`
	TotalBytes  uint64       `json:"total_bytes"`
	AvailBytes  uint64       `json:"avail_bytes"`
	ClusterSize uint64       `json:"cluster_size"`
	Health      *NvmeHealth  `json:"health"`
	TrAddr      string       `json:"tr_addr"`
}

// MarshalJSON marshals SmdDevice to JSON.
func (sd *SmdDevice) MarshalJSON() ([]byte, error) {
	if sd == nil {
		return nil, errors.New("tried to marshal nil SmdDevice")
	}

	// use a type alias to leverage the default marshal for
	// most fields
	type toJSON SmdDevice
	return json.Marshal(&struct {
		NvmeStateStr string `json:"dev_state"`
		*toJSON
	}{
		NvmeStateStr: sd.NvmeState.String(),
		toJSON:       (*toJSON)(sd),
	})
}

// UnmarshalJSON unmarshals SmaDevice from JSON.
func (sd *SmdDevice) UnmarshalJSON(data []byte) error {
	if string(data) == "null" {
		return nil
	}

	// use a type alias to leverage the default unmarshal for
	// most fields
	type fromJSON SmdDevice
	from := &struct {
		NvmeStateStr string `json:"dev_state"`
		*fromJSON
	}{
		fromJSON: (*fromJSON)(sd),
	}

	if err := json.Unmarshal(data, from); err != nil {
		return err
	}

	sd.NvmeState = NvmeDevStateFromString(from.NvmeStateStr)

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
	SocketID    int32            `json:"socket_id"`
	HealthStats *NvmeHealth      `json:"health_stats"`
	Namespaces  []*NvmeNamespace `hash:"set" json:"namespaces"`
	SmdDevices  []*SmdDevice     `hash:"set" json:"smd_devices"`
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

// NvmeControllers is a type alias for []*NvmeController.
type NvmeControllers []*NvmeController

func (ncs NvmeControllers) String() string {
	var ss []string
	for _, c := range ncs {
		ss = append(ss, c.PciAddr)
	}
	return strings.Join(ss, ", ")
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
		QueryFirmware(NVMeFirmwareQueryRequest) (*NVMeFirmwareQueryResponse, error)
		UpdateFirmware(NVMeFirmwareUpdateRequest) (*NVMeFirmwareUpdateResponse, error)
	}

	// BdevPrepareRequest defines the parameters for a Prepare operation.
	BdevPrepareRequest struct {
		pbin.ForwardableRequest
		HugePageCount      int
		HugeNodes          string
		CleanHugePagesOnly bool
		PCIAllowList       string
		PCIBlockList       string
		TargetUser         string
		Reset_             bool
		DisableVFIO        bool
		EnableVMD          bool
	}

	// BdevPrepareResponse contains the results of a successful Prepare operation.
	BdevPrepareResponse struct {
		NrHugePagesRemoved uint
		VMDPrepared        bool
	}

	// BdevScanRequest defines the parameters for a Scan operation.
	BdevScanRequest struct {
		pbin.ForwardableRequest
		DeviceList  *BdevDeviceList
		VMDEnabled  bool
		BypassCache bool
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
	}

	// BdevFormatRequest defines the parameters for a Format operation.
	BdevFormatRequest struct {
		pbin.ForwardableRequest
		Properties BdevTierProperties
		OwnerUID   int
		OwnerGID   int
		VMDEnabled bool
		Hostname   string
		BdevCache  *BdevScanResponse
	}

	// BdevWriteConfigRequest defines the parameters for a WriteConfig operation.
	BdevWriteConfigRequest struct {
		pbin.ForwardableRequest
		ConfigOutputPath  string
		OwnerUID          int
		OwnerGID          int
		TierProps         []BdevTierProperties
		VMDEnabled        bool
		HotplugEnabled    bool
		HotplugBusidBegin uint8
		HotplugBusidEnd   uint8
		Hostname          string
		BdevCache         *BdevScanResponse
	}

	// BdevWriteConfigResponse contains the result of a WriteConfig operation.
	BdevWriteConfigResponse struct{}

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
// accessing the same references in multiple code paths.
func filterBdevScanResponse(incBdevs *BdevDeviceList, resp *BdevScanResponse) error {
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
				return errors.Wrap(err, "converting pci address of vmd backing device")
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
	resp.Controllers = newCtrlrRefs

	return nil
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
	pf := pbin.NewForwarder(log, pbin.DaosAdminName)

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
