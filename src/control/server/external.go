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

package main

import (
	"fmt"
	"os"
	"path/filepath"
	"syscall"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/log"
	"github.com/pkg/errors"
)

const (
	msgUnmount = "syscall: calling unmount with %s, MNT_DETACH"
	msgMount   = "syscall: mount %s, %s, %s, %s, %s"
	msgMkdir   = "os: mkdirall %s, 0777"
	msgRemove  = "os: removeall %s"
	msgCmd     = "cmd: %s"
)

// External interface provides methods to support various os operations.
type External interface {
	runCommand(string) error
	writeToFile(string, string) error
	createEmpty(string, int64) error
	mount(string, string, string, uintptr, string) error
	unmount(string) error
	mkdir(string) error
	remove(string) error
	getHistory() []string
	exists(string) (bool, error)
	getAbsInstallPath(string) (string, error)
}

type ext struct {
	history []string
}

func (e *ext) getHistory() []string {
	return e.history
}

// runCommand executes command in subshell (to allow redirection) and returns
// error result.
func (e *ext) runCommand(cmd string) error {
	e.history = append(e.history, fmt.Sprintf(msgCmd, cmd))

	return common.Run(cmd)
}

// writeToFile wraps around common.WriteString and writes input string to given
// file path.
func (e *ext) writeToFile(contents string, path string) error {
	return common.WriteString(path, contents)
}

// createEmpty creates a file (if it doesn't exist) of specified size in bytes
// at the given path.
// If Fallocate not supported by kernel or backing fs, fall back to Truncate.
func (e *ext) createEmpty(path string, size int64) (err error) {
	if !filepath.IsAbs(path) {
		return errors.Errorf("please specify absolute path (%s)", path)
	}
	if _, err = os.Stat(path); !os.IsNotExist(err) {
		return
	}
	file, err := common.TruncFile(path)
	if err != nil {
		return
	}
	defer file.Close()
	if err := syscall.Fallocate(int(file.Fd()), 0, 0, size); err != nil {
		e, ok := err.(syscall.Errno)
		if ok && (e == syscall.ENOSYS || e == syscall.EOPNOTSUPP) {
			log.Debugf(
				"Warning: Fallocate not supported, attempting Truncate: ", e)
			err = file.Truncate(size)
		}
	}
	return
}

// NOTE: requires elevated privileges
func (e *ext) mount(
	dev string, mount string, mntType string, flags uintptr, opts string) error {

	op := fmt.Sprintf(msgMount, dev, mount, mntType, fmt.Sprint(flags), opts)

	log.Debugf(op)
	e.history = append(e.history, op)

	if err := syscall.Mount(dev, mount, mntType, flags, opts); err != nil {
		return os.NewSyscallError("mount", err)
	}
	return nil
}

// NOTE: requires elevated privileges, lazy unmount, mntpoint may not be
//       available immediately after
func (e *ext) unmount(path string) error {
	log.Debugf(msgUnmount, path)
	e.history = append(e.history, fmt.Sprintf(msgUnmount, path))

	// ignore NOENT errors, treat as success
	if err := syscall.Unmount(
		path, syscall.MNT_DETACH); err != nil && !os.IsNotExist(err) {

		// when mntpoint exists but is unmounted, get EINVAL
		e, ok := err.(syscall.Errno)
		if ok && e == syscall.EINVAL {
			return nil
		}

		return os.NewSyscallError("umount", err)
	}
	return nil
}

// NOTE: may require elevated privileges
func (e *ext) mkdir(path string) error {
	log.Debugf(msgMkdir, path)
	e.history = append(e.history, fmt.Sprintf(msgMkdir, path))

	if err := os.MkdirAll(path, 0777); err != nil {
		return errors.WithMessage(err, "mkdir")
	}
	return nil
}

// NOTE: may require elevated privileges
func (e *ext) remove(path string) error {
	log.Debugf(msgRemove, path)
	e.history = append(e.history, fmt.Sprintf(msgRemove, path))

	// ignore NOENT errors, treat as success
	if err := os.RemoveAll(path); err != nil && !os.IsNotExist(err) {
		return errors.WithMessage(err, "remove")
	}
	return nil
}

func (e *ext) exists(path string) (bool, error) {
	if _, err := os.Stat(path); err == nil {
		return true, nil
	} else if !os.IsNotExist(err) {
		return false, errors.Wrap(err, "os stat")
	}

	return false, nil
}

func (e *ext) getAbsInstallPath(path string) (string, error) {
	return common.GetAbsInstallPath(path)
}
