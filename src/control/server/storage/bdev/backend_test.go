//
// (C) Copyright 2018-2020 Intel Corporation.
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
package bdev

import (
	"encoding/json"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/spdk"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// defCmpOpts returns a default set of cmp option suitable for this package
func defCmpOpts() []cmp.Option {
	return []cmp.Option{
		// ignore these fields on most tests, as they are intentionally not stable
		cmpopts.IgnoreFields(storage.NvmeController{}, "HealthStats", "Serial"),
	}
}

func convertTypes(in interface{}, out interface{}) error {
	data, err := json.Marshal(in)
	if err != nil {
		return err
	}

	return json.Unmarshal(data, out)
}

func mockSpdkController(varIdx ...int32) spdk.Controller {
	native := storage.MockNvmeController(varIdx...)

	s := new(spdk.Controller)
	if err := convertTypes(native, s); err != nil {
		panic(err)
	}

	return *s
}

func mockSpdkNamespace(varIdx ...int32) spdk.Namespace {
	native := storage.MockNvmeNamespace(varIdx...)

	s := new(spdk.Namespace)
	if err := convertTypes(native, s); err != nil {
		panic(err)
	}

	return *s
}

func mockSpdkDeviceHealth(varIdx ...int32) spdk.DeviceHealth {
	native := storage.MockNvmeDeviceHealth(varIdx...)

	s := new(spdk.DeviceHealth)
	if err := convertTypes(native, s); err != nil {
		panic(err)
	}

	return *s
}

func TestBdevBackendGetController(t *testing.T) {
	type input struct {
		pciAddr         string
		spdkControllers []spdk.Controller
	}

	spdkNormalNoHealth := mockSpdkController()
	spdkNormalNoHealth.HealthStats = nil
	scNormalNoHealth := storage.MockNvmeController()
	scNormalNoHealth.HealthStats = nil

	for name, tc := range map[string]struct {
		input  input
		expSc  *storage.NvmeController
		expErr error
	}{
		"empty input": {
			input:  input{},
			expErr: errors.New("unable to resolve"),
		},
		"wrong pciAddr": {
			input: input{
				pciAddr:         "abc123",
				spdkControllers: []spdk.Controller{mockSpdkController()},
			},
			expErr: errors.New("unable to resolve"),
		},
		"correct pciAddr": {
			input: input{
				pciAddr:         mockSpdkController().PCIAddr,
				spdkControllers: []spdk.Controller{mockSpdkController()},
			},
			expSc: storage.MockNvmeController(),
		},
		"missing health stats": {
			input: input{
				pciAddr:         spdkNormalNoHealth.PCIAddr,
				spdkControllers: []spdk.Controller{spdkNormalNoHealth},
			},
			expSc: scNormalNoHealth,
		},
		"two controllers select first": {
			input: input{
				pciAddr:         mockSpdkController(1).PCIAddr,
				spdkControllers: []spdk.Controller{mockSpdkController(1), mockSpdkController(2)},
			},
			expSc: storage.MockNvmeController(1),
		},
		"two controllers select second": {
			input: input{
				pciAddr:         mockSpdkController(2).PCIAddr,
				spdkControllers: []spdk.Controller{mockSpdkController(1), mockSpdkController(2)},
			},
			expSc: storage.MockNvmeController(2),
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotSc, gotErr := getController(tc.input.pciAddr, tc.input.spdkControllers)

			common.CmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expSc, gotSc, defCmpOpts()...); diff != "" {
				t.Fatalf("\nunexpected output (-want, +got):\n%s\n", diff)
			}
		})
	}
}
