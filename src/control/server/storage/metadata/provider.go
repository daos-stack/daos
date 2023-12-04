//
// (C) Copyright 2022-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package metadata

import (
	"os"
	"path/filepath"
	"strings"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/provider/system"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/mount"
	"github.com/pkg/errors"
)

// SystemProvider provides operating system capabilities.
type SystemProvider interface {
	system.IsMountedProvider
	system.MountProvider
	Mkfs(req system.MkfsReq) error
	Getfs(device string) (string, error)
	Chown(string, int, int) error
	Stat(string) (os.FileInfo, error)
	GetFsType(path string) (*system.FsType, error)
}

const defaultDevFS = "ext4"

// Provider provides management functionality for metadata storage.
type Provider struct {
	log     logging.Logger
	sys     SystemProvider
	mounter storage.MountProvider
}

// Format formats the storage used for control metadata, if it is a separate device.
// If the storage location is on an existing partition, the format of the existing filesystem is
// checked.
func (p *Provider) Format(req storage.MetadataFormatRequest) error {
	if p == nil {
		return errors.New("nil metadata provider")
	}

	p.log.Debugf("Format(%+v)", req)

	if req.RootPath == "" {
		return errors.New("no control metadata root path specified")
	}

	if !strings.HasPrefix(req.DataPath, req.RootPath) {
		return errors.New("control metadata data path is not a subdirectory of the metadata root")
	}

	if hasDevice(req) {
		if err := p.setupMountPoint(req); err != nil {
			return err
		}
	} else {
		if err := p.setupRootDir(req); err != nil {
			return err
		}
	}

	if err := p.setupDataDir(req); err != nil {
		return err
	}

	if hasDevice(req) {
		if _, err := p.Unmount(storage.MetadataMountRequest{
			RootPath: req.RootPath,
		}); err != nil {
			return errors.Wrap(err, "unmounting formatted control metadata device")
		}
	}

	return nil
}

func hasDevice(req storage.MetadataFormatRequest) bool {
	return req.Device != ""
}

func (p *Provider) setupMountPoint(req storage.MetadataFormatRequest) error {
	p.log.Debugf("clearing control metadata mount point %q", req.RootPath)
	if err := p.mounter.ClearMountpoint(req.RootPath); err != nil {
		return errors.Wrap(err, "clearing control metadata mount point")
	}

	p.log.Debugf("creating control metadata mount point %q", req.RootPath)
	if err := p.mounter.MakeMountPath(req.RootPath, req.OwnerUID, req.OwnerGID); err != nil {
		return errors.Wrap(err, "creating control metadata mount point")
	}

	p.log.Debugf("formatting device %q", req.Device)
	if err := p.sys.Mkfs(system.MkfsReq{
		Filesystem: defaultDevFS,
		Device:     req.Device,
		Force:      true,
	}); err != nil {
		return errors.Wrap(err, "formatting control metadata device filesystem")
	}

	p.log.Debugf("mounting device %q at mount point %q", req.Device, req.RootPath)
	if _, err := p.Mount(storage.MetadataMountRequest{
		RootPath: req.RootPath,
		Device:   req.Device,
	}); err != nil {
		return errors.Wrap(err, "mounting control metadata device")
	}

	return nil
}

func (p *Provider) setupRootDir(req storage.MetadataFormatRequest) error {
	fsPath := req.RootPath
	for {
		fsType, err := p.sys.GetFsType(fsPath)
		if err == nil {
			if !p.isUsableFS(fsType, fsPath) {
				return FaultBadFilesystem(fsType)
			}
			break
		}

		if !os.IsNotExist(err) || fsPath == "/" {
			return errors.Wrapf(err, "getting filesystem type for %q", fsPath)
		}

		// Try the parent directory
		fsPath = filepath.Dir(fsPath)
	}

	p.log.Debugf("creating control metadata root dir %q", req.RootPath)
	if err := p.mounter.MakeMountPath(req.RootPath, req.OwnerUID, req.OwnerGID); err != nil {
		return errors.Wrap(err, "creating control metadata root")
	}

	return nil
}

func (p *Provider) isUsableFS(fs *system.FsType, path string) bool {
	if fs.NoSUID {
		// The daos_server_helper runs with the suid bit to set up the control metadata
		// device/directories, as some of these steps require superuser privileges.
		p.log.Errorf("cannot use filesystem with nosuid flag set (%s) for control metadata", path)
		return false
	}

	switch fs.Name {
	case system.FsTypeNfs:
		// If multiple servers use the same control_metadata directory on distributed storage,
		// they will corrupt and override each other's data. The control_metadata path is
		// strongly recommended to be on local storage.
		// There is another issue with NFS: The daos_server_helper also gets confusing failures
		// creating the directory when root squash is enabled. Root squash is the default for
		// NFS, and we unfortunately cannot easily detect the option at runtime. It is not
		// identical to the nosuid flag.
		p.log.Errorf("cannot use filesystem %s (%s) for control metadata", fs.Name, path)
		return false
	case system.FsTypeTmpfs, system.FsTypeExt4, system.FsTypeBtrfs, system.FsTypeNtfs, system.FsTypeXfs, system.FsTypeZfs:
		// Common local filesystems that we know will be okay.
		return true
	default:
		// As there are many filesystems that may be safely used, we'll allow others we don't recognize, with a
		// warning. Any distributed filesystem is subject to the same concerns as NFS above.
		p.log.Errorf("%q is not using one of the recommended filesystems for control metadata. If this is a distributed "+
			"filesystem, DAOS may not operate correctly.", path)
		return true
	}
}

func (p *Provider) setupDataDir(req storage.MetadataFormatRequest) error {
	if err := os.RemoveAll(req.DataPath); err != nil {
		return errors.Wrap(err, "removing old control metadata subdirectory")
	}

	if err := os.Mkdir(req.DataPath, 0755); err != nil {
		return errors.Wrap(err, "creating control metadata subdirectory")
	}

	if err := p.sys.Chown(req.DataPath, req.OwnerUID, req.OwnerGID); err != nil {
		return errors.Wrapf(err, "setting ownership of control metadata subdirectory to %d/%d", req.OwnerUID, req.OwnerGID)
	}

	for _, idx := range req.EngineIdxs {
		engPath := storage.ControlMetadataEngineDir(req.DataPath, idx)
		if err := os.Mkdir(engPath, 0755); err != nil {
			return errors.Wrapf(err, "creating control metadata engine %d subdirectory", idx)
		}

		if err := p.sys.Chown(engPath, req.OwnerUID, req.OwnerGID); err != nil {
			return errors.Wrapf(err, "setting ownership of control metadata engine %d subdirectory to %d/%d", idx, req.OwnerUID, req.OwnerGID)
		}
	}

	return nil
}

// NeedsFormat checks whether the metadata storage needs to be formatted.
func (p *Provider) NeedsFormat(req storage.MetadataFormatRequest) (out bool, _ error) {
	if p == nil {
		return false, errors.New("nil metadata provider")
	}

	defer func() {
		p.log.Debugf("NeedsFormat(%+v): %t", req, out)
	}()

	p.log.Debugf("checking if control metadata root (%s) exists", req.RootPath)
	if _, err := p.sys.Stat(req.RootPath); os.IsNotExist(err) {
		p.log.Debugf("control metadata root (%s) does not exist", req.RootPath)
		return true, nil
	} else if err != nil {
		return false, errors.Wrap(err, "checking control metadata root path existence")
	}

	alreadyMounted := false
	if req.Device != "" {
		p.log.Debugf("checking control metadata device filesystem")
		if fs, err := p.sys.Getfs(req.Device); err != nil {
			return false, errors.Wrap(err, "detecting filesystem on control metadata device")
		} else if fs != defaultDevFS {
			return true, nil
		}

		p.log.Debugf("attempting to mount control metadata device")
		if _, err := p.Mount(storage.MetadataMountRequest{
			RootPath: req.RootPath,
			Device:   req.Device,
		}); err != nil {
			if errors.Is(errors.Cause(err), storage.FaultTargetAlreadyMounted) {
				alreadyMounted = true
			} else {
				return false, errors.Wrap(err, "mounting control metadata device")
			}
		}
	}

	p.log.Debugf("checking if control metadata path (%s) exists", req.DataPath)
	if _, err := p.sys.Stat(req.DataPath); os.IsNotExist(err) {
		p.log.Debugf("control metadata path (%s) does not exist", req.DataPath)
		return true, nil
	} else if err != nil {
		return false, errors.Wrap(err, "checking control metadata path existence")
	}

	if hasDevice(req) && !alreadyMounted {
		if _, err := p.Unmount(storage.MetadataMountRequest{
			RootPath: req.RootPath,
		}); err != nil {
			return false, errors.Wrap(err, "unmounting formatted control metadata device")
		}
	}

	return false, nil
}

// Mount mounts the metadata device, if there is one. If an directory is being used instead,
// this is a no-op.
func (p *Provider) Mount(req storage.MetadataMountRequest) (*storage.MountResponse, error) {
	if p == nil {
		return nil, errors.New("nil metadata provider")
	}

	if req.RootPath == "" {
		return nil, errors.New("no control metadata root path specified")
	}

	if req.Device == "" {
		return &storage.MountResponse{
			Target: req.RootPath,
		}, nil
	}

	return p.mounter.Mount(storage.MountRequest{
		Source:     req.Device,
		Target:     req.RootPath,
		Filesystem: defaultDevFS,
	})
}

// Unmount unmounts the metadata device, if there is one. If an directory is being used instead,
// this is a no-op.
func (p *Provider) Unmount(req storage.MetadataMountRequest) (*storage.MountResponse, error) {
	if p == nil {
		return nil, errors.New("nil metadata provider")
	}

	if req.RootPath == "" {
		return nil, errors.New("no control metadata root path specified")
	}

	if isMounted, err := p.mounter.IsMounted(req.RootPath); err != nil {
		return nil, err
	} else if !isMounted {
		return &storage.MountResponse{
			Target: req.RootPath,
		}, nil
	}
	return p.mounter.Unmount(storage.MountRequest{Target: req.RootPath})
}

// NewProvider creates a Provider with the underlying data sources.
func NewProvider(log logging.Logger, sys SystemProvider, mount storage.MountProvider) *Provider {
	return &Provider{
		log:     log,
		sys:     sys,
		mounter: mount,
	}
}

// DefaultProvider creates a Provider with the default data sources.
func DefaultProvider(log logging.Logger) *Provider {
	return NewProvider(log, system.DefaultProvider(), mount.DefaultProvider(log))
}
