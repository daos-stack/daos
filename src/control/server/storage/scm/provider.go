//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package scm

import (
	"fmt"
	"os"
	"strings"

	"github.com/pkg/errors"
	"golang.org/x/sys/unix"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/provider/system"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/mount"
)

var _ storage.ScmProvider = &Provider{}

const (
	defaultMountFlags = unix.MS_NOATIME

	defaultMountPointPerms = 0700

	dcpmFsType    = system.FsTypeExt4
	dcpmMountOpts = "dax,nodelalloc"

	ramFsType = system.FsTypeTmpfs
)

type (
	// Backend defines a set of methods to be implemented by a SCM backend.
	Backend interface {
		getModules(int) (storage.ScmModules, error)
		getNamespaces(int) (storage.ScmNamespaces, error)
		prep(storage.ScmPrepareRequest, *storage.ScmScanResponse) (*storage.ScmPrepareResponse, error)
		prepReset(storage.ScmPrepareRequest, *storage.ScmScanResponse) (*storage.ScmPrepareResponse, error)
		GetFirmwareStatus(deviceUID string) (*storage.ScmFirmwareInfo, error)
		UpdateFirmware(deviceUID string, firmwarePath string) error
	}

	// SystemProvider defines a set of methods to be implemented by a provider
	// of SCM-specific system capabilities.
	SystemProvider interface {
		Mkfs(system.MkfsReq) error
		Getfs(device string) (string, error)
		Stat(string) (os.FileInfo, error)
		Chmod(string, os.FileMode) error
		Chown(string, int, int) error
	}

	// Provider encapsulates configuration and logic for
	// providing SCM management and interrogation.
	Provider struct {
		log     logging.Logger
		backend Backend
		sys     SystemProvider
		mounter storage.MountProvider
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

// DefaultProvider returns an initialized *Provider suitable for use with production code.
func DefaultProvider(log logging.Logger) *Provider {
	return NewProvider(log, defaultCmdRunner(log), system.DefaultProvider(), mount.DefaultProvider(log))
}

// NewProvider returns an initialized *Provider.
func NewProvider(log logging.Logger, backend Backend, sys SystemProvider, mounter storage.MountProvider) *Provider {
	p := &Provider{
		log:     log,
		backend: backend,
		sys:     sys,
		mounter: mounter,
	}
	return p
}

// Scan attempts to scan the system for SCM storage components.
func (p *Provider) Scan(req storage.ScmScanRequest) (_ *storage.ScmScanResponse, err error) {
	msg := fmt.Sprintf("scm backend scan: req %+v", req)
	defer func() {
		if err != nil {
			msg = fmt.Sprintf("%s failed: %s", msg, err)
		}
		p.log.Debug(msg)
	}()

	resp := &storage.ScmScanResponse{
		Modules:    storage.ScmModules{},
		Namespaces: storage.ScmNamespaces{},
	}

	// If socket ID set in request, only scan devices attached to that socket.
	sockSelector := sockAny
	if req.SocketID != nil {
		sockSelector = int(*req.SocketID)
	}

	modules, err := p.backend.getModules(sockSelector)
	if err != nil {
		return nil, err
	}
	if len(modules) == 0 {
		msg = fmt.Sprintf("%s: no pmem modules", msg)
		return resp, nil
	}
	msg = fmt.Sprintf("%s: %d pmem modules", msg, len(modules))
	resp.Modules = modules

	namespaces, err := p.backend.getNamespaces(sockSelector)
	if err != nil {
		return nil, err
	}
	if len(namespaces) == 0 {
		msg = fmt.Sprintf("%s: no pmem namespaces", msg)
		return resp, nil
	}
	msg = fmt.Sprintf("%s: %d pmem namespace", msg, len(namespaces))
	resp.Namespaces = namespaces

	return resp, nil
}

type scanFn func(storage.ScmScanRequest) (*storage.ScmScanResponse, error)

func (p *Provider) prepare(req storage.ScmPrepareRequest, scan scanFn) (*storage.ScmPrepareResponse, error) {
	p.log.Debug("scm provider prepare: calling provider scan")

	scanReq := storage.ScmScanRequest{
		SocketID: req.SocketID,
	}

	scanResp, err := scan(scanReq)
	if err != nil {
		return nil, err
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
					if _, err := p.mounter.Unmount(storage.MountRequest{
						Target: nsDev,
					}); err != nil {
						p.log.Errorf("Unmount error: %s", err)
						return nil, err
					}
				}
			}
		}

		p.log.Debug("scm provider prepare: calling backend prepReset")
		return p.backend.prepReset(req, scanResp)
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
	case system.FsTypeExt4:
		res.Mountable = true
	case system.FsTypeNone:
		res.Formatted = false
	case system.FsTypeUnknown:
		// formatted but not mountable
		p.log.Debugf("unexpected format of output from 'file -s %s'", req.Dcpm.Device)
	default:
		// formatted but not mountable
		p.log.Debugf("%q fs type is unexpected", fsType)
	}

	return res, nil
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

	if err := p.mounter.ClearMountpoint(req.Mountpoint); err != nil {
		return nil, errors.Wrap(err, "failed to clear existing mount")
	}

	if err := p.mounter.MakeMountPath(req.Mountpoint, req.OwnerUID, req.OwnerGID); err != nil {
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

	res, err := p.mountRamdisk(req.Mountpoint, req.Ramdisk)
	if err != nil {
		return nil, err
	}

	if !res.Mounted {
		return nil, errors.Errorf("%s was not mounted", req.Mountpoint)
	}

	if err := p.sys.Chown(req.Mountpoint, req.OwnerUID, req.OwnerGID); err != nil {
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

func (p *Provider) formatDcpm(req storage.ScmFormatRequest) (*storage.ScmFormatResponse, error) {
	if req.Dcpm == nil {
		return nil, FaultFormatMissingParam
	}

	opts := []string{
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
		// one inode for 64MiB is more than enough
		"-i", "67108864",
	}
	opts = append(opts, getDistroArgs()...)

	p.log.Debugf("running mkfs.%s %s", dcpmFsType, req.Dcpm.Device)
	if err := p.sys.Mkfs(system.MkfsReq{
		Filesystem: dcpmFsType,
		Device:     req.Dcpm.Device,
		Options:    opts,
		Force:      req.Force,
	}); err != nil {
		return nil, errors.Wrapf(err, "failed to format %s", req.Dcpm.Device)
	}

	res, err := p.mountDcpm(req.Dcpm.Device, req.Mountpoint)
	if err != nil {
		return nil, err
	}

	if !res.Mounted {
		return nil, errors.Errorf("%s was not mounted", req.Mountpoint)
	}

	if err := p.sys.Chown(req.Mountpoint, req.OwnerUID, req.OwnerGID); err != nil {
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

// mountDcpm attempts to mount a DCPM device at the specified mountpoint.
func (p *Provider) mountDcpm(device, target string) (*storage.MountResponse, error) {
	return p.mounter.Mount(storage.MountRequest{
		Source:     device,
		Target:     target,
		Filesystem: dcpmFsType,
		Flags:      defaultMountFlags,
		Options:    dcpmMountOpts,
	})
}

// mountRamdisk attempts to mount a tmpfs-based ramdisk of the specified size at
// the specified mountpoint.
func (p *Provider) mountRamdisk(target string, params *storage.RamdiskParams) (*storage.MountResponse, error) {
	if params == nil {
		return nil, FaultFormatMissingParam
	}

	var opts = []string{
		// https://www.kernel.org/doc/html/latest/filesystems/tmpfs.html
		// mpol=prefer:Node prefers to allocate memory from the given Node
		fmt.Sprintf("mpol=prefer:%d", params.NUMANode),
	}
	if params.Size > 0 {
		opts = append(opts, fmt.Sprintf("size=%dg", params.Size))
	}
	if !params.DisableHugepages {
		opts = append(opts, "huge=always")
	}

	return p.mounter.Mount(storage.MountRequest{
		Source:     ramFsType,
		Target:     target,
		Filesystem: ramFsType,
		Flags:      defaultMountFlags,
		Options:    strings.Join(opts, ","),
	})
}

// Mount attempts to mount the target specified in the supplied request.
func (p *Provider) Mount(req storage.ScmMountRequest) (*storage.MountResponse, error) {
	switch req.Class {
	case storage.ClassDcpm:
		return p.mountDcpm(req.Device, req.Target)
	case storage.ClassRam:
		return p.mountRamdisk(req.Target, req.Ramdisk)
	default:
		return nil, errors.New(storage.ScmMsgClassNotSupported)
	}
}

// Unmount attempts to unmount the target specified in the supplied request.
func (p *Provider) Unmount(req storage.ScmMountRequest) (*storage.MountResponse, error) {
	return p.mounter.Unmount(storage.MountRequest{
		Target: req.Target,
	})
}

// Stat probes the specified path and returns os level file info.
func (p *Provider) Stat(path string) (os.FileInfo, error) {
	return p.sys.Stat(path)
}

// IsMounted checks to see if the target device or directory is mounted and
// returns flag to specify whether mounted or a relevant fault.
func (p *Provider) IsMounted(target string) (bool, error) {
	return p.mounter.IsMounted(target)
}
