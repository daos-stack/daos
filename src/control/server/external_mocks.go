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

	"github.com/pkg/errors"
)

// mockExt implements the External interface.
type mockExt struct {
	// return error if cmd in shell fails
	cmdRet error
	// return empty string if os env not set/set empty
	getenvRet string
	// return true if file already exists
	existsRet  bool
	mountRet   error
	unmountRet error
	mkdirRet   error
	removeRet  error
}

var (
	commands []string // record commands executed in mocks
	files    []string // record file content written in mocks
)

func (m *mockExt) runCommand(cmd string) error {
	commands = append(commands, cmd)

	return m.cmdRet
}

func (m *mockExt) getenv(key string) string { return m.getenvRet }

func (m *mockExt) writeToFile(in string, outPath string) error {
	files = append(files, fmt.Sprint(outPath, ":", in))

	return nil
}

func (m *mockExt) createEmpty(path string, size int64) error {
	if !m.existsRet {
		files = append(files, fmt.Sprint(path, ":empty size ", size))
	}
	return nil
}

func (m *mockExt) mount(
	dev string, mount string, typ string, flags uintptr, opts string) error {

	commands = append(
		commands,
		fmt.Sprintf("mount %s %s %s %s", dev, mount, typ, opts))

	return m.mountRet
}

func (m *mockExt) unmount(path string) error {
	commands = append(commands, "umount "+path)

	return m.unmountRet
}

func (m *mockExt) mkdir(path string) error {
	commands = append(commands, "mkdir "+path)

	return m.mkdirRet
}

func (m *mockExt) remove(path string) error {
	commands = append(commands, "remove "+path)

	return m.removeRet
}

func newMockExt(
	cmdRet error, getenvRet string, existsRet bool, mountRet error,
	unmountRet error, mkdirRet error, removeRet error) External {

	return &mockExt{
		cmdRet, getenvRet, existsRet, mountRet, unmountRet,
		mkdirRet, removeRet}
}

func defaultMockExt() External {
	return newMockExt(nil, "", false, nil, nil, nil, nil)
}

func cmdFailMockExt() External {
	return newMockExt(errors.New("exit status 1"), "", false, nil, nil, nil, nil)
}

func envExistsMockExt() External {
	return newMockExt(nil, "somevalue", false, nil, nil, nil, nil)
}
