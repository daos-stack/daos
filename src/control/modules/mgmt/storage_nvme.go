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
	"os/exec"
	"strings"

	"common/log"

	"go-spdk/spdk"

	pb "modules/mgmt/proto"
)

var (
	spdkSetupPath    = "share/spdk/scripts/setup.sh"
	spdkFioPluginDir = "share/spdk/fio_plugin"
	fioExecPath      = "bin/fio"
)

// Init method implementation for NvmeStorage.
//
// Setup available NVMe devices to be used by SPDK
// and initialise SPDK environment before probing controllers.
//
// 	Note: shm_id currently hardcoded to 1 to enable multiprocess
//        mode between Discover and Burn-in.
func (sn *NvmeStorage) Init() (err error) {
	absSetupPath, err := getAbsInstallPath(spdkSetupPath)
	if err != nil {
		return
	}
	// run setup to allocate hugepages and bind PCI devices
	// (that don't have active mountpoints) to generic kernel driver
	//
	// NOTE: will make the controller disappear from /dev until
	//       Teardown() is called.
	if err = exec.Command(absSetupPath).Run(); err != nil {
		return
	}
	sn.Env.ShmID = 1	// shm_id passed as opt in SPDK env init
	if err = sn.Env.InitSPDKEnv(); err != nil {
		return
	}
	return
}

// Discover method implementation for NvmeStorage
func (sn *NvmeStorage) Discover() interface{} {
	cs, ns, err := sn.Nvme.Discover()
	return NVMeReturn{cs, ns, err}
}

// Update method implementation for NvmeStorage
func (sn *NvmeStorage) Update(params interface{}) interface{} {
	switch t := params.(type) {
	case UpdateParams:
		cs, ns, err := sn.Nvme.Update(t.CtrlrID, t.Path, t.Slot)
		return NVMeReturn{cs, ns, err}
	default:
		return fmt.Errorf("unexpected return type")
	}
}

// BurnIn method implementation for NvmeStorage
// Doesn't call through go-spdk, returns cmds to be issued over shell
func (sn *NvmeStorage) BurnIn(params interface{}) (
	fioPath string, cmds []string, env string, err error) {
	switch t := params.(type) {
	case BurnInParams:
		pluginDir := ""
		pluginDir, err = getAbsInstallPath(spdkFioPluginDir)
		if err != nil {
			return
		}
		fioPath, err = getAbsInstallPath(fioExecPath)
		if err != nil {
			return
		}
		// run fio with spdk plugin specified in LD_PRELOAD env
		env = fmt.Sprintf("LD_PRELOAD=%s/fio_plugin", pluginDir)
		// limitation of fio_plugin for spdk is that traddr needs
		// to not contain colon chars, convert to full-stops
		// https://github.com/spdk/spdk/tree/master/examples/nvme/fio_plugin .
		// shm_id specified within fio configs to enable spdk multiprocess
		// mode required to perform burn-in from Go process.
		// eta options provided to trigger periodic client responses.
		cmds = []string{
			fmt.Sprintf(
				"--filename=\"trtype=PCIe traddr=%s ns=%d\"",
				strings.Replace(t.PciAddr, ":", ".", -1), t.NsID),
			"--ioengine=spdk",
			"--eta=always",
			"--eta-newline=10",
			t.ConfigPath,
		}
		sn.Logger.Debugf(
			"BurnIn command string: %s %s %v", env, fioPath, cmds)
		return
	default:
		err = fmt.Errorf("unexpected params type")
		return
	}
}

// Teardown method implementation for NvmeStorage.
//
// Cleanup references to NVMe devices held by go-spdk
// bindings, rebind PCI devices back to their original drivers
// and cleanup any leftover spdk files/resources.
func (sn *NvmeStorage) Teardown() error {
	sn.Nvme.Cleanup()

	absSetupPath, err := getAbsInstallPath(spdkSetupPath)
	if err != nil {
		return err
	}
	if err := exec.Command(absSetupPath, "reset").Run(); err != nil {
		return err
	}
	return nil
}

// loadControllers converts slice of Controller into protobuf equivalent.
// Implemented as a pure function.
func loadControllers(ctrlrs []spdk.Controller) (CtrlrMap, error) {
	pbCtrlrs := make(CtrlrMap)
	for _, c := range ctrlrs {
		pbCtrlrs[c.ID] = &pb.NVMeController{
			Id:      c.ID,
			Model:   c.Model,
			Serial:  c.Serial,
			Pciaddr: c.PCIAddr,
			Fwrev:   c.FWRev,
		}
	}
	if len(pbCtrlrs) != len(ctrlrs) {
		return nil, fmt.Errorf("loadControllers: input contained duplicate keys")
	}
	return pbCtrlrs, nil
}

// loadNamespaces converts slice of Namespace into protobuf equivalent.
// Implemented as a pure function.
func loadNamespaces(pbCtrlrs CtrlrMap, nss []spdk.Namespace) (NsMap, error) {
	pbNamespaces := make(NsMap)
	for _, ns := range nss {
		c, exists := pbCtrlrs[ns.CtrlrID]
		if !exists {
			return nil, fmt.Errorf(
				"loadNamespaces: missing controller with ID %d", ns.CtrlrID)
		}
		pbNamespaces[ns.ID] = &pb.NVMeNamespace{
			Controller: c,
			Id:         ns.ID,
			Capacity:   ns.Size,
		}
	}
	if len(pbNamespaces) != len(nss) {
		return nil, fmt.Errorf("loadNamespaces: input contained duplicate keys")
	}
	return pbNamespaces, nil
}

// populateNVMe unpacks return type and loads protobuf representations.
func (s *ControlService) populateNVMe(ret interface{}) error {
	switch ret := ret.(type) {
	case NVMeReturn:
		if ret.Err != nil {
			return ret.Err
		}

		NvmeControllers, err := loadControllers(ret.Ctrlrs)
		if err != nil {
			return err
		}
		s.NvmeControllers = NvmeControllers

		NvmeNamespaces, err := loadNamespaces(s.NvmeControllers, ret.Nss)
		if err != nil {
			return err
		}
		s.NvmeNamespaces = NvmeNamespaces

		s.storageInitialised = true
	case error:
		return ret
	default:
		return fmt.Errorf("unexpected return type")
	}

	return nil
}

// NewNvmeStorage creates a new instance of our NvmeStorage struct.
func NewNvmeStorage(logger *log.Logger) *NvmeStorage {
	return &NvmeStorage{
		Logger:	logger,
		Env: &spdk.Env{},
		Nvme: &spdk.Nvme{},
	}
}
