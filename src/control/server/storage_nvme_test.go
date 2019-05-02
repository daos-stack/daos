//
// (C) Copyright 2018-2019 Intel Corporation.
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
	"strings"
	"testing"

	. "github.com/daos-stack/daos/src/control/common"
	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	. "github.com/daos-stack/go-spdk/spdk"
)

var nvmeFormatCalls []string

// MockController is a mock NVMe SSD controller of type exported from go-spdk.
func MockController(fwrev string) Controller {
	c := MockControllerPB(fwrev)
	return Controller{
		Model:   c.Model,
		Serial:  c.Serial,
		PCIAddr: c.Pciaddr,
		FWRev:   fwrev,
	}
}

// MockNamespace is a mock NVMe SSD namespace of type exported from go-spdk.
func MockNamespace(ctrlr *Controller) Namespace {
	n := MockNamespacePB()
	return Namespace{
		ID:           n.Id,
		Size:         n.Capacity,
		CtrlrPciAddr: ctrlr.PCIAddr,
	}
}

// mock external interface implementations for go-spdk/spdk package
type mockSpdkEnv struct{}

func (m *mockSpdkEnv) InitSPDKEnv(int) error { return nil }

type mockSpdkNvme struct {
	fwRevBefore string
	fwRevAfter  string
	initCtrlrs  []Controller
	initNss     []Namespace
}

func (m *mockSpdkNvme) Discover() ([]Controller, []Namespace, error) {
	return m.initCtrlrs, m.initNss, nil
}
func (m *mockSpdkNvme) Format(pciAddr string) ([]Controller, []Namespace, error) {
	nvmeFormatCalls = append(nvmeFormatCalls, pciAddr)
	return m.initCtrlrs, m.initNss, nil
}
func (m *mockSpdkNvme) Update(pciAddr string, path string, slot int32) (
	[]Controller, []Namespace, error) {
	c := MockController(m.fwRevAfter)
	return []Controller{c}, []Namespace{MockNamespace(&c)}, nil
}
func (m *mockSpdkNvme) Cleanup() { return }

func newMockSpdkNvme(
	fwBefore string, fwAfter string, ctrlrs []Controller, nss []Namespace) NVME {
	return &mockSpdkNvme{fwBefore, fwAfter, ctrlrs, nss}
}

func defaultMockSpdkNvme() NVME {
	c := MockController("1.0.0")
	return newMockSpdkNvme(
		"1.0.0",
		"1.0.1",
		[]Controller{c},
		[]Namespace{MockNamespace(&c)})
}

// mock external interface implementations for spdk setup script
type mockSpdkSetup struct{}

func (m *mockSpdkSetup) prep(int, string, string) error { return nil }
func (m *mockSpdkSetup) reset() error           { return nil }

// mockNvmeStorage factory
func newMockNvmeStorage(
	spdkNvme NVME, inited bool, config *configuration) *nvmeStorage {

	return &nvmeStorage{
		env:         &mockSpdkEnv{},
		nvme:        spdkNvme,
		spdk:        &mockSpdkSetup{},
		config:      config,
		initialized: inited,
	}
}

// defaultMockNvmeStorage factory
func defaultMockNvmeStorage(config *configuration) *nvmeStorage {
	return newMockNvmeStorage(
		defaultMockSpdkNvme(),
		false, // Discover will not fetch when initialised is true
		config)
}

func TestDiscoveryNvmeSingle(t *testing.T) {
	tests := []struct {
		inited bool
	}{
		{
			true,
		},
		{
			false,
		},
	}

	c := MockControllerPB("1.0.0")

	for _, tt := range tests {
		config := defaultMockConfig(t)
		sn := newMockNvmeStorage(defaultMockSpdkNvme(), tt.inited, &config)

		if err := sn.Discover(); err != nil {
			t.Fatal(err)
		}

		if tt.inited {
			AssertEqual(
				t, sn.controllers, []*pb.NvmeController(nil),
				"unexpected list of protobuf format controllers")
			continue
		}

		AssertEqual(
			t, sn.controllers, []*pb.NvmeController{c},
			"unexpected list of protobuf format controllers")
	}
}

// Verify correct mapping of namespaces to multiple controllers
func TestDiscoveryNvmeMulti(t *testing.T) {
	tests := []struct {
		ctrlrs []Controller
		nss    []Namespace
	}{
		{
			[]Controller{
				{"", "", "1.2.3.4.5", "1.0.0"},
				{"", "", "1.2.3.4.6", "1.0.0"},
			},
			[]Namespace{
				{0, 100, "1.2.3.4.5"},
				{1, 200, "1.2.3.4.6"},
			},
		},
		{
			[]Controller{
				{"", "", "1.2.3.4.5", "1.0.0"},
				{"", "", "1.2.3.4.6", "1.0.0"},
			},
			[]Namespace{},
		},
		{
			[]Controller{
				{"", "", "1.2.3.4.5", "1.0.0"},
				{"", "", "1.2.3.4.6", "1.0.0"},
			},
			[]Namespace{
				{0, 100, "1.2.3.4.5"},
				{1, 100, "1.2.3.4.5"},
				{2, 100, "1.2.3.4.5"},
				{0, 200, "1.2.3.4.6"},
				{1, 200, "1.2.3.4.6"},
				{2, 200, "1.2.3.4.6"},
			},
		},
	}

	for _, tt := range tests {
		config := defaultMockConfig(t)
		sn := newMockNvmeStorage(
			newMockSpdkNvme("1.0.0", "1.0.1", tt.ctrlrs, tt.nss),
			false,
			&config)

		if err := sn.Discover(); err != nil {
			t.Fatal(err)
		}

		if len(tt.ctrlrs) != len(sn.controllers) {
			t.Fatalf(
				"unexpected number of controllers found, wanted %d, found %d",
				len(tt.ctrlrs), len(sn.controllers))
		}
		// verify we have the expected number of namespaces reported
		discovered := 0
		for _, pbC := range sn.controllers {
			discovered += len(pbC.Namespace)
		}
		if len(tt.nss) != discovered {
			t.Fatalf(
				"unexpected number of namespaces found, wanted %d, found %d",
				len(tt.nss), discovered)
		}

		// verify protobuf Controller has ns for each one expected
		for _, n := range tt.nss {
			foundNs := false // find namespace
			for i, pbC := range sn.controllers {
				if n.CtrlrPciAddr == pbC.Pciaddr {
					for _, pbNs := range sn.controllers[i].Namespace {
						if pbNs.Capacity == n.Size && pbNs.Id == n.ID {
							foundNs = true
						}
					}
				}
			}
			if !foundNs {
				t.Fatalf("namespace not found: %v", n)
			}
		}
	}
}

func TestFormatNvme(t *testing.T) {
	tests := []struct {
		inited    bool
		formatted bool
		pciAddrs  []string
		errMsg    string
	}{
		{
			true,
			false,
			[]string{},
			"",
		},
		{
			false,
			true,
			[]string{},
			"nvme storage not initialized",
		},
		{
			true,
			true,
			[]string{},
			"nvme storage has already been formatted and reformat " +
				"not implemented",
		},
		{
			true,
			false,
			[]string{"0000:81:00.0", "0000:83:00.0"},
			"",
		},
	}

	c := MockControllerPB("1.0.0")
	srvIdx := 0 // assume just a single io_server (index 0)

	for _, tt := range tests {
		nvmeFormatCalls = []string{}

		config := defaultMockConfig(t)
		config.Servers[srvIdx].BdevList = tt.pciAddrs
		sn := defaultMockNvmeStorage(&config)

		if tt.inited {
			if err := sn.Discover(); err != nil {
				t.Fatal(err)
			}
		}

		if err := sn.Format(srvIdx); err != nil {
			if tt.errMsg != "" {
				ExpectError(t, err, tt.errMsg, "")
				continue
			}
			t.Fatal(err)
		}

		AssertEqual(
			t, nvmeFormatCalls, tt.pciAddrs,
			"unexpected list of pci addresses in format calls")
		AssertEqual(t, sn.formatted, true, "expect formatted state")
		AssertEqual(
			t, sn.controllers, []*pb.NvmeController{c},
			"unexpected list of protobuf format controllers")
	}
}

func TestUpdateNvme(t *testing.T) {
	tests := []struct {
		inited bool
		errMsg string
	}{
		{
			true,
			"",
		},
		{
			false,
			"nvme storage not initialized",
		},
	}

	// expected Controller protobuf representation should have updated
	// firmware revision
	c := MockControllerPB("1.0.1")

	for _, tt := range tests {
		config := defaultMockConfig(t)
		sn := defaultMockNvmeStorage(&config)

		if tt.inited {
			if err := sn.Discover(); err != nil {
				t.Fatal(err)
			}
		}

		if err := sn.Update(c.Pciaddr, "", 0); err != nil {
			if tt.errMsg != "" {
				ExpectError(t, err, tt.errMsg, "")
				continue
			}
			t.Fatal(err)
		}

		AssertEqual(
			t, sn.controllers, []*pb.NvmeController{c},
			"unexpected list of protobuf format controllers")
	}
}

// TestBurnInNvme verifies a corner case because BurnIn does not call out
// to SPDK via bindings.
// In this case the real NvmeStorage is used as opposed to a mockNvmeStorage.
func TestBurnInNvme(t *testing.T) {
	tests := []struct {
		inited bool
		errMsg string
	}{
		{
			true,
			"",
		},
		{
			false,
			"nvme storage not initialized",
		},
	}

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

	for _, tt := range tests {
		config := defaultMockConfig(t)
		sn := defaultMockNvmeStorage(&config)

		if tt.inited {
			if err := sn.Discover(); err != nil {
				t.Fatal(err)
			}
		}

		cmdName, args, env, err := sn.BurnIn(c.Pciaddr, int32(nsID), configPath)
		if err != nil {
			if tt.errMsg != "" {
				ExpectError(t, err, tt.errMsg, "")
				continue
			}
			t.Fatal(err)
		}

		AssertTrue(t, strings.HasSuffix(cmdName, "bin/fio"), "unexpected fio executable path")
		AssertEqual(t, args, expectedArgs, "unexpected list of command arguments")
		AssertTrue(t, strings.HasPrefix(env, "LD_PRELOAD="), "unexpected LD_PRELOAD fio_plugin executable path")
		AssertTrue(t, strings.HasSuffix(env, "spdk/fio_plugin/fio_plugin"), "unexpected LD_PRELOAD fio_plugin executable path")
	}
}
