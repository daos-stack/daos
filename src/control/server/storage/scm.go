//
// (C) Copyright 2021-2022 Intel Corporation.
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
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/pbin"
)

// ScmState represents the probed state of SCM modules on the system.
//
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
		Class      Class    `json:"class"`
		DeviceList []string `json:"device_list"`
		Info       string   `json:"info"`
		Path       string   `json:"path"`
		TotalBytes uint64   `json:"total_bytes"`
		AvailBytes uint64   `json:"avail_bytes"`
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

const (
	ScmMsgRebootRequired     = "A reboot is required to process new SCM memory allocation goals."
	ScmMsgNoModules          = "no SCM modules to prepare"
	ScmMsgNotInited          = "SCM storage could not be accessed"
	ScmMsgClassNotSupported  = "operation unsupported on SCM class"
	ScmMsgIpmctlDiscoverFail = "ipmctl module discovery"
)

type (
	// ScmProvider defines an interface to be implemented by a SCM provider.
	ScmProvider interface {
		Mount(ScmMountRequest) (*ScmMountResponse, error)
		Unmount(ScmMountRequest) (*ScmMountResponse, error)
		Format(ScmFormatRequest) (*ScmFormatResponse, error)
		CheckFormat(ScmFormatRequest) (*ScmFormatResponse, error)
		Scan(ScmScanRequest) (*ScmScanResponse, error)
		GetPmemState() (ScmState, error)
		Prepare(ScmPrepareRequest) (*ScmPrepareResponse, error)
		QueryFirmware(ScmFirmwareQueryRequest) (*ScmFirmwareQueryResponse, error)
		UpdateFirmware(ScmFirmwareUpdateRequest) (*ScmFirmwareUpdateResponse, error)
	}

	// ScmPrepareRequest defines the parameters for a Prepare operation.
	ScmPrepareRequest struct {
		pbin.ForwardableRequest
		// Reset indicates that the operation should reset (clear) DCPM namespaces.
		Reset bool
	}

	// ScmPrepareResponse contains the results of a successful Prepare operation.
	ScmPrepareResponse struct {
		State          ScmState
		RebootRequired bool
		Namespaces     ScmNamespaces
	}

	// ScmScanRequest defines the parameters for a Scan operation.
	ScmScanRequest struct {
		pbin.ForwardableRequest
		Rescan     bool
		DeviceList []string
	}

	// ScmScanResponse contains information gleaned during a successful Scan operation.
	ScmScanResponse struct {
		State      ScmState
		Modules    ScmModules
		Namespaces ScmNamespaces
	}

	// DcpmParams defines the sub-parameters of a Format operation that
	// will use DCPM
	DcpmParams struct {
		Device string
	}

	// RamdiskParams defines the sub-parameters of a Format operation that
	// will use tmpfs-based ramdisk
	RamdiskParams struct {
		Size uint
	}

	// ScmFormatRequest defines the parameters for a Format operation or query.
	ScmFormatRequest struct {
		pbin.ForwardableRequest
		Force      bool
		Mountpoint string
		OwnerUID   int
		OwnerGID   int
		Ramdisk    *RamdiskParams
		Dcpm       *DcpmParams
	}

	// ScmFormatResponse contains the results of a successful Format operation or query.
	ScmFormatResponse struct {
		Mountpoint string
		Formatted  bool
		Mounted    bool
		Mountable  bool
	}

	// ScmMountRequest defines the parameters for a Mount operation.
	ScmMountRequest struct {
		pbin.ForwardableRequest
		Class  Class
		Device string
		Target string
		Size   uint
	}

	// ScmMountResponse contains the results of a successful Mount operation.
	ScmMountResponse struct {
		Target  string
		Mounted bool
	}

	// ScmFirmwareQueryRequest defines the parameters for a firmware query.
	ScmFirmwareQueryRequest struct {
		pbin.ForwardableRequest
		DeviceUIDs  []string // requested device UIDs, empty for all
		ModelID     string   // filter by model ID
		FirmwareRev string   // filter by current FW revision
	}

	// ScmModuleFirmware represents the results of a firmware query for a specific
	// SCM module.
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
	// a specific SCM module.
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
	pf := pbin.NewForwarder(log, pbin.DaosAdminName)

	return &ScmAdminForwarder{
		Forwarder: *pf,
	}
}

// Mount forwards an SCM mount request.
func (f *ScmAdminForwarder) Mount(req ScmMountRequest) (*ScmMountResponse, error) {
	req.Forwarded = true

	res := new(ScmMountResponse)
	if err := f.SendReq("ScmMount", req, res); err != nil {
		return nil, err
	}

	return res, nil
}

// Unmount forwards an SCM unmount request.
func (f *ScmAdminForwarder) Unmount(req ScmMountRequest) (*ScmMountResponse, error) {
	req.Forwarded = true

	res := new(ScmMountResponse)
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

// GetPmemState gets the state of DCPM.
func (f *ScmAdminForwarder) GetPmemState() (ScmState, error) {
	resp, err := f.Scan(ScmScanRequest{})
	if err != nil {
		return ScmStateUnknown, err
	}
	return resp.State, nil
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
