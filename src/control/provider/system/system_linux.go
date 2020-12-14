//
// (C) Copyright 2019-2020 Intel Corporation.
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
	"path/filepath"
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
	miNumFields
)

func scanMountInfo(input io.Reader, target string, scanField int) (bool, error) {
	scn := bufio.NewScanner(input)
	for scn.Scan() {
		fields := strings.Fields(scn.Text())
		if len(fields) < miNumFields {
			continue
		}
		if fields[scanField] == target {
			return true, nil
		}
	}

	return false, scn.Err()
}

func resolveDeviceMount(device string) (string, error) {
	fi, err := os.Stat(device)
	if err != nil {
		return "", err
	}

	majorMinor, err := resolveDevice(fi)
	if err != nil {
		return "", err
	}

	mi, err := os.Open("/proc/self/mountinfo")
	if err != nil {
		return "", err
	}
	defer mi.Close()

	scn := bufio.NewScanner(mi)
	for scn.Scan() {
		fields := strings.Fields(scn.Text())
		if len(fields) < miNumFields {
			continue
		}
		if fields[miMajorMinor] == majorMinor {
			return fields[miMountPoint], nil
		}
	}

	return "", errors.Errorf("Unable to resolve %s to mountpoint", device)
}

func resolveDevice(fi os.FileInfo) (string, error) {
	if fi.Mode()&os.ModeDevice == 0 {
		return "", errors.Errorf("%s is not a device", fi.Name())
	}
	st, ok := fi.Sys().(*syscall.Stat_t)
	if !ok {
		return "", errors.Errorf("unable to get underlying stat for %v", fi)
	}
	return fmt.Sprintf("%d:%d", st.Rdev/256, st.Rdev%256), nil
}

func isDevice(target string) bool {
	fi, err := os.Stat(target)
	if err != nil {
		return false
	}

	return fi.Mode()&os.ModeDevice != 0
}

// IsMounted checks the target device or directory for mountedness.
func (s LinuxProvider) IsMounted(target string) (bool, error) {
	fi, err := os.Stat(target)
	if err != nil {
		return false, err
	}

	var scanField int
	switch {
	case fi.IsDir():
		target = filepath.Clean(target)
		scanField = miMountPoint
	case fi.Mode()&os.ModeDevice != 0:
		scanField = miMajorMinor
		target, err = resolveDevice(fi)
		if err != nil {
			return false, err
		}
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
	// umount(2) in Linux doesn't allow unmount of a block device
	if isDevice(target) {
		mntPoint, err := resolveDeviceMount(target)
		if err != nil {
			return err
		}
		target = mntPoint
	}
	return unix.Unmount(target, flags)
}

// GetfsUsage retrieves total and available byte counts for a mountpoint.
func (s LinuxProvider) GetfsUsage(target string) (uint64, uint64, error) {
	stBuf := new(unix.Statfs_t)

	if err := unix.Statfs(target, stBuf); err != nil {
		return 0, 0, err
	}

	frSize := uint64(stBuf.Frsize)

	return frSize * stBuf.Blocks, frSize * stBuf.Bavail, nil
}
