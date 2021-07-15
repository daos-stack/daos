//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package scm

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"

	"github.com/pkg/errors"
	"golang.org/x/sys/unix"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/provider/system"
	"github.com/daos-stack/daos/src/control/server/storage"
)

var _ storage.ScmProvider = &Provider{}

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
)

type (
	// Backend defines a set of methods to be implemented by a SCM backend.
	Backend interface {
		Discover() (storage.ScmModules, error)
		Prep(storage.ScmState) (bool, storage.ScmNamespaces, error)
		PrepReset(storage.ScmState) (bool, error)
		GetPmemState() (storage.ScmState, error)
		GetPmemNamespaces() (storage.ScmNamespaces, error)
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
		GetfsUsage(string) (uint64, uint64, error)
		Stat(string) (os.FileInfo, error)
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
	}
)

func validateFormatRequest(r storage.ScmFormatRequest) error {
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

func (dsp *defaultSystemProvider) checkDevice(device string) error {
	st, err := dsp.Stat(device)
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
func (dsp *defaultSystemProvider) Mkfs(fsType, device string, force bool) error {
	cmdPath, err := exec.LookPath(fmt.Sprintf("mkfs.%s", fsType))
	if err != nil {
		return errors.Wrapf(err, "unable to find mkfs.%s", fsType)
	}

	if err := dsp.checkDevice(device); err != nil {
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
func (dsp *defaultSystemProvider) Getfs(device string) (string, error) {
	cmdPath, err := exec.LookPath("file")
	if err != nil {
		return fsTypeNone, errors.Wrap(err, "unable to find file")
	}

	if err := dsp.checkDevice(device); err != nil {
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

// Stat probes the specified path and returns os level file info.
func (dsp *defaultSystemProvider) Stat(path string) (os.FileInfo, error) {
	return os.Stat(path)
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
	}
	return p
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
	state, err = p.backend.GetPmemState()
	if err != nil {
		return
	}

	p.Lock()
	p.lastState = state
	p.Unlock()

	return
}

// GetPmemState returns the current state of DCPM namespaces, if available.
func (p *Provider) GetPmemState() (storage.ScmState, error) {
	if !p.isInitialized() {
		if _, err := p.Scan(storage.ScmScanRequest{}); err != nil {
			return p.lastState, err
		}
	}

	return p.currentState(), nil
}

func (p *Provider) createScanResponse() *storage.ScmScanResponse {
	p.RLock()
	defer p.RUnlock()

	return &storage.ScmScanResponse{
		State:      p.lastState,
		Modules:    p.modules,
		Namespaces: p.namespaces,
	}
}

// Scan attempts to scan the system for SCM storage components.
func (p *Provider) Scan(req storage.ScmScanRequest) (*storage.ScmScanResponse, error) {
	if p.isInitialized() && !req.Rescan {
		return p.createScanResponse(), nil
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

	namespaces, err := p.backend.GetPmemNamespaces()
	if err != nil {
		return p.createScanResponse(), err
	}

	state, err := p.backend.GetPmemState()
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
func (p *Provider) Prepare(req storage.ScmPrepareRequest) (res *storage.ScmPrepareResponse, err error) {
	if !p.isInitialized() {
		if _, err := p.Scan(storage.ScmScanRequest{}); err != nil {
			return nil, err
		}
	}

	res = &storage.ScmPrepareResponse{}
	if sr := p.createScanResponse(); len(sr.Modules) == 0 {
		p.log.Info("skipping SCM prepare; no modules detected")
		res.State = sr.State

		return res, nil
	}

	if req.Reset {
		// Ensure that namespace block devices are unmounted first.
		if sr := p.createScanResponse(); len(sr.Namespaces) > 0 {
			for _, ns := range sr.Namespaces {
				nsDev := "/dev/" + ns.BlockDevice
				isMounted, err := p.IsMounted(nsDev)
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
func (p *Provider) CheckFormat(req storage.ScmFormatRequest) (*storage.ScmFormatResponse, error) {
	if err := validateFormatRequest(req); err != nil {
		return nil, err
	}

	res := &storage.ScmFormatResponse{
		Mountpoint: req.Mountpoint,
		Formatted:  true,
	}

	isMounted, err := p.IsMounted(req.Mountpoint)
	if err != nil && !os.IsNotExist(err) {
		return nil, err
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

	if !p.isInitialized() {
		if _, err := p.Scan(storage.ScmScanRequest{}); err != nil {
			return nil, err
		}
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

func (p *Provider) clearMount(mntpt string) error {
	mounted, err := p.IsMounted(mntpt)
	if err != nil && !os.IsNotExist(err) {
		return err
	}

	if mounted {
		_, err := p.unmount(mntpt, defaultUnmountFlags)
		if err != nil {
			return err
		}
	}

	if err := os.RemoveAll(mntpt); err != nil {
		if !os.IsNotExist(err) {
			return errors.Wrapf(err, "failed to remove %s", mntpt)
		}
	}

	return nil
}

// makeMountPath will build the target path by creating any non-existent
// subdirectories and setting ownership to target uid/gid to ensure that the
// target user is able to access the created mount point if multiple layers
// deep. If subdirectories in path already exist, their permissions will not be
// modified.
func (p *Provider) makeMountPath(path string, tgtUID, tgtGID int) error {
	if !filepath.IsAbs(path) {
		return errors.Errorf("expecting absolute target path, got %q", path)
	}
	if _, err := p.Stat(path); err == nil {
		return nil // path already exists
	}
	// don't need to validate request UID/GID as they are populated inside
	// this package during CreateFormatRequest()

	sep := string(filepath.Separator)
	dirs := strings.Split(path, sep)[1:] // omit empty element

	for i := range dirs {
		ps := sep + filepath.Join(dirs[:i+1]...)
		_, err := p.Stat(ps)
		switch {
		case os.IsNotExist(err):
			// subdir missing, attempt to create and chown
			if err := os.Mkdir(ps, defaultMountPointPerms); err != nil {
				return errors.Wrapf(err, "failed to create directory %q", ps)
			}
			if err := os.Chown(ps, tgtUID, tgtGID); err != nil {
				return errors.Wrapf(err, "failed to set ownership of %s to %d.%d",
					ps, tgtUID, tgtGID)
			}
		case err != nil:
			return errors.Wrapf(err, "unable to stat %q", ps)
		}
	}

	return nil
}

// storage.ScmFormat attempts to fulfill the specified SCM format request.
func (p *Provider) Format(req storage.ScmFormatRequest) (*storage.ScmFormatResponse, error) {
	check, err := p.CheckFormat(req)
	if err != nil {
		return nil, err
	}
	if check.Formatted {
		if !req.Force {
			return nil, FaultFormatNoReformat
		}
	}

	if err := p.clearMount(req.Mountpoint); err != nil {
		return nil, errors.Wrap(err, "failed to clear existing mount")
	}

	if err := p.makeMountPath(req.Mountpoint, req.OwnerUID, req.OwnerGID); err != nil {
		return nil, errors.Wrap(err, "failed to create mount path")
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

func (p *Provider) formatRamdisk(req storage.ScmFormatRequest) (*storage.ScmFormatResponse, error) {
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

	return &storage.ScmFormatResponse{
		Mountpoint: res.Target,
		Formatted:  true,
		Mounted:    true,
		Mountable:  false,
	}, nil
}

func (p *Provider) formatDcpm(req storage.ScmFormatRequest) (*storage.ScmFormatResponse, error) {
	if req.Dcpm == nil {
		return nil, FaultFormatMissingParam
	}

	alreadyMounted, err := p.IsMounted(req.Dcpm.Device)
	if err != nil {
		return nil, err
	}
	if alreadyMounted {
		return nil, errors.Wrap(FaultDeviceAlreadyMounted, req.Dcpm.Device)
	}

	p.log.Debugf("running mkfs.%s %s", dcpmFsType, req.Dcpm.Device)
	if err := p.sys.Mkfs(dcpmFsType, req.Dcpm.Device, req.Force); err != nil {
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

	return &storage.ScmFormatResponse{
		Mountpoint: res.Target,
		Formatted:  true,
		Mounted:    true,
		Mountable:  false,
	}, nil
}

// MountDcpm attempts to mount a DCPM device at the specified mountpoint.
func (p *Provider) MountDcpm(device, target string) (*storage.ScmMountResponse, error) {
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
func (p *Provider) MountRamdisk(target string, size uint) (*storage.ScmMountResponse, error) {
	var opts string
	if size > 0 {
		opts = fmt.Sprintf("size=%dg", size)
	}

	return p.mount(ramFsType, target, ramFsType, defaultMountFlags, opts)
}

// Mount attempts to mount the target specified in the supplied request.
func (p *Provider) Mount(req storage.ScmMountRequest) (*storage.ScmMountResponse, error) {
	switch req.Class {
	case storage.ClassDcpm:
		return p.MountDcpm(req.Device, req.Target)
	case storage.ClassRam:
		return p.MountRamdisk(req.Target, req.Size)
	default:
		return nil, errors.New(storage.ScmMsgClassNotSupported)
	}
}

func (p *Provider) mount(src, target, fsType string, flags uintptr, opts string) (*storage.ScmMountResponse, error) {
	// make sure that we're not double-mounting over an existing mount
	tgtMounted, err := p.IsMounted(target)
	if err != nil && !os.IsNotExist(err) {
		return nil, err
	}
	if tgtMounted {
		return nil, errors.Wrap(FaultTargetAlreadyMounted, target)
	}

	p.log.Debugf("mount %s->%s (%s) (%s)", src, target, fsType, opts)
	if err := p.sys.Mount(src, target, fsType, flags, opts); err != nil {
		return nil, errors.Wrapf(err, "mount %s->%s failed", src, target)
	}

	return &storage.ScmMountResponse{
		Target:  target,
		Mounted: true,
	}, nil
}

// Unmount attempts to unmount the target specified in the supplied request.
func (p *Provider) Unmount(req storage.ScmMountRequest) (*storage.ScmMountResponse, error) {
	return p.unmount(req.Target, defaultUnmountFlags)
}

func (p *Provider) unmount(target string, flags int) (*storage.ScmMountResponse, error) {
	if err := p.sys.Unmount(target, flags); err != nil {
		return nil, errors.Wrapf(err, "failed to unmount %s", target)
	}

	return &storage.ScmMountResponse{
		Target:  target,
		Mounted: false,
	}, nil
}

// Stat probes the specified path and returns os level file info.
func (p *Provider) Stat(path string) (os.FileInfo, error) {
	return p.sys.Stat(path)
}

// IsMounted checks to see if the target device or directory is mounted and
// returns flag to specify whether mounted or a relevant fault.
func (p *Provider) IsMounted(target string) (bool, error) {
	isMounted, err := p.sys.IsMounted(target)

	if errors.Is(err, os.ErrPermission) {
		return false, errors.Wrap(FaultPathAccessDenied(target), "check if mounted")
	}

	return isMounted, err
}

// GetfsUsage returns space utilization info for a mount point.
func (p *Provider) GetfsUsage(target string) (*storage.ScmMountPoint, error) {
	total, avail, err := p.sys.GetfsUsage(target)
	if err != nil {
		return nil, err
	}

	return &storage.ScmMountPoint{
		Path:       target,
		TotalBytes: total,
		AvailBytes: avail,
	}, nil
}
