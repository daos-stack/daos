//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package storage

type (
	// MountProvider defines an interface to be implemented by a mount/unmount provider.
	MountProvider interface {
		Mount(MountRequest) (*MountResponse, error)
		Unmount(MountRequest) (*MountResponse, error)
		IsMounted(string) (bool, error)
		ClearMountpoint(string) error
		MakeMountPath(path string, tgtUID, tgtGID int) error
	}

	// MountRequest represents a generic storage mount/unmount request.
	MountRequest struct {
		Source     string
		Target     string
		Filesystem string
		Flags      uintptr
		Options    string
	}

	// MountResponse indicates a successful mount or unmount operation on a generic device.
	MountResponse struct {
		Target  string
		Mounted bool
	}
)
