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
	"os"
	"syscall"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/spdk"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

type (
	spdkWrapper struct {
		spdk.Env
		spdk.Nvme
		controllers []spdk.Controller

		initialized bool
	}

	spdkBackend struct {
		log     logging.Logger
		binding *spdkWrapper
		script  *spdkSetupScript
	}
)

// suppressOutput is a horrible, horrible hack necessitated by the fact that
// SPDK blathers to stdout, causing console spam and messing with our secure
// communications channel between the server and privileged helper.
func (w *spdkWrapper) suppressOutput() (restore func(), err error) {
	realStdout, dErr := syscall.Dup(syscall.Stdout)
	if dErr != nil {
		err = dErr
		return
	}

	devNull, oErr := os.OpenFile(os.DevNull, os.O_WRONLY, 0)
	if oErr != nil {
		err = oErr
		return
	}

	if err = syscall.Dup2(int(devNull.Fd()), syscall.Stdout); err != nil {
		return
	}

	restore = func() {
		// NB: Normally panic() in production code is frowned upon, but in this
		// case if we get errors there really isn't any handling to be done
		// because things have gone completely sideways.
		if err := devNull.Close(); err != nil {
			panic(err)
		}
		if err := syscall.Dup2(realStdout, syscall.Stdout); err != nil {
			panic(err)
		}
	}

	return
}

func (w *spdkWrapper) init(initShmID ...int) (err error) {
	if w.initialized {
		return nil
	}

	restore, err := w.suppressOutput()
	if err != nil {
		return errors.Wrap(err, "failed to suppress SPDK output")
	}
	defer restore()

	shmID := 0
	if len(initShmID) > 0 {
		shmID = initShmID[0]
	}

	if err := w.InitSPDKEnv(shmID); err != nil {
		return errors.Wrap(err, "failed to initialize SPDK")
	}

	cs, err := w.Discover()
	if err != nil {
		return errors.Wrap(err, "failed to discover NVMe")
	}
	w.controllers = cs

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

func convertControllers(bcs []spdk.Controller) ([]*storage.NvmeController, error) {
	scs := make([]*storage.NvmeController, 0, len(bcs))

	for _, bc := range bcs {
		sc := &storage.NvmeController{}
		if err := convertController(bc, sc); err != nil {
			return nil, errors.Wrapf(err, "failed to convert spdk Controller %+v", bc)
		}

		scs = append(scs, sc)
	}

	return scs, nil
}

func (b *spdkBackend) Scan() (storage.NvmeControllers, error) {
	if err := b.Init(); err != nil {
		return nil, err
	}

	return convertControllers(b.binding.controllers)
}

func getController(pciAddr string, bcs []spdk.Controller) (*storage.NvmeController, error) {
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

	scs, err := convertControllers([]spdk.Controller{*spdkController})
	if err != nil {
		return nil, err
	}
	if len(scs) != 1 {
		return nil, errors.Errorf("unable to resolve %s in convertControllers", pciAddr)
	}

	return scs[0], nil
}

func (b *spdkBackend) Format(pciAddr string) (*storage.NvmeController, error) {
	if pciAddr == "" {
		return nil, FaultFormatBadPciAddr("")
	}

	controllers, err := b.Scan()
	if err != nil {
		return nil, err
	}

	foundAddr := false
	for _, c := range controllers {
		if c.PciAddr == pciAddr {
			foundAddr = true
			break
		}
	}

	if !foundAddr {
		return nil, FaultFormatBadPciAddr(pciAddr)
	}

	if err := b.binding.Format(pciAddr); err != nil {
		return nil, err
	}

	return getController(pciAddr, b.binding.controllers)
}

func (b *spdkBackend) Prepare(nrHugePages int, targetUser, pciWhiteList string) error {
	return b.script.Prepare(nrHugePages, targetUser, pciWhiteList)
}

func (b *spdkBackend) Reset() error {
	return b.script.Reset()
}
