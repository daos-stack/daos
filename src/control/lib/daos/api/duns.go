//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package api

import (
	"context"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/logging"
)

/*
#include <daos.h>
#include <daos_uns.h>
*/
import "C"

type (
	// PathInfo contains DAOS pool and container information resolved from a path.
	PathInfo struct {
		// PoolID is the pool UUID or label string.
		PoolID string
		// ContainerID is the container UUID or label string.
		ContainerID string
		// Layout is the container layout type (POSIX, HDF5, etc.).
		Layout daos.ContainerLayout
		// RelPath is the relative path from the UNS entry to the resolved path.
		RelPath string
	}
)

// ResolvePathInfo resolves the given path to pool and container information.
// The path may be:
//   - A path with a DUNS extended attribute (e.g., /mnt/lustre/mypool/mycont)
//   - A direct path in the format daos://pool/container/path
//   - A direct path in the format /pool/container/path (with appropriate flags)
//
// The caller should check the RelPath field to determine if the path
// resolved to a location within a container.
func ResolvePathInfo(path string) (*PathInfo, error) {
	if path == "" {
		return nil, errors.Wrap(daos.InvalidInput, "empty path")
	}

	cPath := C.CString(path)
	defer freeString(cPath)

	var attr C.struct_duns_attr_t
	if err := dunsError(duns_resolve_path(cPath, &attr)); err != nil {
		return nil, errors.Wrapf(err, "failed to resolve path %q", path)
	}
	defer duns_destroy_attr(&attr)

	info := &PathInfo{
		PoolID:      C.GoString(&attr.da_pool[0]),
		ContainerID: C.GoString(&attr.da_cont[0]),
		Layout:      daos.ContainerLayout(attr.da_type),
	}

	if attr.da_rel_path != nil {
		info.RelPath = C.GoString(attr.da_rel_path)
	}

	return info, nil
}

// LinkContainerAtPath creates a filesystem link at the specified path pointing to the container.
func (ph *PoolHandle) LinkContainerAtPath(ctx context.Context, containerID, path string) error {
	if !ph.IsValid() {
		return ErrInvalidPoolHandle
	}
	logging.FromContext(ctx).Debugf("PoolHandle.LinkContainerAtPath(%s, %s, %s)", ph, containerID, path)

	return ContainerLinkAtPath(ph.toCtx(ctx), "", "", containerID, path)
}

// ContainerLinkAtPath creates a filesystem link at the specified path pointing to an
// existing container. This creates a special directory (for POSIX containers)
// or file (for HDF5 containers) with an extended attribute containing the
// pool and container information.
//
// Note: Multiple paths may link to the same container, but destroying any
// linked path will also destroy the container. The caller is responsible
// for managing path lifecycle.
func ContainerLinkAtPath(ctx context.Context, sysName, poolID, containerID, path string) error {
	if containerID == "" {
		return errors.Wrap(daos.InvalidInput, "empty container ID")
	}
	if path == "" {
		return errors.Wrap(daos.InvalidInput, "empty path")
	}

	poolConn, cleanup, err := getPoolConn(ctx, sysName, poolID, daos.PoolConnectFlagReadOnly)
	if err != nil {
		return err
	}
	defer cleanup()
	logging.FromContext(ctx).Debugf("ContainerLinkAtPath(%s, %s, %s)", poolConn, containerID, path)

	cContID := C.CString(containerID)
	defer freeString(cContID)
	cPath := C.CString(path)
	defer freeString(cPath)

	if err := dunsError(duns_link_cont(poolConn.daosHandle, cContID, cPath)); err != nil {
		return errors.Wrapf(err, "failed to link container %q to path %q", containerID, path)
	}

	return nil
}

// DestroyContainerAtPath destroys the container linked at the specified path.
func (ph *PoolHandle) DestroyContainerAtPath(ctx context.Context, path string) error {
	if !ph.IsValid() {
		return ErrInvalidPoolHandle
	}
	logging.FromContext(ctx).Debugf("PoolHandle.DestroyContainerAtPath(%s, %s)", ph, path)

	return ContainerDestroyAtPath(ph.toCtx(ctx), "", "", path)
}

// ContainerDestroyAtPath destroys the container linked at the specified path and
// removes the path from the namespace. This is a destructive operation that
// cannot be undone.
//
// Note: If multiple paths link to the same container, destroying one path
// will destroy the container, leaving other paths as dangling references.
func ContainerDestroyAtPath(ctx context.Context, sysName, poolID, path string) error {
	if path == "" {
		return errors.Wrap(daos.InvalidInput, "empty path")
	}

	poolConn, cleanup, err := getPoolConn(ctx, sysName, poolID, daos.PoolConnectFlagReadWrite)
	if err != nil {
		if errors.Is(err, daos.NoPermission) {
			poolConn, cleanup, err = getPoolConn(ctx, sysName, poolID, daos.PoolConnectFlagReadOnly)
		}
		if err != nil {
			return err
		}
	}
	defer cleanup()
	logging.FromContext(ctx).Debugf("ContainerDestroyAtPath(%s, %s)", poolConn, path)

	cPath := C.CString(path)
	defer freeString(cPath)

	if err := dunsError(duns_destroy_path(poolConn.daosHandle, cPath)); err != nil {
		return errors.Wrapf(err, "failed to destroy path %q", path)
	}

	return nil
}
