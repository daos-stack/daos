//
// (C) Copyright 2021-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package storage

import (
	"bytes"
	"fmt"
	"sort"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/pbin"
)

// ScmState represents the probed state of PMem modules on the system.
//
//go:generate stringer -type=ScmState
type ScmState int

const (
	// ScmStateUnknown represents the default (unknown) state.
	ScmStateUnknown ScmState = iota
	// ScmNoRegions indicates that PMem modules exist, but no regions have been created.
	ScmNoRegions
	// ScmFreeCap indicates that PMem AppDirect regions have free capacity.
	ScmFreeCap
	// ScmNoFreeCap indicates that PMem AppDirect regions have no free capacity.
	ScmNoFreeCap
	// ScmNotInterleaved indicates that a PMem AppDirect region is in non-interleaved mode.
	ScmNotInterleaved
	// ScmNoModules indicates that no PMem modules exist.
	ScmNoModules
	// ScmNotHealthy indicates a PMem AppDirect region is showing health state as "Error".
	ScmNotHealthy
	// ScmPartFreeCap indicates a PMem AppDirect region has only partial free capacity.
	ScmPartFreeCap
	// ScmUnknownMode indicates a pMem AppDirect region is in an unsupported memory mode.
	ScmUnknownMode

	// MinRamdiskMem is the minimum amount of memory needed for each engine's tmpfs RAM-disk.
	MinRamdiskMem = humanize.GiByte * 4
)

// Memory reservation constant defaults to be used when calculating RAM-disk size for DAOS I/O engine.
const (
	DefaultSysMemRsvd    = humanize.GiByte * 16  // per-system
	DefaultTgtMemRsvd    = humanize.MiByte * 128 // per-engine-target
	DefaultEngineMemRsvd = humanize.GiByte * 1   // per-engine
)

func (ss ScmState) String() string {
	if val, exists := map[ScmState]string{
		ScmStateUnknown:   "Unknown",
		ScmNoRegions:      "NoRegions",
		ScmFreeCap:        "FreeCapacity",
		ScmNoFreeCap:      "NoFreeCapacity",
		ScmNotInterleaved: "NotInterleaved",
		ScmNoModules:      "NoModules",
		ScmNotHealthy:     "NotHealthy",
		ScmPartFreeCap:    "PartialFreeCapacity",
		ScmUnknownMode:    "UnknownMode",
	}[ss]; exists {
		return val
	}
	return "Unknown"
}

type (
	// ScmSocketState indicates the state of PMem for either a specific socket or all sockets.
	ScmSocketState struct {
		SocketID *uint // If set, state applies to a specific socket.
		State    ScmState
	}

	// ScmModule represents a PMem DIMM.
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

	// ScmMountPoint represents location PMem filesystem is mounted.
	ScmMountPoint struct {
		Class       Class         `json:"class"`
		DeviceList  []string      `json:"device_list"`
		Info        string        `json:"info"`
		Path        string        `json:"path"`
		Rank        ranklist.Rank `json:"rank"`
		TotalBytes  uint64        `json:"total_bytes"`
		AvailBytes  uint64        `json:"avail_bytes"`
		UsableBytes uint64        `json:"usable_bytes"`
	}

	// ScmMountPoints is a type alias for []ScmMountPoint that implements fmt.Stringer.
	ScmMountPoints []*ScmMountPoint

	// ScmNamespace is a block device exposing a PMem AppDirect region.
	ScmNamespace struct {
		UUID        string         `json:"uuid" hash:"ignore"`
		BlockDevice string         `json:"blockdev"`
		Name        string         `json:"dev"`
		NumaNode    uint32         `json:"numa_node"`
		Size        uint64         `json:"size"`
		Mount       *ScmMountPoint `json:"mount"`
	}

	// ScmNamespaces is a type alias for a slice of ScmNamespace references.
	ScmNamespaces []*ScmNamespace

	// ScmFirmwareUpdateStatus represents the status of a firmware update on the module.
	ScmFirmwareUpdateStatus uint32

	// ScmFirmwareInfo describes the firmware information of an PMem module.
	ScmFirmwareInfo struct {
		ActiveVersion     string
		StagedVersion     string
		ImageMaxSizeBytes uint32
		UpdateStatus      ScmFirmwareUpdateStatus
	}
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

// Capacity reports total storage capacity (bytes) of PMem namespace (pmem block device).
func (sn ScmNamespace) Capacity() uint64 {
	return sn.Size
}

// Total returns the total bytes on mounted PMem namespace as reported by OS.
func (sn ScmNamespace) Total() uint64 {
	if sn.Mount == nil {
		return 0
	}
	return sn.Mount.TotalBytes
}

// Free returns the available free bytes on mounted PMem namespace as reported by OS.
func (sn ScmNamespace) Free() uint64 {
	if sn.Mount == nil {
		return 0
	}
	return sn.Mount.AvailBytes
}

// Free returns the available free bytes on mounted PMem namespace as reported by OS.
func (sn ScmNamespace) Usable() uint64 {
	if sn.Mount == nil {
		return 0
	}
	return sn.Mount.UsableBytes
}

// Capacity reports total storage capacity (bytes) across all namespaces.
func (sns ScmNamespaces) Capacity() (tb uint64) {
	for _, sn := range sns {
		tb += (*ScmNamespace)(sn).Capacity()
	}
	return
}

// Total returns the cumulative total bytes on all mounted PMem namespaces.
func (sns ScmNamespaces) Total() (tb uint64) {
	for _, sn := range sns {
		tb += (*ScmNamespace)(sn).Total()
	}
	return
}

// Free returns the cumulative available bytes on all mounted PMem namespaces.
func (sns ScmNamespaces) Free() (tb uint64) {
	for _, sn := range sns {
		tb += (*ScmNamespace)(sn).Free()
	}
	return
}

// Free returns the cumulative effective available bytes on all mounted PMem namespaces.
func (sns ScmNamespaces) Usable() (tb uint64) {
	for _, sn := range sns {
		tb += (*ScmNamespace)(sn).Usable()
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

const (
	ScmMsgRebootRequired     = "A reboot is required to process new PMem memory allocation goals."
	ScmMsgNotInited          = "PMem storage could not be accessed"
	ScmMsgClassNotSupported  = "operation unsupported on PMem class"
	ScmMsgIpmctlDiscoverFail = "ipmctl module discovery"
)

type (
	// ScmProvider defines an interface to be implemented by a PMem provider.
	ScmProvider interface {
		Mount(ScmMountRequest) (*MountResponse, error)
		Unmount(ScmMountRequest) (*MountResponse, error)
		Format(ScmFormatRequest) (*ScmFormatResponse, error)
		CheckFormat(ScmFormatRequest) (*ScmFormatResponse, error)
		Scan(ScmScanRequest) (*ScmScanResponse, error)
		Prepare(ScmPrepareRequest) (*ScmPrepareResponse, error)
		QueryFirmware(ScmFirmwareQueryRequest) (*ScmFirmwareQueryResponse, error)
		UpdateFirmware(ScmFirmwareUpdateRequest) (*ScmFirmwareUpdateResponse, error)
	}

	// ScmPrepareRequest defines the parameters for a Prepare operation.
	ScmPrepareRequest struct {
		pbin.ForwardableRequest
		Reset                 bool  // Clear PMem namespaces and regions.
		NrNamespacesPerSocket uint  // Request this many PMem namespaces per socket.
		SocketID              *uint // Only process PMem attached to this socket.
	}

	// ScmPrepareResponse contains the results of a successful Prepare operation.
	ScmPrepareResponse struct {
		Socket         *ScmSocketState
		RebootRequired bool
		Namespaces     ScmNamespaces
	}

	// ScmScanRequest defines the parameters for a Scan operation.
	ScmScanRequest struct {
		pbin.ForwardableRequest
		SocketID *uint // Only process PMem attached to this socket.
	}

	// ScmScanResponse contains information gleaned during a successful Scan operation.
	ScmScanResponse struct {
		Modules    ScmModules
		Namespaces ScmNamespaces
	}

	// RamdiskParams defines the sub-parameters of a Format or Mount operation that
	// will use tmpfs-based ramdisk
	RamdiskParams struct {
		Size             uint
		NUMANode         uint
		DisableHugepages bool
	}

	// ScmFormatRequest defines the parameters for a Format operation or query.
	ScmFormatRequest struct {
		pbin.ForwardableRequest
		Force      bool
		Mountpoint string
		OwnerUID   int
		OwnerGID   int
		Ramdisk    *RamdiskParams
		Dcpm       *DeviceParams
	}

	// ScmFormatResponse contains the results of a successful Format operation or query.
	ScmFormatResponse struct {
		Mountpoint string
		Formatted  bool
		Mounted    bool
		Mountable  bool
	}

	// ScmMountRequest represents an SCM mount request.
	ScmMountRequest struct {
		pbin.ForwardableRequest
		Class   Class
		Device  string
		Target  string
		Ramdisk *RamdiskParams
	}

	// ScmFirmwareQueryRequest defines the parameters for a firmware query.
	ScmFirmwareQueryRequest struct {
		pbin.ForwardableRequest
		DeviceUIDs  []string // requested device UIDs, empty for all
		ModelID     string   // filter by model ID
		FirmwareRev string   // filter by current FW revision
	}

	// ScmModuleFirmware represents the results of a firmware query for a specific
	// PMem module.
	ScmModuleFirmware struct {
		Module ScmModule
		Info   *ScmFirmwareInfo
		Error  string
	}

	// ScmFirmwareQueryResponse contains the results of a successful firmware query.
	ScmFirmwareQueryResponse struct {
		Results []ScmModuleFirmware
	}

	// ScmFirmwareUpdateRequest defines the parameters for a firmware update.
	ScmFirmwareUpdateRequest struct {
		pbin.ForwardableRequest
		DeviceUIDs   []string // requested device UIDs, empty for all
		FirmwarePath string   // location of the firmware binary
		ModelID      string   // filter devices by model ID
		FirmwareRev  string   // filter devices by current FW revision
	}

	// ScmFirmwareUpdateResult represents the result of a firmware update for
	// a specific PMem module.
	ScmFirmwareUpdateResult struct {
		Module ScmModule
		Error  string
	}

	// ScmFirmwareUpdateResponse contains the results of the firmware update.
	ScmFirmwareUpdateResponse struct {
		Results []ScmFirmwareUpdateResult
	}
)

type ScmForwarder struct {
	ScmAdminForwarder
	ScmFwForwarder
}

func NewScmForwarder(log logging.Logger) *ScmForwarder {
	return &ScmForwarder{
		ScmAdminForwarder: *NewScmAdminForwarder(log),
		ScmFwForwarder:    *NewScmFwForwarder(log),
	}
}

// ScmAdminForwarder forwards requests to the DAOS admin binary.
type ScmAdminForwarder struct {
	pbin.Forwarder
}

// NewScmAdminForwarder creates a new ScmAdminForwarder.
func NewScmAdminForwarder(log logging.Logger) *ScmAdminForwarder {
	pf := pbin.NewForwarder(log, pbin.DaosPrivHelperName)

	return &ScmAdminForwarder{
		Forwarder: *pf,
	}
}

// Mount forwards an SCM mount request.
func (f *ScmAdminForwarder) Mount(req ScmMountRequest) (*MountResponse, error) {
	req.Forwarded = true

	res := new(MountResponse)
	if err := f.SendReq("ScmMount", req, res); err != nil {
		return nil, err
	}

	return res, nil
}

// Unmount forwards an SCM unmount request.
func (f *ScmAdminForwarder) Unmount(req ScmMountRequest) (*MountResponse, error) {
	req.Forwarded = true

	res := new(MountResponse)
	if err := f.SendReq("ScmUnmount", req, res); err != nil {
		return nil, err
	}

	return res, nil
}

// Format forwards a request request to format SCM.
func (f *ScmAdminForwarder) Format(req ScmFormatRequest) (*ScmFormatResponse, error) {
	req.Forwarded = true

	res := new(ScmFormatResponse)
	if err := f.SendReq("ScmFormat", req, res); err != nil {
		return nil, err
	}

	return res, nil
}

// CheckFormat forwards a request to check the SCM formatting.
func (f *ScmAdminForwarder) CheckFormat(req ScmFormatRequest) (*ScmFormatResponse, error) {
	req.Forwarded = true

	res := new(ScmFormatResponse)
	if err := f.SendReq("ScmCheckFormat", req, res); err != nil {
		return nil, err
	}

	return res, nil
}

// Scan forwards an SCM scan request.
func (f *ScmAdminForwarder) Scan(req ScmScanRequest) (*ScmScanResponse, error) {
	req.Forwarded = true

	res := new(ScmScanResponse)
	if err := f.SendReq("ScmScan", req, res); err != nil {
		return nil, err
	}

	return res, nil
}

// Prepare forwards a request to prep the SCM.
func (f *ScmAdminForwarder) Prepare(req ScmPrepareRequest) (*ScmPrepareResponse, error) {
	req.Forwarded = true

	res := new(ScmPrepareResponse)
	if err := f.SendReq("ScmPrepare", req, res); err != nil {
		return nil, err
	}

	return res, nil
}

const (
	// ScmFirmwareQueryMethod is the method name used when forwarding the request
	// to query SCM firmware.
	ScmFirmwareQueryMethod = "ScmFirmwareQuery"
	// ScmFirmwareUpdateMethod is the method name used when forwarding the request
	// to update SCM firmware.
	ScmFirmwareUpdateMethod = "ScmFirmwareUpdate"
)

// ScmFwForwarder forwards firmware requests to a privileged binary.
type ScmFwForwarder struct {
	pbin.Forwarder
}

// NewScmFwForwarder returns a new ScmFwForwarder.
func NewScmFwForwarder(log logging.Logger) *ScmFwForwarder {
	pf := pbin.NewForwarder(log, pbin.DaosFWName)

	return &ScmFwForwarder{
		Forwarder: *pf,
	}
}

// checkSupport verifies that the firmware support binary is installed.
func (f *ScmFwForwarder) checkSupport() error {
	if f.Forwarder.CanForward() {
		return nil
	}

	return errors.Errorf("SCM firmware operations are not supported on this system")
}

// Query forwards an SCM firmware query request.
func (f *ScmFwForwarder) QueryFirmware(req ScmFirmwareQueryRequest) (*ScmFirmwareQueryResponse, error) {
	if err := f.checkSupport(); err != nil {
		return nil, err
	}
	req.Forwarded = true

	res := new(ScmFirmwareQueryResponse)
	if err := f.SendReq(ScmFirmwareQueryMethod, req, res); err != nil {
		return nil, err
	}

	return res, nil
}

// Update forwards a request to update firmware on the SCM.
func (f *ScmFwForwarder) UpdateFirmware(req ScmFirmwareUpdateRequest) (*ScmFirmwareUpdateResponse, error) {
	if err := f.checkSupport(); err != nil {
		return nil, err
	}
	req.Forwarded = true

	res := new(ScmFirmwareUpdateResponse)
	if err := f.SendReq(ScmFirmwareUpdateMethod, req, res); err != nil {
		return nil, err
	}

	return res, nil
}

// CalcRamdiskSize returns recommended tmpfs RAM-disk size calculated as
// (total mem - hugepage mem - sys rsvd mem - engine rsvd mem) / nr engines.
// All values in units of bytes and return value is for a single RAM-disk/engine.
func CalcRamdiskSize(log logging.Logger, memTotal, memHuge, memSys uint64, tgtCount, engCount int) (uint64, error) {
	if memTotal == 0 {
		return 0, errors.New("requires nonzero total mem")
	}
	if tgtCount <= 0 {
		return 0, errors.New("requires positive nonzero nr engine targets")
	}
	if engCount <= 0 {
		return 0, errors.New("requires positive nonzero nr engines")
	}

	memEng := uint64(tgtCount) * DefaultTgtMemRsvd
	if memEng < DefaultEngineMemRsvd {
		memEng = DefaultEngineMemRsvd
	}

	msgStats := fmt.Sprintf("mem stats: total %s (%d) - (hugepages %s + sys rsvd %s + "+
		"(engine rsvd %s * nr engines %d). %d tgts-per-engine)", humanize.IBytes(memTotal),
		memTotal, humanize.IBytes(memHuge), humanize.IBytes(memSys),
		humanize.IBytes(memEng), engCount, tgtCount)

	memRsvd := memHuge + memSys + (memEng * uint64(engCount))
	if memTotal < memRsvd {
		return 0, errors.Errorf("insufficient ram to meet minimum requirements (%s)",
			msgStats)
	}

	ramdiskSize := (memTotal - memRsvd) / uint64(engCount)

	log.Debugf("ram-disk size %s calculated using %s", humanize.IBytes(ramdiskSize), msgStats)

	return ramdiskSize, nil
}

// CalcMemForRamdiskSize returns the minimum RAM required for the input requested RAM-disk size.
func CalcMemForRamdiskSize(log logging.Logger, ramdiskSize, memHuge, memSys uint64, tgtCount, engCount int) (uint64, error) {
	if ramdiskSize == 0 {
		return 0, errors.New("requires nonzero ram-disk size")
	}
	if tgtCount <= 0 {
		return 0, errors.New("requires positive nonzero nr engine targets")
	}
	if engCount == 0 {
		return 0, errors.New("requires nonzero nr engines")
	}

	memEng := uint64(tgtCount) * DefaultTgtMemRsvd
	if memEng < DefaultEngineMemRsvd {
		memEng = DefaultEngineMemRsvd
	}

	msgStats := fmt.Sprintf("required ram-disk size %s (%d). mem hugepage: %s, nr engines: %d, "+
		"sys mem rsvd: %s, engine mem rsvd: %s, %d tgts-per-engine",
		humanize.IBytes(ramdiskSize), ramdiskSize, humanize.IBytes(memHuge), engCount,
		humanize.IBytes(memSys), humanize.IBytes(memEng), tgtCount)

	memRsvd := memHuge + memSys + (memEng * uint64(engCount))
	memReqd := memRsvd + (ramdiskSize * uint64(engCount))

	log.Debugf("%s RAM needed for %s", humanize.IBytes(memReqd), msgStats)

	return memReqd, nil
}
