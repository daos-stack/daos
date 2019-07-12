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

package server

//#include <unistd.h>
//#include <errno.h>
import "C"

import (
	"fmt"
	"os"
	"os/user"
	"path/filepath"
	"strings"
	"syscall"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/log"
)

const (
	msgUnmount      = "syscall: calling unmount with %s, MNT_DETACH"
	msgMount        = "syscall: mount %s, %s, %s, %s, %s"
	msgIsMountPoint = "check if dir %s is mounted"
	msgExists       = "os: stat %s"
	msgMkdir        = "os: mkdirall %s, 0777"
	msgRemove       = "os: removeall %s"
	msgCmd          = "cmd: %s"
	msgSetUID       = "C: setuid %d"
	msgSetGID       = "C: setgid %d"
	msgChown        = "os: chown %s %d %d"
)

// External interface provides methods to support various os operations.
type External interface {
	runCommand(string) error
	writeToFile(string, string) error
	createEmpty(string, int64) error
	mount(string, string, string, uintptr, string) error
	isMountPoint(string) (bool, error)
	unmount(string) error
	mkdir(string) error
	remove(string) error
	exists(string) (bool, error)
	getAbsInstallPath(string) (string, error)
	lookupUser(string) (*user.User, error)
	lookupGroup(string) (*user.Group, error)
	listGroups(*user.User) ([]string, error)
	setUID(int64) error
	setGID(int64) error
	chown(string, int, int) error
	getHistory() []string
}

func errPermsAnnotate(err error) (e error) {
	e = err
	if os.IsPermission(e) {
		e = errors.WithMessagef(
			e,
			"%s requires elevated privileges to perform this action",
			os.Args[0])
	}
	return
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
func (e *ext) createEmpty(path string, size int64) error {
	if !filepath.IsAbs(path) {
		return errors.Errorf("please specify absolute path (%s)", path)
	}

	if _, err := os.Stat(path); !os.IsNotExist(err) {
		return err
	}

	file, err := common.TruncFile(path)
	if err != nil {
		return err
	}
	defer file.Close()

	if err := syscall.Fallocate(int(file.Fd()), 0, 0, size); err != nil {
		e, ok := err.(syscall.Errno)
		if ok && (e == syscall.ENOSYS || e == syscall.EOPNOTSUPP) {
			log.Debugf(
				"Warning: Fallocate not supported, attempting Truncate: ", e)

			if err := file.Truncate(size); err != nil {
				return err
			}
		}
	}

	return nil
}

// NOTE: requires elevated privileges
func (e *ext) mount(
	dev string, mount string, mntType string, flags uintptr, opts string) error {

	op := fmt.Sprintf(msgMount, dev, mount, mntType, fmt.Sprint(flags), opts)

	log.Debugf(op)
	e.history = append(e.history, op)

	if flags == 0 {
		flags = uintptr(syscall.MS_NOATIME | syscall.MS_SILENT)
		flags |= syscall.MS_NODEV | syscall.MS_NOEXEC | syscall.MS_NOSUID
	}

	if err := syscall.Mount(dev, mount, mntType, flags, opts); err != nil {
		return errPermsAnnotate(os.NewSyscallError("mount", err))
	}
	return nil
}

// isMountPoint checks if path is likely to be a mount point.straiowhotenoul
func (e *ext) isMountPoint(path string) (bool, error) {
	log.Debugf(msgIsMountPoint, path)
	e.history = append(e.history, fmt.Sprintf(msgIsMountPoint, path))

	pStat, err := os.Stat(path)
	if err != nil {
		return false, err
	}

	rStat, err := os.Stat(filepath.Dir(strings.TrimSuffix(path, "/")))
	if err != nil {
		return false, err
	}

	if pStat.Sys().(*syscall.Stat_t).Dev == rStat.Sys().(*syscall.Stat_t).Dev {
		return false, nil
	}

	// if root dir has different parent device than path then probably a mountpoint
	return true, nil
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

		return errPermsAnnotate(os.NewSyscallError("umount", err))
	}
	return nil
}

// NOTE: may require elevated privileges
func (e *ext) mkdir(path string) error {
	log.Debugf(msgMkdir, path)
	e.history = append(e.history, fmt.Sprintf(msgMkdir, path))

	if err := os.MkdirAll(path, 0777); err != nil {
		return errPermsAnnotate(errors.WithMessage(err, "mkdir"))
	}
	return nil
}

// NOTE: may require elevated privileges
func (e *ext) remove(path string) error {
	log.Debugf(msgRemove, path)
	e.history = append(e.history, fmt.Sprintf(msgRemove, path))

	// ignore NOENT errors, treat as success
	if err := os.RemoveAll(path); err != nil && !os.IsNotExist(err) {
		return errPermsAnnotate(errors.WithMessage(err, "remove"))
	}
	return nil
}

func (e *ext) exists(path string) (bool, error) {
	log.Debugf(msgExists, path)
	e.history = append(e.history, fmt.Sprintf(msgExists, path))

	if _, err := os.Stat(path); err == nil {
		return true, nil
	} else if !os.IsNotExist(err) {
		return false, errPermsAnnotate(
			errors.WithMessage(err, "os stat"))
	}

	return false, nil
}

func (e *ext) getAbsInstallPath(path string) (string, error) {
	return common.GetAbsInstallPath(path)
}

func (e *ext) lookupUser(userName string) (*user.User, error) {
	return user.Lookup(userName)
}

func (e *ext) lookupGroup(groupName string) (*user.Group, error) {
	return user.LookupGroup(groupName)
}

func (e *ext) listGroups(usr *user.User) ([]string, error) {
	return usr.GroupIds()
}

func (e *ext) setUID(uid int64) error {
	log.Debugf(msgSetUID, uid)
	e.history = append(e.history, fmt.Sprintf(msgSetUID, uid))

	if cerr, errno := C.setuid(C.__uid_t(uid)); cerr != 0 {
		return errors.Errorf("C.setuid rc: %d, errno: %d", cerr, errno)
	}
	return nil
}

func (e *ext) setGID(gid int64) error {
	log.Debugf(msgSetGID, gid)
	e.history = append(e.history, fmt.Sprintf(msgSetGID, gid))

	if cerr, errno := C.setgid(C.__gid_t(gid)); cerr != 0 {
		return errors.Errorf("C.setgid rc: %d, errno: %d", cerr, errno)
	}
	return nil
}

func (e *ext) chown(path string, uid int, gid int) error {
	log.Debugf(msgChown, path, uid, gid)
	e.history = append(e.history, fmt.Sprintf(msgChown, path, uid, gid))

	return os.Chown(path, uid, gid)
}
