//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package bdev

import (
	"fmt"
	"os"
	"os/user"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"syscall"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/hardware"
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
	vmdDetectFn  func() (*hardware.PCIAddressSet, error)
	hpCleanFn    func(string, string, string) (uint, error)
	writeConfFn  func(logging.Logger, *storage.BdevWriteConfigRequest) error
	restoreFn    func()
)

// suppressOutput is a horrible, horrible hack necessitated by the fact that
// SPDK blathers to stdout, causing console spam and messing with our secure
// communications channel between the server and privileged helper.

func (w *spdkWrapper) suppressOutput() (restore restoreFn, err error) {
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

func (w *spdkWrapper) init(log logging.Logger, spdkOpts *spdk.EnvOptions) (restoreFn, error) {
	log.Debug("spdk backend init (bindings call)")

	restoreOutput, err := w.suppressOutput()
	if err != nil {
		return nil, errors.Wrap(err, "failed to suppress spdk output")
	}

	if err := w.InitSPDKEnv(log, spdkOpts); err != nil {
		restoreOutput()
		return nil, errors.Wrap(err, "failed to init spdk env")
	}

	return restoreOutput, nil
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
func hugePageWalkFunc(hugePageDir, prefix, tgtUID string, remove removeFn, count *uint) filepath.WalkFunc {
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
		*count++

		return nil
	}
}

// cleanHugePages removes hugepage files with pathPrefix that are owned by the user with username
// tgtUsr by processing directory tree with filepath.WalkFunc returned from hugePageWalkFunc.
func cleanHugePages(hugePageDir, prefix, tgtUID string) (count uint, _ error) {
	return count, filepath.Walk(hugePageDir,
		hugePageWalkFunc(hugePageDir, prefix, tgtUID, os.Remove, &count))
}

// prepare receives function pointers for external interfaces.
func (sb *spdkBackend) prepare(req storage.BdevPrepareRequest, userLookup userLookupFn, vmdDetect vmdDetectFn, hpClean hpCleanFn) (*storage.BdevPrepareResponse, error) {
	resp := &storage.BdevPrepareResponse{}

	usr, err := userLookup(req.TargetUser)
	if err != nil {
		return resp, errors.Wrapf(err, "lookup on local host")
	}

	// Remove hugepages matching file name beginning with prefix and owned by the target user.
	hpPrefix := hugePagePrefix
	if req.CleanHugePagesPID != 0 {
		// If a pid is supplied then include in the prefix.
		hpPrefix = fmt.Sprintf("%s_pid%dmap", hpPrefix, req.CleanHugePagesPID)
	}
	nrRemoved, err := hpClean(hugePageDir, hpPrefix, usr.Uid)
	if err != nil {
		return resp, errors.Wrapf(err, "clean spdk hugepages")
	}
	resp.NrHugePagesRemoved = nrRemoved
	if req.CleanHugePagesOnly {
		return resp, nil
	}

	// Update request if VMD has been explicitly enabled and there are VMD endpoints configured.
	if err := updatePrepareRequest(sb.log, &req, vmdDetect); err != nil {
		return resp, errors.Wrapf(err, "update prepare request")
	}
	resp.VMDPrepared = req.EnableVMD

	// Before preparing, reset device bindings.
	if req.EnableVMD {
		// Unbind devices to speed up VMD re-binding as per
		// https://github.com/spdk/spdk/commit/b0aba3fcd5aceceea530a702922153bc75664978.
		//
		// Applies block (not allow) list if VMD is configured so specific NVMe devices can
		// be reserved for other use (bdev_exclude).
		if err := sb.script.Unbind(&req); err != nil {
			return resp, errors.Wrap(err, "un-binding devices")
		}
	} else {
		if err := sb.script.Reset(&req); err != nil {
			return resp, errors.Wrap(err, "resetting device bindings")
		}
	}

	return resp, errors.Wrap(sb.script.Prepare(&req), "binding devices to userspace drivers")
}

// reset receives function pointers for external interfaces.
func (sb *spdkBackend) reset(req storage.BdevPrepareRequest, vmdDetect vmdDetectFn) error {
	// Update request if VMD has been explicitly enabled and there are VMD endpoints configured.
	if err := updatePrepareRequest(sb.log, &req, vmdDetect); err != nil {
		return errors.Wrapf(err, "update prepare request")
	}

	if req.EnableVMD {
		// First run with VMD addresses in allow list and then without to reset backing SSDs.
		if err := sb.script.Reset(&req); err != nil {
			return errors.Wrap(err, "unbinding vmd endpoints from userspace drivers")
		}
		req.PCIAllowList = ""
	}

	return errors.Wrap(sb.script.Reset(&req), "unbinding ssds from userspace drivers")
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
	return sb.reset(req, DetectVMD)
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
	return sb.prepare(req, user.Lookup, DetectVMD, cleanHugePages)
}

// groomDiscoveredBdevs ensures that for a non-empty device list, restrict output controller data
// to only those devices discovered and in device list and confirm that the devices specified in
// the device list have all been discovered.
func groomDiscoveredBdevs(reqDevs *hardware.PCIAddressSet, discovered storage.NvmeControllers, vmdEnabled bool) (storage.NvmeControllers, error) {
	// if the request does not specify a device filter, return all discovered controllers
	if reqDevs.IsEmpty() {
		return discovered, nil
	}

	var missing hardware.PCIAddressSet
	out := make(storage.NvmeControllers, 0)

	vmds, err := mapVMDToBackingDevs(discovered)
	if err != nil {
		return nil, err
	}

	for _, want := range reqDevs.Addresses() {
		found := false
		for _, got := range discovered {
			// check if discovered ctrlr is in device list
			if got.PciAddr == want.String() {
				out = append(out, got)
				found = true
				break
			}
		}

		if !found && vmdEnabled {
			// check if discovered ctrlr is backing devices for vmd in device list
			if backing, exists := vmds[want.String()]; exists {
				out = append(out, backing...)
				found = true
			}
		}

		if !found {
			if err := missing.Add(want); err != nil {
				return nil, err
			}
		}
	}

	if !missing.IsEmpty() {
		return nil, storage.FaultBdevNotFound(missing.Strings()...)
	}

	return out, nil
}

// Scan discovers NVMe controllers accessible by SPDK.
func (sb *spdkBackend) Scan(req storage.BdevScanRequest) (*storage.BdevScanResponse, error) {
	sb.log.Debugf("spdk backend scan (bindings discover call): %+v", req)

	// Only filter devices if all have a PCI address, avoid validating the presence of emulated
	// NVMe devices as they may not exist yet e.g. for SPDK AIO-file the devices are created on
	// format.
	needDevs := req.DeviceList.PCIAddressSetPtr()
	spdkOpts := &spdk.EnvOptions{
		PCIAllowList: needDevs,
		EnableVMD:    req.VMDEnabled,
	}

	restoreAfterInit, err := sb.binding.init(sb.log, spdkOpts)
	if err != nil {
		return nil, errors.Wrap(err, "failed to init nvme")
	}
	defer restoreAfterInit()

	foundDevs, err := sb.binding.Discover(sb.log)
	if err != nil {
		return nil, errors.Wrap(err, "failed to discover nvme")
	}

	outDevs, err := groomDiscoveredBdevs(needDevs, foundDevs, req.VMDEnabled)
	if err != nil {
		return nil, err
	}
	if len(outDevs) != len(foundDevs) {
		sb.log.Debugf("scan bdevs filtered, in: %v, out: %v (requested %s)", foundDevs,
			outDevs, needDevs)
	}

	return &storage.BdevScanResponse{
		Controllers: outDevs,
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
		if result.CtrlrPCIAddr == "" {
			return nil, errors.Errorf("result is missing ctrlr address: %+v",
				result)
		}

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
		sb.log.Debugf("format device response for %q: %+v", addr, devResp)
		resp.DeviceResponses[addr] = devResp
	}

	return resp, nil
}

func (sb *spdkBackend) formatAioFile(req *storage.BdevFormatRequest) (*storage.BdevFormatResponse, error) {
	resp := &storage.BdevFormatResponse{
		DeviceResponses: make(storage.BdevDeviceFormatResponses),
	}

	for _, path := range req.Properties.DeviceList.Devices() {
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

	for _, device := range req.Properties.DeviceList.Devices() {
		resp.DeviceResponses[device] = new(storage.BdevDeviceFormatResponse)
		sb.log.Debugf("%s format for non-NVMe bdev skipped on %s", req.Properties.Class, device)
	}

	return resp, nil
}

func (sb *spdkBackend) formatNvme(req *storage.BdevFormatRequest) (*storage.BdevFormatResponse, error) {
	needDevs := req.Properties.DeviceList.PCIAddressSetPtr()

	if needDevs.IsEmpty() {
		sb.log.Debug("skip nvme format as bdev device list is empty")
		return &storage.BdevFormatResponse{}, nil
	}

	if req.VMDEnabled {
		sb.log.Debug("vmd support enabled during nvme format")
		dl, err := substituteVMDAddresses(sb.log, needDevs, req.BdevCache)
		if err != nil {
			return nil, err
		}
		needDevs = dl
	}

	spdkOpts := &spdk.EnvOptions{
		PCIAllowList: needDevs,
		EnableVMD:    req.VMDEnabled,
	}

	restoreAfterInit, err := sb.binding.init(sb.log, spdkOpts)
	if err != nil {
		return nil, err
	}
	defer restoreAfterInit()

	sb.log.Debugf("calling spdk bindings format")
	results, err := sb.binding.Format(sb.log)
	if err != nil {
		return nil, errors.Wrapf(err, "spdk format %s", needDevs)
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

func (sb *spdkBackend) writeNvmeConfig(req storage.BdevWriteConfigRequest, confWriter writeConfFn) error {
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

			bdevs := &props.DeviceList.PCIAddressSet

			dl, err := substituteVMDAddresses(sb.log, bdevs, req.BdevCache)
			if err != nil {
				return errors.Wrapf(err, "storage tier %d", props.Tier)
			}
			props.DeviceList = &storage.BdevDeviceList{PCIAddressSet: *dl}
			tps = append(tps, props)
		}
		req.TierProps = tps
	}

	return errors.Wrap(confWriter(sb.log, &req), "write spdk nvme config")
}

func (sb *spdkBackend) WriteConfig(req storage.BdevWriteConfigRequest) (*storage.BdevWriteConfigResponse, error) {
	return &storage.BdevWriteConfigResponse{}, sb.writeNvmeConfig(req, writeJsonConfig)
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
