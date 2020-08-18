//
// (C) Copyright 2019-2020 Intel Corporation.
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
	"bufio"
	"bytes"
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"strings"
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
		vmdDisabled bool
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

func (w *spdkWrapper) init(log logging.Logger, spdkOpts spdk.EnvOptions) (err error) {
	if w.initialized {
		return nil
	}

	restore, err := w.suppressOutput()
	if err != nil {
		return errors.Wrap(err, "failed to suppress spdk output")
	}
	defer restore()

	// provide empty whitelist on init so all devices are discovered
	if err := w.InitSPDKEnv(log, spdkOpts); err != nil {
		return errors.Wrap(err, "failed to init spdk env")
	}

	cs, err := w.Discover(log)
	if err != nil {
		return errors.Wrap(err, "failed to discover nvme")
	}
	w.controllers = cs
	w.initialized = true
	// TODO: w.FiniSPDKEnv(log, spdkOpts)

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
		binding: &spdkWrapper{Env: &spdk.EnvImpl{}, Nvme: &spdk.NvmeImpl{}},
		script:  sr,
	}
}

func defaultBackend(log logging.Logger) *spdkBackend {
	return newBackend(log, defaultScriptRunner(log))
}

// DisableVMD turns off VMD device awareness.
func (b *spdkBackend) DisableVMD() {
	b.binding.vmdDisabled = true
}

// IsVMDDisabled checks for VMD device awareness.
func (b *spdkBackend) IsVMDDisabled() bool {
	return b.binding.vmdDisabled
}

func convertControllers(bcs []spdk.Controller) ([]*storage.NvmeController, error) {
	scs := make([]*storage.NvmeController, 0, len(bcs))

	for _, bc := range bcs {
		sc := &storage.NvmeController{}
		if err := convertController(bc, sc); err != nil {
			return nil, errors.Wrapf(err,
				"failed to convert spdk controller %+v", bc)
		}

		scs = append(scs, sc)
	}

	return scs, nil
}

// Scan discovers NVMe controllers accessible by SPDK.
func (b *spdkBackend) Scan(_ ScanRequest) (*ScanResponse, error) {
	spdkOpts := spdk.EnvOptions{
		DisableVMD: b.IsVMDDisabled(),
	}

	if err := b.binding.init(b.log, spdkOpts); err != nil {
		return nil, err
	}

	cs, err := convertControllers(b.binding.controllers)
	if err != nil {
		return nil, err
	}

	return &ScanResponse{
		Controllers: cs,
	}, nil
}

func getController(pciAddr string, bcs []spdk.Controller) (*storage.NvmeController, error) {
	if pciAddr == "" {
		return nil, FaultBadPCIAddr("")
	}

	var spdkController *spdk.Controller
	for _, bc := range bcs {
		if bc.PCIAddr == pciAddr {
			spdkController = &bc
			break
		}
	}

	if spdkController == nil {
		return nil, FaultPCIAddrNotFound(pciAddr)
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

func (b *spdkBackend) deviceFormat(class storage.BdevClass, device string) (*DeviceFormatResponse, error) {
	if device == "" {
		return nil, errors.New("empty pci address in device list")
	}

	resp := new(DeviceFormatResponse)

	switch class {
	case storage.BdevClassKdev, storage.BdevClassFile, storage.BdevClassMalloc:
		resp.Formatted = true
		b.log.Debugf("%s format for non-NVMe bdev skipped on %s", class, device)
	case storage.BdevClassNvme:
		msg := fmt.Sprintf("%s device format on %q", class, device)

		if err := b.binding.Format(b.log, device); err != nil {
			resp.Error = FaultFormatError(device, err)
			b.log.Debugf("%s failed Format() (%s)", msg, err)
			break
		}

		resp.Formatted = true
		b.log.Debugf("%s successful", msg)
	default:
		return nil, FaultFormatUnknownClass(class.String())
	}

	return resp, nil
}

func (b *spdkBackend) Format(req FormatRequest) (*FormatResponse, error) {
	spdkOpts := spdk.EnvOptions{
		ShmID:        req.ShmID,
		MemSize:      req.MemSize,
		PciWhiteList: req.DeviceList,
		DisableVMD:   b.IsVMDDisabled(),
	}

	// provide bdev as whitelist so only formatting device is bound
	if err := b.binding.InitSPDKEnv(b.log, spdkOpts); err != nil {
		return nil, errors.Wrap(err, "failed to init spdk env")
	}

	// TODO (DAOS-3844): Kick off device formats parallel?
	resp := &FormatResponse{
		DeviceResponses: make(DeviceFormatResponses),
	}

	for _, dev := range req.DeviceList {
		devResp, err := b.deviceFormat(req.Class, dev)
		if err != nil {
			return nil, err
		}
		resp.DeviceResponses[dev] = devResp
	}

	b.binding.FiniSPDKEnv(b.log, spdkOpts)

	return resp, nil
}

// detectVMD returns whether VMD devices have been found and a slice of VMD
// PCI addresses if found.
func detectVMD() ([]string, error) {
	// Check available VMD devices with command:
	// "$lspci | grep  -i -E "201d | Volume Management Device"
	lspciCmd := exec.Command("lspci")
	vmdCmd := exec.Command("grep", "-i", "-E", "201d|Volume Management Device")
	var cmdOut bytes.Buffer

	vmdCmd.Stdin, _ = lspciCmd.StdoutPipe()
	vmdCmd.Stdout = &cmdOut
	_ = lspciCmd.Start()
	_ = vmdCmd.Run()
	_ = lspciCmd.Wait()

	if cmdOut.Len() == 0 {
		return []string{}, nil
	}

	vmdCount := bytes.Count(cmdOut.Bytes(), []byte("0000:"))
	vmdAddrs := make([]string, 0, vmdCount)

	i := 0
	scanner := bufio.NewScanner(&cmdOut)
	for scanner.Scan() {
		if i == vmdCount {
			break
		}
		s := strings.Split(scanner.Text(), " ")
		vmdAddrs = append(vmdAddrs, strings.TrimSpace(s[0]))
		i++
	}

	if len(vmdAddrs) == 0 {
		return nil, errors.New("error parsing cmd output")
	}

	return vmdAddrs, nil
}

func (b *spdkBackend) Prepare(req PrepareRequest) (*PrepareResponse, error) {
	resp := &PrepareResponse{}

	if err := b.script.Prepare(req); err != nil {
		return nil, errors.Wrap(err, "re-binding ssds to attach with spdk")
	}

	if req.DisableVMD {
		return resp, nil
	}

	vmdDevs, err := detectVMD()
	if err != nil {
		return nil, errors.Wrap(err, "VMD could not be enabled")
	}

	if len(vmdDevs) == 0 {
		return resp, nil
	}

	vmdReq := req
	// If VMD devices are going to be used, then need to run a separate
	// bdev prepare (SPDK setup) with the VMD address as the PCI_WHITELIST
	//
	// TODO: ignore devices not in include list
	vmdReq.PCIWhitelist = strings.Join(vmdDevs, " ")

	if err := b.script.Prepare(vmdReq); err != nil {
		return nil, errors.Wrap(err, "re-binding vmd ssds to attach with spdk")
	}

	resp.VmdDetected = true
	b.log.Debugf("volume management devices detected: %v", vmdDevs)

	return resp, nil
}

func (b *spdkBackend) PrepareReset() error {
	return b.script.Reset()
}
