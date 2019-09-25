//
// (C) Copyright 2019 Intel Corporation.
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

package system

import (
	"bufio"
	"fmt"
	"io"
	"os"
	"strings"
	"syscall"

	"github.com/pkg/errors"
	"golang.org/x/sys/unix"
)

// DefaultProvider returns the package-default provider implementation.
func DefaultProvider() *LinuxProvider {
	return &LinuxProvider{}
}

// LinuxProvider encapsulates Linux-specific implementations of system
// interfaces.
type LinuxProvider struct{}

// mountId,parentId,major:minor,root,mountPoint
const (
	_ int = iota
	_
	miMajorMinor
	_
	miMountPoint
	miLastField
)

func scanMountInfo(input io.Reader, target string, scanField int) (bool, error) {
	scn := bufio.NewScanner(input)
	for scn.Scan() {
		fields := strings.Fields(scn.Text())
		if len(fields) < miLastField {
			continue
		}
		if fields[scanField] == target {
			return true, nil
		}
	}

	return false, scn.Err()
}

// IsMounted checks the target device or directory for mountedness.
func (s LinuxProvider) IsMounted(target string) (bool, error) {
	st, err := os.Stat(target)
	if err != nil {
		return false, err
	}

	var scanField int
	switch {
	case st.IsDir():
		scanField = miMountPoint
	case st.Mode()&os.ModeDevice != 0:
		scanField = miMajorMinor
		st, ok := st.Sys().(*syscall.Stat_t)
		if !ok {
			return false, errors.Errorf("unable to get underlying stat for %v", st)
		}
		target = fmt.Sprintf("%d:%d", st.Rdev/256, st.Rdev%256)
	default:
		return false, errors.Errorf("not a valid mount target: %q", target)
	}

	mi, err := os.Open("/proc/self/mountinfo")
	if err != nil {
		return false, err
	}
	defer mi.Close()

	return scanMountInfo(mi, target, scanField)
}

// Mount provides an implementation of Mounter which calls the system implementation.
func (s LinuxProvider) Mount(source, target, fstype string, flags uintptr, data string) error {
	return unix.Mount(source, target, fstype, flags, data)
}

// Unmount provides an implementation of Unmounter which calls the system implementation.
func (s LinuxProvider) Unmount(target string, flags int) error {
	return unix.Unmount(target, flags)
}
