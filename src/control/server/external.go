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

import (
	"fmt"
	"os"
	"os/user"
	"path/filepath"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
)

const (
	msgUnmount      = "syscall: calling unmount with %s, MNT_DETACH"
	msgMount        = "syscall: mount %s, %s, %s, %s, %s"
	msgIsMountPoint = "check if dir %s is mounted"
	msgExists       = "os: stat %s"
	msgMkdir        = "os: mkdirall %s, 0777"
	msgRemove       = "os: removeall %s"
	msgCmd          = "cmd: %s"
	msgChownR       = "os: walk %s chown %d %d"
)

// External interface provides methods to support various os operations.
type External interface {
	getAbsInstallPath(string) (string, error)
	lookupUser(string) (*user.User, error)
	lookupGroup(string) (*user.Group, error)
	listGroups(*user.User) ([]string, error)
	chownR(string, int, int) error
	checkSudo() (bool, string)
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

func (e *ext) getAbsInstallPath(path string) (string, error) {
	return common.GetAdjacentPath(path)
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

func (e *ext) chownR(root string, uid int, gid int) error {
	op := fmt.Sprintf(msgChownR, root, uid, gid)

	e.history = append(e.history, op)

	return filepath.Walk(root, func(name string, info os.FileInfo, err error) error {
		if err != nil {
			return errors.Wrapf(err, "accessing path %s", name)
		}

		return os.Chown(name, uid, gid)
	})
}

func (e *ext) checkSudo() (bool, string) {
	usr := os.Getenv("SUDO_USER")
	if usr == "" {
		usr = "root"
	}

	return (os.Geteuid() == 0), usr
}
