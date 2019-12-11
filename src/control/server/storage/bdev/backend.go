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
package bdev

import (
	"encoding/json"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/spdk"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

type (
	spdkWrapper struct {
		spdk.Env
		spdk.Nvme

		initialized bool
	}

	spdkBackend struct {
		log     logging.Logger
		binding *spdkWrapper
		script  *spdkSetupScript
	}
)

func (w *spdkWrapper) init(initShmID ...int) error {
	if w.initialized {
		return nil
	}

	shmID := 0
	if len(initShmID) > 0 {
		shmID = initShmID[0]
	}

	if err := w.InitSPDKEnv(shmID); err != nil {
		return errors.Wrap(err, "failed to initialize SPDK")
	}

	w.initialized = true
	return nil
}

func convert(in interface{}, out interface{}) error {
	data, err := json.Marshal(in)
	if err != nil {
		return err
	}

	return json.Unmarshal(data, out)
}

func convertController(in spdk.Controller, out *storage.NvmeController) error {
	return convert(in, out)
}

func convertNamespace(in spdk.Namespace, out *storage.NvmeNamespace) error {
	return convert(in, out)
}

func convertDeviceHealth(in spdk.DeviceHealth, out *storage.NvmeDeviceHealth) error {
	return convert(in, out)
}

func newBackend(log logging.Logger, sr *spdkSetupScript) *spdkBackend {
	return &spdkBackend{
		log:     log,
		binding: &spdkWrapper{},
		script:  sr,
	}
}

func defaultBackend(log logging.Logger) *spdkBackend {
	return newBackend(log, defaultScriptRunner(log))
}

func (b *spdkBackend) Init(shmID ...int) error {
	if err := b.binding.init(shmID...); err != nil {
		return err
	}

	return nil
}

func coalesceControllers(bcs []spdk.Controller, bns []spdk.Namespace, bhs []spdk.DeviceHealth) ([]*storage.NvmeController, error) {
	scs := make([]*storage.NvmeController, 0, len(bcs))

	for _, bc := range bcs {
		sc := &storage.NvmeController{}
		if err := convertController(bc, sc); err != nil {
			return nil, errors.Wrapf(err, "failed to convert spdk Controller %+v", bc)
		}

		for _, bn := range bns {
			if bn.CtrlrPciAddr != sc.PciAddr {
				continue
			}
			sn := &storage.NvmeNamespace{}
			if err := convertNamespace(bn, sn); err != nil {
				return nil, errors.Wrapf(err, "failed to convert spdk Namespace %+v", bn)
			}
			sc.Namespaces = append(sc.Namespaces, sn)
		}

		for _, bh := range bhs {
			if bh.CtrlrPciAddr != sc.PciAddr {
				continue
			}
			if sc.HealthStats != nil {
				return nil, errors.Errorf("duplicate health entry for %s", sc.PciAddr)
			}
			sc.HealthStats = &storage.NvmeDeviceHealth{}
			if err := convertDeviceHealth(bh, sc.HealthStats); err != nil {
				return nil, errors.Wrapf(err, "failed to convert spdk DeviceHealth %+v", bh)
			}
		}

		scs = append(scs, sc)
	}

	return scs, nil
}

func (b *spdkBackend) Scan() (storage.NvmeControllers, error) {
	if err := b.Init(); err != nil {
		return nil, err
	}

	bcs, bns, bhs, err := b.binding.Discover()
	if err != nil {
		return nil, err
	}

	return coalesceControllers(bcs, bns, bhs)
}

func getFormattedController(pciAddr string, bcs []spdk.Controller, bns []spdk.Namespace) (*storage.NvmeController, error) {
	var spdkController *spdk.Controller
	for _, bc := range bcs {
		if bc.PCIAddr == pciAddr {
			spdkController = &bc
			break
		}
	}

	if spdkController == nil {
		return nil, errors.Errorf("unable to resolve %s after format", pciAddr)
	}

	scs, err := coalesceControllers([]spdk.Controller{*spdkController}, bns, nil)
	if err != nil {
		return nil, err
	}
	if len(scs) != 1 {
		return nil, errors.Errorf("unable to resolve %s in coalesceControllers", pciAddr)
	}

	return scs[0], nil
}

func (b *spdkBackend) Format(pciAddr string) (*storage.NvmeController, error) {
	if err := b.Init(); err != nil {
		return nil, err
	}

	if pciAddr == "" {
		return nil, errors.New("empty pciAddr string")
	}

	bcs, bns, err := b.binding.Format(pciAddr)
	if err != nil {
		return nil, err
	}

	return getFormattedController(pciAddr, bcs, bns)
}

func (b *spdkBackend) Prepare(nrHugePages int, targetUser, pciWhiteList string) error {
	return b.script.Prepare(nrHugePages, targetUser, pciWhiteList)
}

func (b *spdkBackend) Reset() error {
	return b.script.Reset()
}
