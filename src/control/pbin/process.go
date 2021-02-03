//
// (C) Copyright 2020-2021 Intel Corporation.
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
	pPath, err := os.Readlink(fmt.Sprintf("/proc/%d/exe", os.Getppid()))
	if err != nil {
		return "", errors.Wrap(err, "failed to identify parent process binary")
	}

	return filepath.Base(pPath), nil
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
