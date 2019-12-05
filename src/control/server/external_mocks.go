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
	"os/user"
	"sync"
)

// mockExt implements the External interface.
type mockExt struct {
	sync.RWMutex
	// return error if cmd in shell fails
	cmdRet error
	// return true if file already exists
	existsRet       bool
	mountRet        error
	isMountPointRet bool
	unmountRet      error
	mkdirRet        error
	removeRet       error
	lUsrRet         *user.User  // lookup user
	lGrpRet         *user.Group // lookup group
	lUsrErr         error       // lookup user error
	lGrpErr         error       // lookup group error
	listGrpsErr     error       // list groups error
	listGrpsRet     []string    // list of user's groups
	chownRErr       error
	isRoot          bool
	history         []string
	files           []string
}

func (m *mockExt) getHistory() []string {
	m.RLock()
	defer m.RUnlock()

	return m.history
}

func (m *mockExt) appendHistory(update string) {
	m.Lock()
	defer m.Unlock()

	m.history = append(m.history, update)
}

func (m *mockExt) appendFiles(update string) {
	m.Lock()
	defer m.Unlock()

	m.files = append(m.files, update)
}

func (m *mockExt) runCommand(cmd string) error {
	m.appendHistory(fmt.Sprintf(msgCmd, cmd))

	return m.cmdRet
}

func (m *mockExt) writeToFile(in string, outPath string) error {
	m.appendFiles(fmt.Sprint(outPath, ":", in))

	return nil
}

func (m *mockExt) createEmpty(path string, size int64) error {
	if !m.existsRet {
		m.appendFiles(fmt.Sprint(path, ":empty size ", size))
	}
	return nil
}

func (m *mockExt) mount(
	dev string, mount string, typ string, flags uintptr, opts string) error {

	op := fmt.Sprintf(msgMount, dev, mount, typ, fmt.Sprint(flags), opts)

	m.appendHistory(op)

	return m.mountRet
}

func (m *mockExt) isMountPoint(path string) (bool, error) {
	m.appendHistory(fmt.Sprintf(msgIsMountPoint, path))

	return m.isMountPointRet, nil
}

func (m *mockExt) unmount(path string) error {
	m.appendHistory(fmt.Sprintf(msgUnmount, path))

	return m.unmountRet
}

func (m *mockExt) mkdir(path string) error {
	m.appendHistory(fmt.Sprintf(msgMkdir, path))

	return m.mkdirRet
}

func (m *mockExt) remove(path string) error {
	m.appendHistory(fmt.Sprintf(msgRemove, path))

	return m.removeRet
}

func (m *mockExt) exists(string) (bool, error) {
	return m.existsRet, nil
}

func (m *mockExt) getAbsInstallPath(path string) (string, error) {
	return path, nil
}

func (m *mockExt) lookupUser(name string) (*user.User, error) {
	return m.lUsrRet, m.lUsrErr
}

func (m *mockExt) lookupGroup(name string) (*user.Group, error) {
	return m.lGrpRet, m.lGrpErr
}

func (m *mockExt) listGroups(usr *user.User) ([]string, error) {
	return m.listGrpsRet, m.listGrpsErr
}

func (m *mockExt) checkSudo() (bool, string) {
	return m.isRoot, ""
}

func (m *mockExt) chownR(root string, uid int, gid int) error {
	m.appendHistory(fmt.Sprintf(msgChownR, root, uid, gid))

	return m.chownRErr
}

func newMockExt(
	cmdRet error, existsRet bool, mountRet error, isMountPointRet bool,
	unmountRet error, mkdirRet error, removeRet error, isRoot bool,
) External {

	return &mockExt{
		sync.RWMutex{},
		cmdRet, existsRet, mountRet, isMountPointRet, unmountRet, mkdirRet,
		removeRet, nil, nil, nil, nil, nil, []string{}, nil, isRoot,
		[]string{}, []string{},
	}
}

func defaultMockExt() External {
	return &mockExt{}
}
