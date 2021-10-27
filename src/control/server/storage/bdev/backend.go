//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package bdev

import (
	"os"
	"os/user"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"syscall"

	"github.com/pkg/errors"

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
	}

	spdkBackend struct {
		log     logging.Logger
		binding *spdkWrapper
		script  *spdkSetupScript
	}

	removeFn     func(string) error
	userLookupFn func(string) (*user.User, error)
	vmdDetectFn  func() ([]string, error)
	hpCleanFn    func(string, string, string) error
	writeConfFn  func(logging.Logger, *storage.BdevWriteConfigRequest) error
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
	log.Debug("spdk backend init (bindings call)")

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

// prepare receives function pointers for external interfaces.
func (sb *spdkBackend) prepare(req storage.BdevPrepareRequest, userLookup userLookupFn, vmdDetect vmdDetectFn, hpClean hpCleanFn) (*storage.BdevPrepareResponse, error) {
	resp := &storage.BdevPrepareResponse{}

	usr, err := userLookup(req.TargetUser)
	if err != nil {
		return nil, errors.Wrapf(err, "lookup on local host")
	}

	if !req.DisableCleanHugePages {
		// remove hugepages matching /dev/hugepages/spdk* owned by target user
		err := hpClean(hugePageDir, hugePagePrefix, usr.Uid)
		if err != nil {
			return nil, errors.Wrapf(err, "clean spdk hugepages")
		}
	}

	// If VMD has been explicitly enabled and there are VMD enabled
	// NVMe devices on the host, attempt to prepare them first.
	vmdReq, err := getVMDPrepReq(sb.log, &req, vmdDetect)
	if err != nil {
		return nil, err
	}
	if vmdReq != nil {
		if err := sb.script.Prepare(vmdReq); err != nil {
			return nil, errors.Wrap(err, "re-binding vmd ssds to attach with spdk")
		}
		resp.VMDPrepared = true
	}

	// Prepare non-VMD devices.
	req.EnableVMD = false
	return resp, errors.Wrap(sb.script.Prepare(&req), "re-binding ssds to attach with spdk")
}

// reset receives function pointers for external interfaces.
func (sb *spdkBackend) reset(req storage.BdevPrepareRequest, vmdDetect vmdDetectFn) error {
	// If VMD has been explicitly enabled and there are VMD enabled
	// NVMe devices on the host, attempt to prepare them first.
	vmdReq, err := getVMDPrepReq(sb.log, &req, vmdDetect)
	if err != nil {
		return err
	}
	if vmdReq != nil {
		if err := sb.script.Reset(vmdReq); err != nil {
			return errors.Wrap(err, "un-binding vmd ssds")
		}
	}

	// Reset non-VMD devices.
	req.EnableVMD = false
	return errors.Wrap(sb.script.Reset(&req), "un-binding vmd ssds")
}

// Reset will perform a lookup on the requested target user to validate existence
// then reset non-VMD NVMe devices for use by the OS/kernel.
// If EnableVmd is true in request then attempt to use VMD NVMe devices.
// If DisableCleanHugePages is false in request then cleanup any leftover hugepages
// owned by the target user.
// Backend call executes the SPDK setup.sh script to rebind PCI devices as selected by
// bdev_include and bdev_exclude list filters provided in the server config file.
func (sb *spdkBackend) Reset(req storage.BdevPrepareRequest) error {
	sb.log.Debugf("spdk backend reset (script call): %+v", req)
	return sb.reset(req, detectVMD)
}

// Prepare will perform a lookup on the requested target user to validate existence
// then prepare non-VMD NVMe devices for use with SPDK.
// If EnableVmd is true in request then attempt to use VMD NVMe devices.
// If DisableCleanHugePages is false in request then cleanup any leftover hugepages
// owned by the target user.
// Backend call executes the SPDK setup.sh script to rebind PCI devices as selected by
// bdev_include and bdev_exclude list filters provided in the server config file.
func (sb *spdkBackend) Prepare(req storage.BdevPrepareRequest) (*storage.BdevPrepareResponse, error) {
	sb.log.Debugf("spdk backend prepare (script call): %+v", req)
	return sb.prepare(req, user.Lookup, detectVMD, cleanHugePages)
}

// groomDiscoveredBdevs ensures that for a non-empty device list, restrict output controller data
// to only those devices discovered and in device list and confirm that the devices specified in
// the device list have all been discovered.
func groomDiscoveredBdevs(req storage.BdevScanRequest, discovered storage.NvmeControllers) (storage.NvmeControllers, error) {
	// if empty device list, return all discovered controllers
	if len(req.DeviceList) == 0 {
		return discovered, nil
	}

	var missing []string
	out := make(storage.NvmeControllers, 0)
	vmds := make(map[string]storage.NvmeControllers)

	// store discovered VMD backing devices under vmd address key
	for _, ctrlr := range discovered {
		vmdAddr, isVMDBackingAddr := backingAddrToVMD(ctrlr.PciAddr)
		if isVMDBackingAddr {
			vmds[vmdAddr] = append(vmds[vmdAddr], ctrlr)
		}
	}

	for _, want := range req.DeviceList {
		found := false
		for _, got := range discovered {
			// check if discovered ctrlr is in device list
			if got.PciAddr == want {
				out = append(out, got)
				found = true
				break
			}
		}

		if !found && req.VMDEnabled {
			// check if discovered ctrlr is backing devices for vmd in device list
			if backing, exists := vmds[want]; exists {
				out = append(out, backing...)
				found = true
			}
		}

		if !found {
			missing = append(missing, want)
		}
	}

	if len(missing) > 0 {
		return nil, FaultBdevNotFound(missing...)
	}

	return out, nil
}

// Scan discovers NVMe controllers accessible by SPDK.
func (sb *spdkBackend) Scan(req storage.BdevScanRequest) (*storage.BdevScanResponse, error) {
	sb.log.Debugf("spdk backend scan (bindings discover call): %+v", req)

	restoreOutput, err := sb.binding.init(sb.log, &spdk.EnvOptions{
		PCIAllowList: req.DeviceList,
		EnableVMD:    req.VMDEnabled,
	})
	if err != nil {
		return nil, err
	}
	defer restoreOutput()

	discoveredBdevs, err := sb.binding.Discover(sb.log)
	if err != nil {
		return nil, errors.Wrap(err, "failed to discover nvme")
	}

	outBdevs, err := groomDiscoveredBdevs(req, discoveredBdevs)
	if err != nil {
		return nil, err
	}
	if len(outBdevs) != len(discoveredBdevs) {
		sb.log.Debugf("scan bdevs filtered, in: %v, out: %v (devlist %v)", discoveredBdevs,
			outBdevs, req.DeviceList)
	}

	return &storage.BdevScanResponse{
		Controllers: outBdevs,
		VMDEnabled:  req.VMDEnabled,
	}, nil
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

func (sb *spdkBackend) formatNvme(req *storage.BdevFormatRequest) (*storage.BdevFormatResponse, error) {
	deviceList := req.Properties.DeviceList
	if len(deviceList) == 0 {
		sb.log.Debug("skip nvme format as bdev device list is empty")
		return &storage.BdevFormatResponse{}, nil
	}

	if req.VMDEnabled {
		sb.log.Debug("vmd support enabled during nvme format")
		dl, err := substituteVMDAddresses(sb.log, deviceList, req.BdevCache)
		if err != nil {
			return nil, err
		}
		deviceList = dl
	}

	spdkOpts := &spdk.EnvOptions{
		PCIAllowList: deviceList,
		EnableVMD:    req.VMDEnabled,
	}

	restoreOutput, err := sb.binding.init(sb.log, spdkOpts)
	if err != nil {
		return nil, err
	}
	defer restoreOutput()
	defer sb.binding.FiniSPDKEnv(sb.log, spdkOpts)
	defer func() {
		if err := sb.binding.CleanLockfiles(sb.log, deviceList...); err != nil {
			sb.log.Errorf("cleanup failed after format: %s", err)
		}
	}()

	results, err := sb.binding.Format(sb.log)
	if err != nil {
		return nil, errors.Wrapf(err, "spdk format %v", deviceList)
	}

	if len(results) == 0 {
		return nil, errors.New("empty results from spdk binding format request")
	}

	return sb.formatRespFromResults(results)
}

// Format delegates to class specific format functions.
func (sb *spdkBackend) Format(req storage.BdevFormatRequest) (resp *storage.BdevFormatResponse, err error) {
	sb.log.Debugf("spdk backend format (bindings call): %+v", req)

	// TODO (DAOS-3844): Kick off device formats in parallel?
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

func (sb *spdkBackend) writeNVMEConf(req storage.BdevWriteConfigRequest, confWriter writeConfFn) error {
	sb.log.Debugf("spdk backend write config (system calls): %+v", req)

	// Substitute addresses in bdev tier's DeviceLists if VMD is in use.
	if req.VMDEnabled {
		sb.log.Debug("vmd support enabled during nvme config write")
		tps := make([]storage.BdevTierProperties, 0, len(req.TierProps))
		copy(req.TierProps, tps)
		for _, props := range req.TierProps {
			if props.Class != storage.ClassNvme {
				continue
			}

			dl, err := substituteVMDAddresses(sb.log, props.DeviceList, req.BdevCache)
			if err != nil {
				return err
			}
			props.DeviceList = dl
			tps = append(tps, props)
		}
		req.TierProps = tps
	}

	return errors.Wrap(confWriter(sb.log, &req), "write spdk nvme config")
}

func (sb *spdkBackend) WriteConfig(req storage.BdevWriteConfigRequest) (*storage.BdevWriteConfigResponse, error) {
	return &storage.BdevWriteConfigResponse{}, sb.writeNVMEConf(req, writeJSONConf)
}

// UpdateFirmware uses the SPDK bindings to update an NVMe controller's firmware.
func (sb *spdkBackend) UpdateFirmware(pciAddr string, path string, slot int32) error {
	sb.log.Debug("spdk backend update firmware")

	if pciAddr == "" {
		return FaultBadPCIAddr("")
	}

	if err := sb.binding.Update(sb.log, pciAddr, path, slot); err != nil {
		return err
	}

	return nil
}
