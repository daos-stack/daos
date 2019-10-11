//
// (C) Copyright 2019 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//
package scm

import (
	"fmt"
	"os"
	"os/exec"
	"strings"
	"sync"

	"github.com/pkg/errors"

	types "github.com/daos-stack/daos/src/control/common/storage"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/provider/system"
)

const (
	defaultUnmountFlags = 0
	defaultMountFlags   = 0

	defaultMountPointPerms = 0700

	parseFsUnformatted = "data"

	fsTypeNone  = "none"
	fsTypeExt4  = "ext4"
	fsTypeTmpfs = "tmpfs"

	dcpmFsType    = fsTypeExt4
	dcpmMountOpts = "dax"

	ramFsType = fsTypeTmpfs
)

type (
	// Module represents a SCM DIMM.
	//
	// This is a simplified representation of the raw struct used in the ipmctl package.
	Module struct {
		ChannelID       uint32
		ChannelPosition uint32
		ControllerID    uint32
		SocketID        uint32
		PhysicalID      uint32
		Capacity        uint64
	}

	// Namespace represents a mapping between AppDirect regions and block device files.
	Namespace struct {
		UUID        string
		BlockDevice string
		Name        string
		NumaNode    uint32 `json:"numa_node"`
	}
)

type (
	// PrepareRequest defines the parameters for a Prepare opration.
	PrepareRequest struct {
		// Reset indicates that the operation should reset (clear) DCPM namespaces.
		Reset bool
	}
	// PrepareResponse contains the results of a successful Prepare operation.
	PrepareResponse struct {
		State          types.ScmState
		RebootRequired bool
		Namespaces     []Namespace
	}

	// DcpmParams defines the sub-parameters of a Format operation that
	// will use DCPM storage.
	DcpmParams struct {
		Device string
	}
	// RamdiskParams defines the sub-parameters of a Format operation that
	// will use tmpfs-based ramdisk storage.
	RamdiskParams struct {
		Size uint
	}
	// FormatRequest defines the parameters for a Format operation or query.
	FormatRequest struct {
		Reformat   bool
		Mountpoint string
		Ramdisk    *RamdiskParams
		Dcpm       *DcpmParams
	}
	// FormatResponse contains the results of a successful Format operation or query.
	FormatResponse struct {
		Mountpoint string
		Formatted  bool
	}

	// MountReqeust defines the parameters for a Mount operation.
	MountRequest struct {
		Source string
		Target string
		FsType string
		Flags  uintptr
		Data   string
	}
	// MountResponse contains the results of a successful Mount operation.
	MountResponse struct {
		Target  string
		Mounted bool
	}

	// UpdateRequest defines the parameters for an Update operation.
	UpdateRequest struct{}
	// UpdateResponse contains the results of a successful Update operation.
	UpdateResponse struct{}

	// ScanRequest defines the parameters for a Scan operation.
	ScanRequest struct {
		Rescan bool
	}
	// ScanResponse contains information gleaned during
	// a successful Scan operation.
	ScanResponse struct {
		State      types.ScmState
		Modules    []Module
		Namespaces []Namespace
	}

	// Backend defines a set of methods to be implemented by a SCM backend.
	Backend interface {
		GetModules() ([]Module, error)
		Prep(types.ScmState) (bool, []Namespace, error)
		PrepReset(types.ScmState) (bool, error)
		GetState() (types.ScmState, error)
		GetNamespaces() ([]Namespace, error)
	}

	// SystemProvider defines a set of methods to be implemented by a provider
	// of SCM-specific system capabilties.
	SystemProvider interface {
		system.IsMountedProvider
		system.MountProvider
		system.UnmountProvider
		Mkfs(fsType, device string, force bool) error
		Getfs(device string) (string, error)
	}

	defaultSystemProvider struct {
		system.LinuxProvider
	}

	// Provider encapsulates configuration and logic for
	// providing SCM management and interrogation.
	Provider struct {
		sync.RWMutex
		scanCompleted bool
		lastState     types.ScmState
		modules       []Module
		namespaces    []Namespace

		log     logging.Logger
		backend Backend
		sys     SystemProvider
	}
)

// Validate checks the request for validity.
func (fr FormatRequest) Validate() error {
	if fr.Mountpoint == "" {
		return FaultFormatMissingMountpoint
	}

	if fr.Ramdisk != nil && fr.Dcpm != nil {
		return FaultFormatConflictingParam
	}

	if fr.Ramdisk == nil && fr.Dcpm == nil {
		return FaultFormatMissingParam
	}

	if fr.Ramdisk != nil {
		if fr.Ramdisk.Size == 0 {
			return FaultFormatInvalidSize
		}
	}

	if fr.Dcpm != nil {
		if fr.Dcpm.Device == "" {
			return FaultFormatInvalidDeviceCount
		}
	}

	return nil
}

func checkDevice(device string) error {
	st, err := os.Stat(device)
	if err != nil {
		return errors.Wrapf(err, "stat failed on %s", device)
	}

	if st.Mode()&os.ModeDevice == 0 {
		return errors.Errorf("%s is not a device file", device)
	}

	return nil
}

// Mkfs attempts to create a filesystem of the supplied type, on the
// supplied device.
func (ssp *defaultSystemProvider) Mkfs(fsType, device string, force bool) error {
	cmdPath, err := exec.LookPath(fmt.Sprintf("mkfs.%s", fsType))
	if err != nil {
		return errors.Wrapf(err, "unable to find mkfs.%s", fsType)
	}

	if err := checkDevice(device); err != nil {
		return err
	}

	// TODO: Think about a way to allow for some kind of progress
	// callback so that the user has some visibility into long-running
	// format operations (very large devices).
	args := []string{device}
	if force {
		args = append([]string{"-F"}, args...)
	}
	out, err := exec.Command(cmdPath, args...).Output()
	if err != nil {
		return &runCmdError{
			wrapped: err,
			stdout:  string(out),
		}
	}

	return nil
}

// GetFs probes the specified device in an attempt to determine the
// formatted filesystem type, if any.
func (ssp *defaultSystemProvider) Getfs(device string) (string, error) {
	cmdPath, err := exec.LookPath("file")
	if err != nil {
		return fsTypeNone, errors.Wrap(err, "unable to find file")
	}

	if err := checkDevice(device); err != nil {
		return fsTypeNone, err
	}

	args := []string{"-s", device}
	out, err := exec.Command(cmdPath, args...).Output()
	if err != nil {
		return fsTypeNone, &runCmdError{
			wrapped: err,
			stdout:  string(out),
		}
	}

	return parseFsType(string(out))
}

func parseFsType(input string) (string, error) {
	// /dev/pmem0: Linux rev 1.0 ext4 filesystem data, UUID=09619a0d-0c9e-46b4-add5-faf575dd293d
	// /dev/pmem1: data
	fields := strings.Fields(input)
	switch {
	case len(fields) == 2 && fields[1] == parseFsUnformatted:
		return fsTypeNone, nil
	case len(fields) >= 5:
		return fields[4], nil
	}

	return fsTypeNone, errors.Errorf("unable to determine fs type from %q", input)
}

// DefaultProvider returns an initialized *Provider suitable for use with production code.
func DefaultProvider(log logging.Logger) *Provider {
	lp := system.DefaultProvider()
	p := &defaultSystemProvider{
		LinuxProvider: *lp,
	}
	return NewProvider(log, defaultCmdRunner(log), p)
}

// NewProvider returns an initialized *Provider.
func NewProvider(log logging.Logger, backend Backend, sys SystemProvider) *Provider {
	return &Provider{
		log:     log,
		backend: backend,
		sys:     sys,
	}
}

func (p *Provider) isInitialized() bool {
	p.RLock()
	defer p.RUnlock()
	return p.scanCompleted
}

func (p *Provider) currentState() types.ScmState {
	p.RLock()
	defer p.RUnlock()
	return p.lastState
}

func (p *Provider) updateState() (state types.ScmState, err error) {
	state, err = p.backend.GetState()
	if err != nil {
		return
	}

	p.Lock()
	p.lastState = state
	p.Unlock()

	return
}

// GetState returns the current state of DCPM namespaces, if available.
func (p *Provider) GetState() (types.ScmState, error) {
	if !p.isInitialized() {
		if _, err := p.Scan(ScanRequest{}); err != nil {
			return p.lastState, err
		}
	}

	return p.currentState(), nil
}

func (p *Provider) createScanResponse() *ScanResponse {
	p.RLock()
	defer p.RUnlock()

	return &ScanResponse{
		State:      p.lastState,
		Modules:    p.modules,
		Namespaces: p.namespaces,
	}
}

// Scan attempts to scan the system for SCM storage components.
func (p *Provider) Scan(req ScanRequest) (*ScanResponse, error) {
	if p.isInitialized() && !req.Rescan {
		return p.createScanResponse(), nil
	}

	modules, err := p.backend.GetModules()
	if err != nil {
		return nil, err
	}

	p.Lock()
	p.scanCompleted = true
	p.modules = modules
	p.Unlock()

	// If there are no modules, don't bother with the rest of the scan.
	if len(modules) == 0 {
		return p.createScanResponse(), nil
	}

	namespaces, err := p.backend.GetNamespaces()
	if err != nil {
		return p.createScanResponse(), err
	}

	state, err := p.backend.GetState()
	if err != nil {
		return nil, err
	}

	p.Lock()
	p.lastState = state
	p.namespaces = namespaces
	p.Unlock()

	return p.createScanResponse(), nil
}

// Prepare attempts to fulfill a SCM Prepare request.
func (p *Provider) Prepare(req PrepareRequest) (res *PrepareResponse, err error) {
	if !p.isInitialized() {
		if _, err := p.Scan(ScanRequest{}); err != nil {
			return nil, err
		}
	}

	res = &PrepareResponse{}
	if req.Reset {
		res.RebootRequired, err = p.backend.PrepReset(p.currentState())
		if err != nil {
			res = nil
			return
		}
		res.State, err = p.updateState()
		if err != nil {
			res = nil
		}
		return
	}

	res.RebootRequired, res.Namespaces, err = p.backend.Prep(p.currentState())
	if err != nil {
		res = nil
		return
	}
	res.State, err = p.updateState()
	if err != nil {
		res = nil
	}
	return
}

// CheckFormat attempts to determine whether or not the SCM specified in the
// request is already formatted. If it is mounted, it is assumed to be formatted.
// In the case of DCPM, the device is checked directly for the presence of a
// filesystem.
func (p *Provider) CheckFormat(req FormatRequest) (*FormatResponse, error) {
	if !p.isInitialized() {
		if _, err := p.Scan(ScanRequest{}); err != nil {
			return nil, err
		}
	}

	if err := req.Validate(); err != nil {
		return nil, err
	}

	res := &FormatResponse{
		Mountpoint: req.Mountpoint,
		Formatted:  true,
	}

	isMounted, err := p.sys.IsMounted(req.Mountpoint)
	if err != nil && !os.IsNotExist(err) {
		return nil, errors.Wrapf(err, "failed to check if %s is mounted", req.Mountpoint)
	}
	if isMounted {
		return res, nil
	}

	if req.Dcpm != nil {
		fsType, err := p.sys.Getfs(req.Dcpm.Device)
		if err != nil {
			return nil, errors.Wrapf(err, "failed to check if %s is formatted", req.Dcpm.Device)
		}

		p.log.Debugf("device %s filesystem: %s", req.Dcpm.Device, fsType)
		if fsType != fsTypeNone {
			return res, nil
		}
	}

	res.Formatted = false
	return res, nil
}

func (p *Provider) clearMount(req FormatRequest) error {
	mounted, err := p.sys.IsMounted(req.Mountpoint)
	if err != nil && !os.IsNotExist(err) {
		return errors.Wrapf(err, "failed to check if %s is mounted", req.Mountpoint)
	}

	if mounted {
		_, err := p.unmount(req.Mountpoint, defaultUnmountFlags)
		if err != nil {
			return err
		}
	}

	if err := os.RemoveAll(req.Mountpoint); err != nil {
		if !os.IsNotExist(err) {
			return errors.Wrapf(err, "failed to remove %s", req.Mountpoint)
		}
	}

	return nil
}

// Format attempts to fulfill the specified SCM format request.
func (p *Provider) Format(req FormatRequest) (*FormatResponse, error) {
	check, err := p.CheckFormat(req)
	if err != nil {
		return nil, err
	}
	if check.Formatted {
		if !req.Reformat {
			return nil, FaultFormatNoReformat
		}
	}

	if err := p.clearMount(req); err != nil {
		return nil, errors.Wrap(err, "failed to clear existing mount")
	}

	switch {
	case req.Ramdisk != nil:
		return p.formatRamdisk(req)
	case req.Dcpm != nil:
		return p.formatDcpm(req)
	default:
		return nil, FaultFormatMissingParam
	}
}

func (p *Provider) formatRamdisk(req FormatRequest) (*FormatResponse, error) {
	if req.Ramdisk == nil {
		return nil, FaultFormatMissingParam
	}

	res, err := p.MountRamdisk(req.Mountpoint, req.Ramdisk.Size)
	if err != nil {
		return nil, err
	}
	return &FormatResponse{
		Mountpoint: res.Target,
		Formatted:  res.Mounted,
	}, nil
}

func (p *Provider) formatDcpm(req FormatRequest) (*FormatResponse, error) {
	if req.Dcpm == nil {
		return nil, FaultFormatMissingParam
	}

	alreadyMounted, err := p.sys.IsMounted(req.Dcpm.Device)
	if err != nil {
		return nil, errors.Wrapf(err, "unable to check if %s is already mounted", req.Dcpm.Device)
	}
	if alreadyMounted {
		return nil, errors.Wrap(FaultDeviceAlreadyMounted, req.Dcpm.Device)
	}

	p.log.Debugf("running mkfs.%s %s", dcpmFsType, req.Dcpm.Device)
	if err := p.sys.Mkfs(dcpmFsType, req.Dcpm.Device, req.Reformat); err != nil {
		return nil, errors.Wrapf(err, "failed to format %s", req.Dcpm.Device)
	}

	res, err := p.MountDcpm(req.Dcpm.Device, req.Mountpoint)
	if err != nil {
		return nil, err
	}
	return &FormatResponse{
		Mountpoint: res.Target,
		Formatted:  res.Mounted,
	}, nil
}

// MountDcpm attempts to mount a DCPM device at the specified mountpoint.
func (p *Provider) MountDcpm(device, target string) (*MountResponse, error) {
	// make sure the source device is not already mounted somewhere else
	devMounted, err := p.sys.IsMounted(device)
	if err != nil {
		return nil, errors.Wrapf(err, "unable to check if %s is mounted", device)
	}
	if devMounted {
		return nil, errors.Wrap(FaultDeviceAlreadyMounted, device)
	}

	return p.mount(device, target, dcpmFsType, defaultMountFlags, dcpmMountOpts)
}

// MountRamdisk attempts to mount a tmpfs-based ramdisk of the specified size at
// the specified mountpoint.
func (p *Provider) MountRamdisk(target string, size uint) (*MountResponse, error) {
	var opts string
	if size > 0 {
		opts = fmt.Sprintf("size=%dg", size)
	}

	return p.mount(ramFsType, target, ramFsType, defaultMountFlags, opts)
}

// Mount attempts to mount the target specified in the supplied request.
func (p *Provider) Mount(req MountRequest) (*MountResponse, error) {
	return p.mount(req.Source, req.Target, req.FsType, req.Flags, req.Data)
}

func (p *Provider) mount(src, target, fsType string, flags uintptr, opts string) (*MountResponse, error) {
	if err := os.MkdirAll(target, defaultMountPointPerms); err != nil {
		return nil, errors.Wrapf(err, "failed to create mountpoint %s", target)
	}

	// make sure that we're not double-mounting over an existing mount
	tgtMounted, err := p.sys.IsMounted(target)
	if err != nil && !os.IsNotExist(err) {
		return nil, errors.Wrapf(err, "unable to check if %s is mounted", target)
	}
	if tgtMounted {
		return nil, errors.Wrap(FaultTargetAlreadyMounted, target)
	}

	p.log.Debugf("mount %s->%s (%s) (%s)", src, target, fsType, opts)
	if err := p.sys.Mount(src, target, fsType, flags, opts); err != nil {
		return nil, errors.Wrapf(err, "mount %s->%s failed", src, target)
	}

	return &MountResponse{
		Target:  target,
		Mounted: true,
	}, nil
}

// Unmount attempts to unmount the target specified in the supplied request.
func (p *Provider) Unmount(req MountRequest) (*MountResponse, error) {
	return p.unmount(req.Target, int(req.Flags))
}

func (p *Provider) unmount(target string, flags int) (*MountResponse, error) {
	if err := p.sys.Unmount(target, flags); err != nil {
		return nil, errors.Wrapf(err, "failed to unmount %s", target)
	}

	return &MountResponse{
		Target:  target,
		Mounted: false,
	}, nil
}

// IsMounted checks to see if the target device or directory
// is mounted.
func (p *Provider) IsMounted(target string) (bool, error) {
	return p.sys.IsMounted(target)
}

// Update attempts to update the DCPM firmware, if supported.
func (p *Provider) Update(req UpdateRequest) (*UpdateResponse, error) {
	return nil, nil
}
