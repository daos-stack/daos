//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package bdev

import (
	"bufio"
	"bytes"
	"os"
	"os/exec"
	"os/user"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"syscall"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/spdk"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

const (
	hugePageDir    = "/dev/hugepages"
	hugePagePrefix = "spdk"
)

type (
	spdkWrapper struct {
		spdk.Env
		spdk.Nvme

		vmdEnabled bool
	}

	spdkBackend struct {
		log     logging.Logger
		binding *spdkWrapper
		script  *spdkSetupScript
	}

	removeFn     func(string) error
	scriptCallFn func(*storage.BdevPrepareRequest) error
	userLookupFn func(string) (*user.User, error)
	vmdDetectFn  func() ([]string, error)
	hpCleanFn    func(string, string, string) error
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

func (w *spdkWrapper) init(log logging.Logger, spdkOpts *spdk.EnvOptions) (func(), error) {
	restore, err := w.suppressOutput()
	if err != nil {
		return nil, errors.Wrap(err, "failed to suppress spdk output")
	}

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

//// EnableVMD turns on VMD device awareness.
//func (sb *spdkBackend) EnableVMD() {
// sb.binding.vmdEnabled = true
//}

//// IsVMDEnabled checks for VMD device awareness.
//func (sb *spdkBackend) IsVMDEnabled() bool {
// return sb.binding.vmdEnabled
//}

// Scan discovers NVMe controllers accessible by SPDK.
func (sb *spdkBackend) Scan(req storage.BdevScanRequest) (*storage.BdevScanResponse, error) {
	restoreOutput, err := sb.binding.init(sb.log, &spdk.EnvOptions{
		PCIAllowList: req.DeviceList,
		//		EnableVMD:    sb.IsVMDEnabled(),
	})
	if err != nil {
		return nil, err
	}
	defer restoreOutput()

	cs, err := sb.binding.Discover(sb.log)
	if err != nil {
		return nil, errors.Wrap(err, "failed to discover nvme")
	}

	return &storage.BdevScanResponse{Controllers: cs}, nil
}

func (sb *spdkBackend) formatRespFromResults(results []*spdk.FormatResult) (*storage.BdevFormatResponse, error) {
	resp := &storage.BdevFormatResponse{
		DeviceResponses: make(storage.BdevDeviceFormatResponses),
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

		sb.log.Debugf("formatted namespaces %v on nvme device at %s", formatted, addr)

		devResp := new(storage.BdevDeviceFormatResponse)
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

func (sb *spdkBackend) formatNvme(req *storage.BdevFormatRequest) (*storage.BdevFormatResponse, error) {
	if len(req.Properties.DeviceList) == 0 {
		sb.log.Debug("skip nvme format as bdev device list is empty")
		return &storage.BdevFormatResponse{}, nil
	}

	spdkOpts := &spdk.EnvOptions{
		PCIAllowList: req.Properties.DeviceList,
		// EnableVMD:    sb.IsVMDEnabled(),
	}

	restoreOutput, err := sb.binding.init(sb.log, spdkOpts)
	if err != nil {
		return nil, err
	}
	defer restoreOutput()
	defer sb.binding.FiniSPDKEnv(sb.log, spdkOpts)
	defer func() {
		if err := sb.binding.CleanLockfiles(sb.log, req.Properties.DeviceList...); err != nil {
			sb.log.Errorf("cleanup failed after format: %s", err)
		}
	}()

	results, err := sb.binding.Format(sb.log)
	if err != nil {
		return nil, errors.Wrapf(err, "spdk format %v", req.Properties.DeviceList)
	}

	if len(results) == 0 {
		return nil, errors.New("empty results from spdk binding format request")
	}

	return sb.formatRespFromResults(results)
}

func (sb *spdkBackend) formatAioFile(req *storage.BdevFormatRequest) (*storage.BdevFormatResponse, error) {
	resp := &storage.BdevFormatResponse{
		DeviceResponses: make(storage.BdevDeviceFormatResponses),
	}

	for _, path := range req.Properties.DeviceList {
		devResp := new(storage.BdevDeviceFormatResponse)
		resp.DeviceResponses[path] = devResp
		if err := createEmptyFile(sb.log, path, req.Properties.DeviceFileSize); err != nil {
			devResp.Error = FaultFormatError(path, err)
			continue
		}
		if err := os.Chown(path, req.OwnerUID, req.OwnerGID); err != nil {
			devResp.Error = FaultFormatError(path, errors.Wrapf(err,
				"failed to set ownership of %q to %d.%d", path,
				req.OwnerUID, req.OwnerGID))
		}
	}

	return resp, nil
}

// TODO DAOS-6039: implement kdev fs format
func (sb *spdkBackend) formatKdev(req *storage.BdevFormatRequest) (*storage.BdevFormatResponse, error) {
	resp := &storage.BdevFormatResponse{
		DeviceResponses: make(storage.BdevDeviceFormatResponses),
	}

	for _, device := range req.Properties.DeviceList {
		resp.DeviceResponses[device] = new(storage.BdevDeviceFormatResponse)
		sb.log.Debugf("%s format for non-NVMe bdev skipped on %s", req.Properties.Class, device)
	}

	return resp, nil
}

// Format delegates to class specific format functions.
func (sb *spdkBackend) Format(req storage.BdevFormatRequest) (resp *storage.BdevFormatResponse, err error) {
	// TODO (DAOS-3844): Kick off device formats parallel?
	switch req.Properties.Class {
	case storage.ClassFile:
		return sb.formatAioFile(&req)
	case storage.ClassKdev:
		return sb.formatKdev(&req)
	case storage.ClassNvme:
		return sb.formatNvme(&req)
	default:
		return nil, FaultFormatUnknownClass(req.Properties.Class.String())
	}
}

func (sb *spdkBackend) WriteNvmeConfig(req storage.BdevWriteNvmeConfigRequest) (*storage.BdevWriteNvmeConfigResponse, error) {
	if err := sb.writeNvmeConfig(&req); err != nil {
		return nil, errors.Wrap(err, "write spdk nvme config")
	}
	res := new(storage.BdevWriteNvmeConfigResponse)
	return res, nil
}

// hugePageWalkFunc returns a filepath.WalkFunc that will remove any file whose
// name begins with prefix and owner has uid equal to tgtUID.
func hugePageWalkFunc(hugePageDir, prefix, tgtUID string, remove removeFn) filepath.WalkFunc {
	return func(path string, info os.FileInfo, err error) error {
		switch {
		case err != nil:
			return err
		case info == nil:
			return errors.New("nil fileinfo")
		case info.IsDir():
			if path == hugePageDir {
				return nil
			}
			return filepath.SkipDir // skip subdirectories
		case !strings.HasPrefix(info.Name(), prefix):
			return nil // skip files without prefix
		}

		stat, ok := info.Sys().(*syscall.Stat_t)
		if !ok || stat == nil {
			return errors.New("stat missing for file")
		}
		if strconv.Itoa(int(stat.Uid)) != tgtUID {
			return nil // skip not owned by target user
		}

		if err := remove(path); err != nil {
			return err
		}

		return nil
	}
}

// cleanHugePages removes hugepage files with pathPrefix that are owned by the
// user with username tgtUsr by processing directory tree with filepath.WalkFunc
// returned from hugePageWalkFunc.
func cleanHugePages(hugePageDir, prefix, tgtUID string) error {
	return filepath.Walk(hugePageDir,
		hugePageWalkFunc(hugePageDir, prefix, tgtUID, os.Remove))
}

// detectVMD returns whether VMD devices have been found and a slice of VMD
// PCI addresses if found.
func detectVMD() ([]string, error) {
	// Check available VMD devices with command:
	// "$lspci | grep  -i -E "201d | Volume Management Device"
	lspciCmd := exec.Command("lspci")
	vmdCmd := exec.Command("grep", "-i", "-E", "201d|Volume Management Device")
	var cmdOut bytes.Buffer
	var prefixIncluded bool

	vmdCmd.Stdin, _ = lspciCmd.StdoutPipe()
	vmdCmd.Stdout = &cmdOut
	_ = lspciCmd.Start()
	_ = vmdCmd.Run()
	_ = lspciCmd.Wait()

	if cmdOut.Len() == 0 {
		return []string{}, nil
	}

	vmdCount := bytes.Count(cmdOut.Bytes(), []byte("0000:"))
	if vmdCount == 0 {
		// sometimes the output may not include "0000:" prefix
		// usually when muliple devices are in PCI_ALLOWED
		vmdCount = bytes.Count(cmdOut.Bytes(), []byte("Volume"))
		if vmdCount == 0 {
			vmdCount = bytes.Count(cmdOut.Bytes(), []byte("201d"))
		}
	} else {
		prefixIncluded = true
	}
	vmdAddrs := make([]string, 0, vmdCount)

	i := 0
	scanner := bufio.NewScanner(&cmdOut)
	for scanner.Scan() {
		if i == vmdCount {
			break
		}
		s := strings.Split(scanner.Text(), " ")
		if !prefixIncluded {
			s[0] = "0000:" + s[0]
		}
		vmdAddrs = append(vmdAddrs, strings.TrimSpace(s[0]))
		i++
	}

	if len(vmdAddrs) == 0 {
		return nil, errors.New("error parsing cmd output")
	}

	return vmdAddrs, nil
}

// vmdProcessFilters takes an input request and a list of discovered VMD addresses.
// The VMD addresses are validated against the input request allow and block lists.
// The output allow list will only contain VMD addresses if either both input allow
// and block lists are empty or if included in allow and not included in block lists.
func vmdProcessFilters(inReq *storage.BdevPrepareRequest, vmdPCIAddrs []string) storage.BdevPrepareRequest {
	var outAllowList []string
	outReq := *inReq

	if inReq.PCIAllowList == "" && inReq.PCIBlockList == "" {
		outReq.PCIAllowList = strings.Join(vmdPCIAddrs, storage.BdevPciAddrSep)
		outReq.PCIBlockList = ""
		return outReq
	}

	if inReq.PCIAllowList != "" {
		allowed := strings.Split(inReq.PCIAllowList, storage.BdevPciAddrSep)
		for _, addr := range vmdPCIAddrs {
			if common.Includes(allowed, addr) {
				outAllowList = append(outAllowList, addr)
			}
		}
		if len(outAllowList) == 0 {
			// no allowed vmd addresses
			outReq.PCIAllowList = ""
			outReq.PCIBlockList = ""
			return outReq
		}
	}

	if inReq.PCIBlockList != "" {
		var outList []string
		inList := outAllowList // in case vmdPCIAddrs list has already been filtered
		if len(inList) == 0 {
			inList = vmdPCIAddrs
		}
		blocked := strings.Split(inReq.PCIBlockList, storage.BdevPciAddrSep)
		for _, addr := range inList {
			if !common.Includes(blocked, addr) {
				outList = append(outList, addr)
			}
		}
		outAllowList = outList
		if len(outAllowList) == 0 {
			// no allowed vmd addresses
			outReq.PCIAllowList = ""
			outReq.PCIBlockList = ""
			return outReq
		}
	}

	outReq.PCIAllowList = strings.Join(outAllowList, storage.BdevPciAddrSep)
	outReq.PCIBlockList = ""
	return outReq
}

// getVMDPrepReq determines if VMD devices are going to be used and returns a
// bdev prepare request with the VMD addresses explicitly set in PCI_ALLOWED list.
//
// If VMD is not to be prepared, a nil request is returned.
func getVMDPrepReq(log logging.Logger, req *storage.BdevPrepareRequest, vmdDetect vmdDetectFn) (*storage.BdevPrepareRequest, error) {
	if !req.EnableVMD {
		return nil, nil
	}

	vmdPCIAddrs, err := vmdDetect()
	if err != nil {
		return nil, errors.Wrap(err, "VMD could not be enabled")
	}

	if len(vmdPCIAddrs) == 0 {
		log.Debug("vmd prep: no vmd devices found")
		return nil, nil
	}
	log.Debugf("volume management devices detected: %v", vmdPCIAddrs)

	vmdReq := vmdProcessFilters(req, vmdPCIAddrs)

	if req.PCIAllowList != "" && vmdReq.PCIAllowList == "" {
		log.Debugf("vmd prep: %v devices not allowed", vmdPCIAddrs)
		return nil, nil
	}
	if req.PCIBlockList != "" && vmdReq.PCIAllowList == "" {
		log.Debugf("vmd prep: %v devices blocked", vmdPCIAddrs)
		return nil, nil
	}
	log.Debugf("volume management devices selected: %v", req.PCIAllowList)

	return &vmdReq, nil
}

// prepare receives function pointers for external interfaces.
func (sb *spdkBackend) prepare(req storage.BdevPrepareRequest, scriptCall scriptCallFn, userLookup userLookupFn, vmdDetect vmdDetectFn, hpClean hpCleanFn) (*storage.BdevPrepareResponse, error) {
	sb.log.Debugf("provider backend prepare %+v", req)
	resp := &storage.BdevPrepareResponse{}

	usr, err := userLookup(req.TargetUser)
	if err != nil {
		return nil, errors.Wrapf(err, "lookup on local host")
	}

	// If VMD has been explicitly enabled and there are VMD enabled
	// NVMe devices on the host, attempt to prepare them first.
	vmdReq, err := getVMDPrepReq(sb.log, &req, vmdDetect)
	if err != nil {
		return nil, err
	}
	if vmdReq != nil {
		if err := scriptCall(vmdReq); err != nil {
			return nil, errors.Wrap(err, "re-binding vmd ssds to attach with spdk")
		}
		resp.VMDPrepared = true
	}

	if err := scriptCall(&req); err != nil {
		return nil, errors.Wrap(err, "re-binding ssds to attach with spdk")
	}

	if !req.DisableCleanHugePages {
		// remove hugepages matching /dev/hugepages/spdk* owned by target user
		err := hpClean(hugePageDir, hugePagePrefix, usr.Uid)
		if err != nil {
			return nil, errors.Wrapf(err, "clean spdk hugepages")
		}
	}

	return resp, nil
}

// Prepare will perform a lookup on the requested target user to validate existence
// then prepare non-VMD NVMe devices for use with SPDK.
// If EnableVmd is true in request then attempt to use VMD NVMe devices.
// If DisableCleanHugePages is false in request then cleanup any leftover hugepages
// owned by the target user.
// Backend call executes the SPDK setup.sh script to rebind PCI devices as selected by
// bdev_include and bdev_exclude list filters provided in the server config file.
func (sb *spdkBackend) Prepare(req storage.BdevPrepareRequest) (*storage.BdevPrepareResponse, error) {
	return sb.prepare(req, sb.script.Prepare, user.Lookup, detectVMD, cleanHugePages)
}

// Reset will perform a lookup on the requested target user to validate existence
// then reset non-VMD NVMe devices for use by the OS/kernel.
// If EnableVmd is true in request then attempt to use VMD NVMe devices.
// If DisableCleanHugePages is false in request then cleanup any leftover hugepages
// owned by the target user.
// Backend call executes the SPDK setup.sh script to rebind PCI devices as selected by
// bdev_include and bdev_exclude list filters provided in the server config file.
func (sb *spdkBackend) Reset(req storage.BdevPrepareRequest) (*storage.BdevPrepareResponse, error) {
	return sb.prepare(req, sb.script.Reset, user.Lookup, detectVMD, cleanHugePages)
}

func (sb *spdkBackend) UpdateFirmware(pciAddr string, path string, slot int32) error {
	if pciAddr == "" {
		return FaultBadPCIAddr("")
	}

	if err := sb.binding.Update(sb.log, pciAddr, path, slot); err != nil {
		return err
	}

	return nil
}
