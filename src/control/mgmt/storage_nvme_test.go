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

package mgmt

import (
	"fmt"
	"strings"
	"testing"

	pb "github.com/daos-stack/daos/src/control/mgmt/proto"
	"github.com/daos-stack/daos/src/control/utils/log"
	. "github.com/daos-stack/daos/src/control/utils/test"
	. "github.com/daos-stack/go-spdk/spdk"
)

// mock external interface implementations for go-spdk/spdk package
type mockSpdkEnv struct{}

func (m *mockSpdkEnv) InitSPDKEnv(int) error { return nil }

type mockSpdkNvme struct {
	fwRevBefore string
	fwRevAfter  string
}

func (m *mockSpdkNvme) Discover() ([]Controller, []Namespace, error) {
	c := MockController(m.fwRevBefore)
	return []Controller{c}, []Namespace{MockNamespace(&c)}, nil
}
func (m *mockSpdkNvme) Update(ctrlrID int32, path string, slot int32) (
	[]Controller, []Namespace, error) {
	c := MockController(m.fwRevAfter)
	return []Controller{c}, []Namespace{MockNamespace(&c)}, nil
}
func (m *mockSpdkNvme) Cleanup() { return }

type mockSpdkSetup struct{}

func (m *mockSpdkSetup) start() error { return nil }
func (m *mockSpdkSetup) reset() error { return nil }

// mockNvmeStorage factory
func newMockNvmeStorage(fwRevBefore string, fwRevAfter string) *nvmeStorage {
	return &nvmeStorage{
		logger: log.NewLogger(),
		env:    &mockSpdkEnv{},
		nvme:   &mockSpdkNvme{fwRevBefore, fwRevAfter},
		setup:  &mockSpdkSetup{},
	}
}

func TestDiscoveryNvme(t *testing.T) {
	sn := newMockNvmeStorage("", "")
	c := MockControllerPB("")

	if err := sn.Discover(); err != nil {
		t.Fatal(err.Error())
	}

	AssertTrue(t, sn.initialised, "expected NvmeStorage to have been initialised")
	AssertEqual(
		t, sn.Controllers, []*pb.NvmeController{c},
		"unexpected list of protobuf format controllers")
}

func TestUpdateNvme(t *testing.T) {
	sn := newMockNvmeStorage("1.0.0", "1.0.1")
	c := MockControllerPB("1.0.1")

	if err := sn.Update(0, "", 0); err != nil {
		t.Fatal(err.Error())
	}

	AssertTrue(t, sn.initialised, "expected NvmeStorage to have been initialised")
	AssertEqual(
		t, sn.Controllers, []*pb.NvmeController{c},
		"unexpected list of protobuf format controllers")
}

// TestBurnInNvme verifies a corner case because BurnIn does not call out
// to SPDK via bindings.
// In this case the real NvmeStorage is used as opposed to a mockNvmeStorage.
func TestBurnInNvme(t *testing.T) {
	sn := newMockNvmeStorage("", "")
	c := MockControllerPB("1.0.0")

	configPath := "/foo/bar/conf.fio"
	nsID := 1
	expectedArgs := []string{
		fmt.Sprintf(
			"--filename=\"trtype=PCIe traddr=%s ns=%d\"",
			strings.Replace(c.Pciaddr, ":", ".", -1), nsID),
		"--ioengine=spdk",
		"--eta=always",
		"--eta-newline=10",
		configPath,
	}

	cmdName, args, env, err := sn.BurnIn(c.Pciaddr, int32(nsID), configPath)
	if err != nil {
		t.Fatal(err.Error())
	}

	AssertTrue(t, strings.HasSuffix(cmdName, "bin/fio"), "unexpected fio executable path")
	AssertEqual(t, args, expectedArgs, "unexpected list of command arguments")
	AssertTrue(t, strings.HasPrefix(env, "LD_PRELOAD="), "unexpected LD_PRELOAD fio_plugin executable path")
	AssertTrue(t, strings.HasSuffix(env, "spdk/fio_plugin/fio_plugin"), "unexpected LD_PRELOAD fio_plugin executable path")
}
