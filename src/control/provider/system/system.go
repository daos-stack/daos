//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system

import (
	"fmt"
	"os/exec"
)

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

	// RunCmdError documents the output of a command that has been run.
	RunCmdError struct {
		Wrapped error  // Error from the command run
		Stdout  string // Standard output from the command
	}
)

func (rce *RunCmdError) Error() string {
	if ee, ok := rce.Wrapped.(*exec.ExitError); ok {
		return fmt.Sprintf("%s: stdout: %s; stderr: %s", ee.ProcessState,
			rce.Stdout, ee.Stderr)
	}

	return fmt.Sprintf("%s: stdout: %s", rce.Wrapped.Error(), rce.Stdout)
}
