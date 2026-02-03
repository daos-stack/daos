//
// (C) Copyright 2020-2021 Intel Corporation.
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pbin

import (
	"fmt"
	"os"
	"path/filepath"

	"github.com/pkg/errors"
)

// Process is a mechanism to interact with the current process.
type Process struct{}

// CurrentProcessName fetches the name of the running process.
func (p *Process) CurrentProcessName() string {
	return filepath.Base(os.Args[0])
}

// ParentProcessName fetches the name of the parent process, or returns an error otherwise.
func (p *Process) ParentProcessName() (string, error) {
	data, err := os.ReadFile(fmt.Sprintf("/proc/%d/comm", os.Getppid()))
	if err != nil || len(data) == 0 {
		pPath, err := os.Readlink(fmt.Sprintf("/proc/%d/exe", os.Getppid()))
		if err == nil {
			return filepath.Base(pPath), nil
		}
		return "", errors.Wrap(err, "failed to identify parent process binary")
	}

	return string(data[:len(data)-1]), nil // trim trailing newline
}

// IsPrivileged determines whether the process is running as a privileged user.
func (p *Process) IsPrivileged() bool {
	return os.Geteuid() == 0
}

// ElevatePrivileges raises the process privileges.
func (p *Process) ElevatePrivileges() error {
	if err := setuid(0); err != nil {
		return errors.Wrap(err, "unable to setuid(0)")
	}

	return nil
}
