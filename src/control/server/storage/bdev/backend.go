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
	"os"
	"os/exec"
	"sort"
	"strings"
	"syscall"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/spdk"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

type (
	spdkWrapper struct {
		spdk.Env
		spdk.Nvme

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

func (w *spdkWrapper) init(log logging.Logger, spdkOpts spdk.EnvOptions) (func(), error) {
	restore, err := w.suppressOutput()
	if err != nil {
		return nil, errors.Wrap(err, "failed to suppress spdk output")
	}

	// provide empty whitelist on init so all devices are discovered
	if err := w.InitSPDKEnv(log, spdkOpts); err != nil {
		restore()
		return nil, errors.Wrap(err, "failed to init spdk env")
	}

	return restore, nil
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

func filterControllers(in storage.NvmeControllers, pciFilter ...string) (out storage.NvmeControllers) {
	for _, c := range in {
		if len(pciFilter) > 0 && !common.Includes(pciFilter, c.PciAddr) {
			continue
		}
		out = append(out, c)
	}

	return
}

// Scan discovers NVMe controllers accessible by SPDK.
func (b *spdkBackend) Scan(req ScanRequest) (*ScanResponse, error) {
	restoreOutput, err := b.binding.init(b.log, spdk.EnvOptions{
		DisableVMD: b.IsVMDDisabled(),
	})
	if err != nil {
		return nil, err
	}
	defer restoreOutput()

	cs, err := b.binding.Discover(b.log)
	if err != nil {
		return nil, errors.Wrap(err, "failed to discover nvme")
	}

	return &ScanResponse{
		Controllers: filterControllers(cs, req.DeviceList...),
	}, nil
}

func (b *spdkBackend) formatRespFromResults(results []*spdk.FormatResult) (*FormatResponse, error) {
	resp := &FormatResponse{
		DeviceResponses: make(DeviceFormatResponses),
	}
	resultMap := make(map[string]map[int]error)

	// build pci address to namespace errors map
	for _, result := range results {
		if _, exists := resultMap[result.CtrlrPCIAddr]; !exists {
			resultMap[result.CtrlrPCIAddr] = make(map[int]error)
		}

		if _, exists := resultMap[result.CtrlrPCIAddr][int(result.NsID)]; exists {
			return nil, errors.Errorf("duplicate error for ns %d on %s",
				result.NsID, result.CtrlrPCIAddr)
		}

		resultMap[result.CtrlrPCIAddr][int(result.NsID)] = result.Err
	}

	// populate device responses for failed/formatted namespacess
	for addr, nsErrMap := range resultMap {
		var formatted, failed, all []int
		var firstErr error

		for nsID := range nsErrMap {
			all = append(all, nsID)
		}
		sort.Ints(all)
		for _, nsID := range all {
			err := nsErrMap[nsID]
			if err != nil {
				failed = append(failed, nsID)
				if firstErr == nil {
					firstErr = errors.Wrapf(err, "namespace %d", nsID)
				}
				continue
			}
			formatted = append(formatted, nsID)
		}

		b.log.Debugf("formatted namespaces %v on nvme device at %s", formatted, addr)

		devResp := new(DeviceFormatResponse)
		if firstErr != nil {
			devResp.Error = FaultFormatError(addr, errors.Errorf(
				"failed to format namespaces %v (%s)",
				failed, firstErr))
			resp.DeviceResponses[addr] = devResp
			continue
		}

		devResp.Formatted = true
		resp.DeviceResponses[addr] = devResp
	}

	return resp, nil
}

func (b *spdkBackend) format(class storage.BdevClass, deviceList []string) (*FormatResponse, error) {
	// TODO (DAOS-3844): Kick off device formats parallel?
	switch class {
	case storage.BdevClassKdev, storage.BdevClassFile, storage.BdevClassMalloc:
		resp := &FormatResponse{
			DeviceResponses: make(DeviceFormatResponses),
		}

		for _, device := range deviceList {
			resp.DeviceResponses[device] = &DeviceFormatResponse{
				Formatted: true,
			}
			b.log.Debugf("%s format for non-NVMe bdev skipped on %s", class, device)
		}

		return resp, nil
	case storage.BdevClassNvme:
		if len(deviceList) == 0 {
			return nil, errors.New("empty pci address list in format request")
		}

		results, err := b.binding.Format(b.log)
		if err != nil {
			return nil, errors.Wrapf(err, "spdk format %v", deviceList)
		}

		if len(results) == 0 {
			return nil, errors.New("empty results from spdk binding format request")
		}

		return b.formatRespFromResults(results)
	default:
		return nil, FaultFormatUnknownClass(class.String())
	}
}

// Format initializes the SPDK environment, defers the call to finalize the same
// environment and calls private format() routine to format all devices in the
// request device list in a manner specific to the supplied bdev class.
//
// Remove any stale SPDK lockfiles after format.
func (b *spdkBackend) Format(req FormatRequest) (*FormatResponse, error) {
	spdkOpts := spdk.EnvOptions{
		MemSize:      req.MemSize,
		PciWhiteList: req.DeviceList,
		DisableVMD:   b.IsVMDDisabled(),
	}

	restoreOutput, err := b.binding.init(b.log, spdkOpts)
	if err != nil {
		return nil, err
	}
	defer restoreOutput()
	defer b.binding.FiniSPDKEnv(b.log, spdkOpts)
	defer b.binding.CleanLockfiles(b.log, req.DeviceList...)

	return b.format(req.Class, req.DeviceList)
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

// Prepare will execute SPDK setup.sh script to rebind PCI devices as selected
// by bdev_include and bdev_exclude white and black list filters provided in the
// server config file. This will make the devices available though SPDK.
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

func (b *spdkBackend) UpdateFirmware(pciAddr string, path string, slot int32) error {
	if pciAddr == "" {
		return FaultBadPCIAddr("")
	}

	restoreOutput, err := b.binding.init(b.log, spdk.EnvOptions{
		DisableVMD: b.IsVMDDisabled(),
	})
	if err != nil {
		return err
	}
	defer restoreOutput()

	cs, err := b.binding.Discover(b.log)
	if err != nil {
		return errors.Wrap(err, "failed to discover nvme")
	}

	var found bool
	for _, c := range cs {
		if c.PciAddr == pciAddr {
			found = true
			break
		}
	}

	if !found {
		return FaultPCIAddrNotFound(pciAddr)
	}

	if err := b.binding.Update(b.log, pciAddr, path, slot); err != nil {
		return err
	}

	return nil
}
