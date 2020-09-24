//
// (C) Copyright 2019-2020 Intel Corporation.
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
	"golang.org/x/sys/unix"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/pbin"
	"github.com/daos-stack/daos/src/control/provider/system"
	"github.com/daos-stack/daos/src/control/server/storage"
)

const (
	defaultUnmountFlags = 0
	defaultMountFlags   = unix.MS_NOATIME

	defaultMountPointPerms = 0700

	parseFsUnformatted = "data"

	fsTypeNone  = "none"
	fsTypeExt4  = "ext4"
	fsTypeTmpfs = "tmpfs"

	dcpmFsType    = fsTypeExt4
	dcpmMountOpts = "dax,nodelalloc"

	ramFsType = fsTypeTmpfs

	MsgScmRebootRequired    = "A reboot is required to process new memory allocation goals."
	MsgScmNoModules         = "no scm modules to prepare"
	MsgScmNotInited         = "scm storage could not be accessed"
	MsgScmClassNotSupported = "operation unsupported on scm class"
	MsgIpmctlDiscoverFail   = "ipmctl module discovery"
)

type (
	// PrepareRequest defines the parameters for a Prepare operation.
	PrepareRequest struct {
		pbin.ForwardableRequest
		// Reset indicates that the operation should reset (clear) DCPM namespaces.
		Reset bool
	}

	// PrepareResponse contains the results of a successful Prepare operation.
	PrepareResponse struct {
		State          storage.ScmState
		RebootRequired bool
		Namespaces     storage.ScmNamespaces
	}

	// ScanRequest defines the parameters for a Scan operation.
	ScanRequest struct {
		pbin.ForwardableRequest
		DeviceList []string
		Rescan     bool
	}

	// ScanResponse contains information gleaned during a successful Scan operation.
	ScanResponse struct {
		State      storage.ScmState
		Modules    storage.ScmModules
		Namespaces storage.ScmNamespaces
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
		pbin.ForwardableRequest
		Reformat   bool
		Mountpoint string
		OwnerUID   int
		OwnerGID   int
		Ramdisk    *RamdiskParams
		Dcpm       *DcpmParams
	}

	// FormatResponse contains the results of a successful Format operation or query.
	FormatResponse struct {
		Mountpoint string
		Formatted  bool
		Mounted    bool
		Mountable  bool
	}

	// MountRequest defines the parameters for a Mount operation.
	MountRequest struct {
		pbin.ForwardableRequest
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

	// Backend defines a set of methods to be implemented by a SCM backend.
	Backend interface {
		Discover() (storage.ScmModules, error)
		Prep(storage.ScmState) (bool, storage.ScmNamespaces, error)
		PrepReset(storage.ScmState) (bool, error)
		GetState() (storage.ScmState, error)
		GetNamespaces() (storage.ScmNamespaces, error)
		GetFirmwareStatus(deviceUID string) (*storage.ScmFirmwareInfo, error)
		UpdateFirmware(deviceUID string, firmwarePath string) error
	}

	// SystemProvider defines a set of methods to be implemented by a provider
	// of SCM-specific system capabilities.
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
		lastState     storage.ScmState
		modules       storage.ScmModules
		namespaces    storage.ScmNamespaces

		log     logging.Logger
		backend Backend
		sys     SystemProvider
		fwd     *AdminForwarder
		firmwareProvider
	}
)

func CreateFormatRequest(scmCfg storage.ScmConfig, reformat bool) (*FormatRequest, error) {
	req := FormatRequest{
		Mountpoint: scmCfg.MountPoint,
		Reformat:   reformat,
		OwnerUID:   os.Geteuid(),
		OwnerGID:   os.Getegid(),
	}

	switch scmCfg.Class {
	case storage.ScmClassRAM:
		req.Ramdisk = &RamdiskParams{
			Size: uint(scmCfg.RamdiskSize),
		}
	case storage.ScmClassDCPM:
		// FIXME (DAOS-3291): Clean up SCM configuration
		if len(scmCfg.DeviceList) != 1 {
			return nil, FaultFormatInvalidDeviceCount
		}
		req.Dcpm = &DcpmParams{
			Device: scmCfg.DeviceList[0],
		}
	default:
		return nil, errors.New(MsgScmClassNotSupported)
	}

	return &req, nil
}

// Validate checks the request for validity.
func (r FormatRequest) Validate() error {
	if r.Mountpoint == "" {
		return FaultFormatMissingMountpoint
	}

	if r.Ramdisk != nil && r.Dcpm != nil {
		return FaultFormatConflictingParam
	}

	if r.Ramdisk == nil && r.Dcpm == nil {
		return FaultFormatMissingParam
	}

	if r.Ramdisk != nil {
		if r.Ramdisk.Size == 0 {
			return FaultFormatInvalidSize
		}
	}

	if r.Dcpm != nil {
		if r.Dcpm.Device == "" {
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
	args := []string{
		// use direct i/o to avoid polluting page cache
		"-D",
		// use DAOS label
		"-L", "daos",
		// don't reserve blocks for super-user
		"-m", "0",
		// use largest possible block size
		"-b", "4096",
		// disable lazy initialization (hurts perf) and discard
		"-E", "lazy_itable_init=0,lazy_journal_init=0,nodiscard",
		// enable bigalloc to reduce metadata overhead
		// enable flex_bg to allow larger contiguous block allocation
		// disable uninit_bg to initialize everything upfront
		"-O", "bigalloc,flex_bg,^uninit_bg",
		// use 16M bigalloc cluster size
		"-C", "16M",
		// don't need that many inodes
		"-i", "16777216",
		// device always comes last
		device,
	}
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

// Getfs probes the specified device in an attempt to determine the
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
	p := &Provider{
		log:     log,
		backend: backend,
		sys:     sys,
		fwd:     NewAdminForwarder(log),
	}
	p.setupFirmwareProvider(log)
	return p
}

func (p *Provider) WithForwardingDisabled() *Provider {
	p.fwd.Disabled = true
	return p
}

func (p *Provider) shouldForward(req pbin.ForwardChecker) bool {
	return !p.fwd.Disabled && !req.IsForwarded()
}

func (p *Provider) isInitialized() bool {
	p.RLock()
	defer p.RUnlock()
	return p.scanCompleted
}

func (p *Provider) currentState() storage.ScmState {
	p.RLock()
	defer p.RUnlock()
	return p.lastState
}

func (p *Provider) updateState() (state storage.ScmState, err error) {
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
func (p *Provider) GetState() (storage.ScmState, error) {
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

	if p.shouldForward(req) {
		res, err := p.fwd.Scan(req)
		if err != nil {
			return nil, err
		}
		p.Lock()
		p.scanCompleted = true
		p.lastState = res.State
		p.modules = res.Modules
		p.namespaces = res.Namespaces
		p.Unlock()

		return res, nil
	}

	modules, err := p.backend.Discover()
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
	if sr := p.createScanResponse(); len(sr.Modules) == 0 {
		p.log.Info("skipping SCM prepare; no modules detected")
		res.State = sr.State

		return res, nil
	}

	if p.shouldForward(req) {
		return p.fwd.Prepare(req)
	}

	if req.Reset {
		// Ensure that namespace block devices are unmounted first.
		if sr := p.createScanResponse(); len(sr.Namespaces) > 0 {
			for _, ns := range sr.Namespaces {
				nsDev := "/dev/" + ns.BlockDevice
				isMounted, err := p.sys.IsMounted(nsDev)
				if err != nil {
					if os.IsNotExist(errors.Cause(err)) {
						continue
					}
					return nil, err
				}
				if isMounted {
					p.log.Debugf("Unmounting %s", nsDev)
					if err := p.sys.Unmount(nsDev, 0); err != nil {
						p.log.Errorf("Unmount error: %s", err)
						return nil, err
					}
				}
			}
		}

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

	if p.shouldForward(req) {
		return p.fwd.CheckFormat(req)
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
		res.Mounted = true
		return res, nil
	}

	if req.Dcpm == nil {
		// ramdisk
		res.Formatted = false
		return res, nil
	}

	fsType, err := p.sys.Getfs(req.Dcpm.Device)
	if err != nil {
		if os.IsNotExist(errors.Cause(err)) {
			return nil, FaultFormatMissingDevice(req.Dcpm.Device)
		}
		return nil, errors.Wrapf(err, "failed to check if %s is formatted", req.Dcpm.Device)
	}

	p.log.Debugf("device %s filesystem: %s", req.Dcpm.Device, fsType)

	switch fsType {
	case fsTypeExt4:
		res.Mountable = true
	case fsTypeNone:
		res.Formatted = false
	}

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

	if p.shouldForward(req) {
		return p.fwd.Format(req)
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

	if !res.Mounted {
		return nil, errors.Errorf("%s was not mounted", req.Mountpoint)
	}

	if err := os.Chown(req.Mountpoint, req.OwnerUID, req.OwnerGID); err != nil {
		return nil, errors.Wrapf(err, "failed to set ownership of %s to %d.%d",
			req.Mountpoint, req.OwnerUID, req.OwnerGID)
	}

	return &FormatResponse{
		Mountpoint: res.Target,
		Formatted:  true,
		Mounted:    true,
		Mountable:  false,
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

	if !res.Mounted {
		return nil, errors.Errorf("%s was not mounted", req.Mountpoint)
	}

	if err := os.Chown(req.Mountpoint, req.OwnerUID, req.OwnerGID); err != nil {
		return nil, errors.Wrapf(err, "failed to set ownership of %s to %d.%d",
			req.Mountpoint, req.OwnerUID, req.OwnerGID)
	}

	return &FormatResponse{
		Mountpoint: res.Target,
		Formatted:  true,
		Mounted:    true,
		Mountable:  false,
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
	req := MountRequest{
		Source: device,
		Target: target,
		FsType: dcpmFsType,
		Flags:  defaultMountFlags,
		Data:   dcpmMountOpts,
	}

	return p.Mount(req)
}

// MountRamdisk attempts to mount a tmpfs-based ramdisk of the specified size at
// the specified mountpoint.
func (p *Provider) MountRamdisk(target string, size uint) (*MountResponse, error) {
	var opts string
	if size > 0 {
		opts = fmt.Sprintf("size=%dg", size)
	}
	req := MountRequest{
		Source: ramFsType,
		Target: target,
		FsType: ramFsType,
		Flags:  defaultMountFlags,
		Data:   opts,
	}

	return p.Mount(req)
}

// Mount attempts to mount the target specified in the supplied request.
func (p *Provider) Mount(req MountRequest) (*MountResponse, error) {
	if p.shouldForward(req) {
		return p.fwd.Mount(req)
	}
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
	if p.shouldForward(req) {
		return p.fwd.Unmount(req)
	}
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
