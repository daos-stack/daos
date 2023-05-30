//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package mount

import (
	"os"
	"path/filepath"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/provider/system"
	"github.com/daos-stack/daos/src/control/server/storage"
)

const (
	defaultMountPointPerms = 0755
	defaultUnmountFlags    = 0
)

type (
	// SystemProvider defines a set of methods to be implemented by a provider
	// of system capabilities.
	SystemProvider interface {
		system.IsMountedProvider
		system.MountProvider
		system.UnmountProvider
		Chmod(string, os.FileMode) error
		Stat(string) (os.FileInfo, error)
	}

	// Provider provides methods to interact with a generic storage device.
	Provider struct {
		log logging.Logger
		sys SystemProvider
	}
)

// DefaultProvider returns an initialized *Provider suitable for use with production code.
func DefaultProvider(log logging.Logger) *Provider {
	return NewProvider(log, system.DefaultProvider())
}

// NewProvider returns an initialized *Provider.
func NewProvider(log logging.Logger, sys SystemProvider) *Provider {
	p := &Provider{
		log: log,
		sys: sys,
	}
	return p
}

// IsMounted checks whether the requested path is mounted.
func (p *Provider) IsMounted(target string) (bool, error) {
	if p == nil {
		return false, errors.New("nil provider")
	}

	isMounted, err := p.sys.IsMounted(target)

	if errors.Is(err, os.ErrPermission) {
		return false, errors.Wrap(storage.FaultPathAccessDenied(target), "check if mounted")
	}

	return isMounted, err
}

// Mount mounts a given device with a set of arguments.
func (p *Provider) Mount(req storage.MountRequest) (*storage.MountResponse, error) {
	if p == nil {
		return nil, errors.New("nil mount provider")
	}
	return p.mount(req.Source, req.Target, req.Filesystem, req.Flags, req.Options)
}

func (p *Provider) mount(src, target, fsType string, flags uintptr, opts string) (*storage.MountResponse, error) {
	// make sure that we're not double-mounting over an existing mount
	tgtMounted, err := p.IsMounted(target)
	if err != nil {
		return nil, err
	}
	if tgtMounted {
		return nil, errors.Wrap(storage.FaultTargetAlreadyMounted, target)
	}

	p.log.Debugf("mount %s->%s (%s) (%s)", src, target, fsType, opts)
	if err := p.sys.Mount(src, target, fsType, flags, opts); err != nil {
		return nil, errors.Wrapf(err, "mount %s->%s failed", src, target)
	}

	// Adjust permissions on the mounted filesystem.
	if err := p.sys.Chmod(target, defaultMountPointPerms); err != nil {
		return nil, errors.Wrapf(err, "failed to set permissions on %s", target)
	}

	return &storage.MountResponse{
		Target:  target,
		Mounted: true,
	}, nil
}

// Unmount unmounts a device from a mount point.
func (p *Provider) Unmount(req storage.MountRequest) (*storage.MountResponse, error) {
	if p == nil {
		return nil, errors.New("nil mount provider")
	}

	if err := p.sys.Unmount(req.Target, int(req.Flags)); err != nil {
		return nil, errors.Wrapf(err, "failed to unmount %s", req.Target)
	}

	return &storage.MountResponse{
		Target:  req.Target,
		Mounted: false,
	}, nil
}

// ClearMountpoint unmounts and removes a mountpoint.
func (p *Provider) ClearMountpoint(mntpt string) error {
	mounted, err := p.IsMounted(mntpt)
	if os.IsNotExist(err) {
		return nil
	}
	if err != nil {
		return err
	}

	if mounted {
		_, err := p.Unmount(storage.MountRequest{
			Target: mntpt,
			Flags:  defaultUnmountFlags,
		})
		if err != nil {
			return err
		}
	}

	if err := os.RemoveAll(mntpt); err != nil && !os.IsNotExist(err) {
		return errors.Wrapf(err, "failed to remove %s", mntpt)
	}

	return nil
}

// MakeMountPath will build the target path by creating any non-existent
// subdirectories and setting ownership to target uid/gid to ensure that the
// target user is able to access the created mount point if multiple layers
// deep. If subdirectories in path already exist, their permissions will not be
// modified.
func (p *Provider) MakeMountPath(path string, tgtUID, tgtGID int) error {
	if !filepath.IsAbs(path) {
		return errors.Errorf("expecting absolute target path, got %q", path)
	}
	if _, err := p.sys.Stat(path); err == nil {
		return nil // path already exists
	}

	sep := string(filepath.Separator)
	dirs := strings.Split(path, sep)[1:] // omit empty element

	for i := range dirs {
		ps := sep + filepath.Join(dirs[:i+1]...)
		_, err := p.sys.Stat(ps)
		switch {
		case os.IsNotExist(err):
			// subdir missing, attempt to create and chown
			if err := os.Mkdir(ps, defaultMountPointPerms); err != nil {
				return errors.Wrapf(err, "failed to create directory %q", ps)
			}
			if err := os.Chown(ps, tgtUID, tgtGID); err != nil {
				return errors.Wrapf(err, "failed to set ownership of %s to %d.%d from %d.%d",
					ps, tgtUID, tgtGID, os.Geteuid(), os.Getegid())
			}
		case err != nil:
			return errors.Wrapf(err, "unable to stat %q", ps)
		}
	}

	return nil
}
