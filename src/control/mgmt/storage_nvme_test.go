//
// (C) Copyright 2018 Intel Corporation.
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

package mgmt_test

import (
	"fmt"
	"strings"
	"testing"

	. "github.com/daos-stack/daos/src/control/go-spdk/spdk"
	. "github.com/daos-stack/daos/src/control/mgmt"
	"github.com/daos-stack/daos/src/control/utils/log"
	. "github.com/daos-stack/daos/src/control/utils/test"
)

// MockStorage struct implements Storage interface
type mockStorage struct {
	fwRevBefore string
	fwRevAfter  string
}

func (mock *mockStorage) Init() error { return nil }
func (mock *mockStorage) Discover() interface{} {
	c := mockController(mock.fwRevBefore)
	return NVMeReturn{[]Controller{c}, []Namespace{mockNamespace(&c)}, nil}
}
func (mock *mockStorage) Update(interface{}) interface{} {
	c := mockController(mock.fwRevAfter)
	return NVMeReturn{[]Controller{c}, []Namespace{mockNamespace(&c)}, nil}
}
func (mock *mockStorage) BurnIn(interface{}) (
	string, []string, string, error) {
	return "", nil, "", nil
}
func (mock *mockStorage) Teardown() error { return nil }

// FakeParams represents an unexpected type that should trigger an error
type FakeParams struct {
	PciAddr    string
	NsID       int32
	ConfigPath string
}

func TestBurnInNVMe(t *testing.T) {
	c := mockControllerPB("1.0.0")
	configPath := "/foo/bar/conf.fio"
	nsID := 1
	goodParams := BurnInParams{
		PciAddr: c.Pciaddr, NsID: int32(nsID), ConfigPath: configPath}
	badParams := FakeParams{
		PciAddr: c.Pciaddr, NsID: int32(nsID), ConfigPath: configPath}
	expectedArgs := []string{
		fmt.Sprintf(
			"--filename=\"trtype=PCIe traddr=%s ns=%d\"",
			strings.Replace(c.Pciaddr, ":", ".", -1), nsID),
		"--ioengine=spdk",
		"--eta=always",
		"--eta-newline=10",
		configPath,
	}

	tests := map[string]struct {
		params     interface{}
		shouldFail bool
	}{
		"successful": {goodParams, false},
		"bad params": {badParams, true},
	}

	for _, test := range tests {
		sn := &NvmeStorage{}
		sn.Logger = log.NewLogger()

		cmdName, args, env, err := sn.BurnIn(test.params)
		if test.shouldFail {
			ExpectError(t, err, "unexpected params type")
			continue
		}
		if err != nil {
			t.Fatal(err.Error())
		}

		AssertTrue(t, strings.HasSuffix(cmdName, "bin/fio"), "unexpected fio executable path")
		AssertEqual(t, args, expectedArgs, "unexpected list of command arguments")
		AssertTrue(t, strings.HasPrefix(env, "LD_PRELOAD="), "unexpected LD_PRELOAD fio_plugin executable path")
		AssertTrue(t, strings.HasSuffix(env, "spdk/fio_plugin/fio_plugin"), "unexpected LD_PRELOAD fio_plugin executable path")
	}
}
