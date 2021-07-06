//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system

type (
	// IsMountedProvider is the interface that wraps the IsMounted method,
	// which can be provided by a system-specific implementation or a mock.
	IsMountedProvider interface {
		IsMounted(target string) (bool, error)
	}
	// MountProvider is the interface that wraps the Mount method, which
	// can be provided by a system-specific implementation or a mock.
	MountProvider interface {
		Mount(source, target, fstype string, flags uintptr, data string) error
	}
	// UnmountProvider is the interface that wraps the Unmount method, which
	// can be provided by a system-specific implementation or a mock.
	UnmountProvider interface {
		Unmount(target string, flags int) error
	}
)
