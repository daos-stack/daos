//
// (C) Copyright 2019-2022 Intel Corporation.
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

	fsTypeNone    = "none"
	fsTypeExt4    = "ext4"
	fsTypeTmpfs   = "tmpfs"
	fsTypeUnknown = "unknown"

	dcpmFsType    = fsTypeExt4
	dcpmMountOpts = "dax,nodelalloc"

	ramFsType = fsTypeTmpfs
)

type (
	// Backend defines a set of methods to be implemented by a SCM backend.
	Backend interface {
		getModules() (storage.ScmModules, error)
		getRegionState() (storage.ScmState, error)
		getNamespaces() (storage.ScmNamespaces, error)
		prep(storage.ScmPrepareRequest, *storage.ScmScanResponse) (*storage.ScmPrepareResponse, error)
		prepReset(*storage.ScmScanResponse) (*storage.ScmPrepareResponse, error)
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
		Stat(string) (os.FileInfo, error)
	}

	defaultSystemProvider struct {
		system.LinuxProvider
	}

	// Provider encapsulates configuration and logic for
	// providing SCM management and interrogation.
	Provider struct {
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

func getDistroArgs() []string {
	distro := system.GetDistribution()

	// use stride option for SCM interleaved mode
	// disable lazy initialization (hurts perf)
	// discard is not needed/supported on SCM
	opts := "stride=512,lazy_itable_init=0,lazy_journal_init=0,nodiscard"

	// enable flex_bg to allow larger contiguous block allocation
	// disable uninit_bg to initialize everything upfront
	// disable resize to avoid GDT block allocations
	// disable extra isize since we really have no use for this
	feat := "flex_bg,^uninit_bg,^resize_inode,^extra_isize"

	switch {
	case distro.ID == "centos" && distro.Version.Major < 8:
		// use strict minimum listed above here since that's
		// the oldest distribution we support
	default:
		// packed_meta_blocks allows to group all data blocks together
		opts += ",packed_meta_blocks=1"
		// enable sparse_super2 since 2x superblock copies are enough
		// disable csum since we have ECC already for SCM
		// bigalloc is intentionally not used since some kernels don't support it
		feat += ",sparse_super2,^metadata_csum"
	}

	return []string{
		"-E", opts,
		"-O", feat,
		// each ext4 group is of size 32767 x 4KB = 128M
		// pack 128 groups together to increase ability to use huge
		// pages for a total virtual group size of 16G
		"-G", "128",
	}
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
		// use quiet mode
		"-q",
		// use direct i/o to avoid polluting page cache
		"-D",
		// use DAOS label
		"-L", "daos",
		// don't reserve blocks for super-user
		"-m", "0",
		// use largest possible block size
		"-b", "4096",
		// don't need large inode, 128B is enough
		// since we don't use xattr
		"-I", "128",
		// reduce the inode per bytes ratio
		// one inode for 64M is more than enough
		"-i", "67108864",
	}
	args = append(args, getDistroArgs()...)
	// string always comes last
	args = append(args, []string{device}...)
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

	return parseFsType(string(out)), nil
}

// Stat probes the specified path and returns os level file info.
func (dsp *defaultSystemProvider) Stat(path string) (os.FileInfo, error) {
	return os.Stat(path)
}

func parseFsType(input string) string {
	// /dev/pmem0: Linux rev 1.0 ext4 filesystem data, UUID=09619a0d-0c9e-46b4-add5-faf575dd293d
	// /dev/pmem1: data
	// /dev/pmem1: COM executable for DOS
	fields := strings.Fields(input)
	switch {
	case len(fields) == 2 && fields[1] == parseFsUnformatted:
		return fsTypeNone
	case len(fields) >= 5:
		return fields[4]
	default:
		return fsTypeUnknown
	}
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

// Scan attempts to scan the system for SCM storage components.
func (p *Provider) Scan(req storage.ScmScanRequest) (*storage.ScmScanResponse, error) {
	modules, err := p.backend.getModules()
	if err != nil {
		return nil, err
	}
	p.log.Debugf("scm backend: %d modules", len(modules))

	// If there are no modules, don't bother with the rest of the scan.
	if len(modules) == 0 {
		return &storage.ScmScanResponse{
			State: storage.ScmStateNoModules,
		}, nil
	}

	namespaces, err := p.backend.getNamespaces()
	if err != nil {
		return nil, err
	}
	p.log.Debugf("scm backend: namespaces %+v", namespaces)

	state, err := p.backend.getRegionState()
	if err != nil {
		return nil, err
	}
	p.log.Debugf("scm backend: state %q", state)

	return &storage.ScmScanResponse{
		State:      state,
		Modules:    modules,
		Namespaces: namespaces,
	}, nil
}

type scanFn func(storage.ScmScanRequest) (*storage.ScmScanResponse, error)

func (p *Provider) prepare(req storage.ScmPrepareRequest, scan scanFn) (*storage.ScmPrepareResponse, error) {
	p.log.Debug("scm provider prepare: calling provider scan")

	scanReq := storage.ScmScanRequest{}

	scanResp, err := scan(scanReq)
	if err != nil {
		return nil, err
	}

	if scanResp.State == storage.ScmStateNoModules {
		return &storage.ScmPrepareResponse{
			State:      scanResp.State,
			Namespaces: storage.ScmNamespaces{},
		}, nil
	}

	if req.Reset {
		// Unmount PMem namespaces before removing them.
		if len(scanResp.Namespaces) > 0 {
			for _, ns := range scanResp.Namespaces {
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

		p.log.Debug("scm provider prepare: calling backend prepReset")
		return p.backend.prepReset(scanResp)
	}

	p.log.Debug("scm provider prepare: calling backend prep")
	return p.backend.prep(req, scanResp)
}

// Prepare attempts to fulfill a SCM Prepare request.
func (p *Provider) Prepare(req storage.ScmPrepareRequest) (*storage.ScmPrepareResponse, error) {
	return p.prepare(req, p.Scan)
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
	case fsTypeUnknown:
		// formatted but not mountable
		p.log.Debugf("unexpected format of output from 'file -s %s'", req.Dcpm.Device)
	default:
		// formatted but not mountable
		p.log.Debugf("%q fs type is unexpected", fsType)
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

// Format attempts to fulfill the specified SCM format request.
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
