//
// (C) Copyright 2020 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package helper

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
