//
// (C) Copyright 2019-2023 Intel Corporation.
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package bdev

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"sort"
	"syscall"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/lib/spdk"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/provider/system"
	"github.com/daos-stack/daos/src/control/server/storage"
)

const (
	hugepageDir         = "/dev/hugepages"
	spdkHugepagePattern = `spdk_pid([0-9]+)map`
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

	statFn      func(string) (os.FileInfo, error)
	removeFn    func(string) error
	vmdDetectFn func() (*hardware.PCIAddressSet, error)
	hpCleanFn   func(logging.Logger, string) (uint, error)
	writeConfFn func(logging.Logger, *storage.BdevWriteConfigRequest) error
	restoreFn   func()
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

	if err = Dup2(int(devNull.Fd()), syscall.Stdout); err != nil {
		return
	}

	restore = func() {
		// NB: Normally panic() in production code is frowned upon, but in this
		// case if we get errors there really isn't any handling to be done
		// because things have gone completely sideways.
		if err := devNull.Close(); err != nil {
			panic(err)
		}
		if err := Dup2(realStdout, syscall.Stdout); err != nil {
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

func isPIDActive(pidStr string, stat statFn) (bool, error) {
	filename := fmt.Sprintf("/proc/%s", pidStr)

	if _, err := stat(filename); err != nil {
		if os.IsNotExist(err) {
			return false, nil
		}
		return false, err
	}

	return true, nil
}

// createHugepageWalkFunc returns a filepath.WalkFunc that will remove any file whose
// name begins with prefix and encoded pid is inactive.
func createHugepageWalkFunc(log logging.Logger, topDir string, stat statFn, remove removeFn, count *uint) filepath.WalkFunc {
	re := regexp.MustCompile(spdkHugepagePattern)

	return func(path string, info os.FileInfo, err error) error {
		switch {
		case err != nil:
			return err
		case info == nil:
			return errors.New("nil fileinfo")
		case info.IsDir():
			log.Debugf("walk func: dir %s", path)
			if path == topDir {
				return nil
			}
			return filepath.SkipDir // skip subdirectories
		}

		matches := re.FindStringSubmatch(info.Name())
		if len(matches) != 2 {
			log.Debugf("walk func: unexpected name, skipping %s", path)
			return nil // skip files not matching expected pattern
		}
		// PID string will be the first submatch at index 1 of the match results.

		if isActive, err := isPIDActive(matches[1], stat); err != nil || isActive {
			log.Debugf("walk func: active owner proc, skipping %s", path)
			return err // skip files created by an existing process (isActive == true)
		}

		log.Debugf("walk func: removing %s", path)
		if err := remove(path); err != nil {
			return err
		}
		*count++

		return nil
	}
}

// cleanHugepages removes hugepage files with pathPrefix that are owned by the user with username
// tgtUsr by processing directory tree with filepath.WalkFunc returned from hugepageWalkFunc.
func cleanHugepages(log logging.Logger, topDir string) (count uint, _ error) {
	return count, filepath.Walk(topDir,
		createHugepageWalkFunc(log, topDir, os.Stat, os.Remove, &count))
}

func logNUMAStats(log logging.Logger) {
	var toLog string

	out, err := exec.Command("numastat", "-m").Output()
	if err != nil {
		toLog = (&system.RunCmdError{
			Wrapped: err,
			Stdout:  string(out),
		}).Error()
	} else {
		toLog = string(out)
	}

	log.Debugf("run cmd numastat -m: %s", toLog)
}

// prepare receives function pointers for external interfaces.
func (sb *spdkBackend) prepare(req storage.BdevPrepareRequest, vmdDetect vmdDetectFn, hpClean hpCleanFn) (*storage.BdevPrepareResponse, error) {
	resp := &storage.BdevPrepareResponse{}

	if req.CleanHugepagesOnly {
		// Remove hugepages that were created by a no-longer-active SPDK process. Note that
		// when running prepare, it's unlikely that any SPDK processes are active as this
		// is performed prior to starting engines.
		nrRemoved, err := hpClean(sb.log, hugepageDir)
		if err != nil {
			return resp, errors.Wrapf(err, "clean spdk hugepages")
		}
		resp.NrHugepagesRemoved = nrRemoved

		logNUMAStats(sb.log)

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
func (sb *spdkBackend) reset(req storage.BdevPrepareRequest, vmdDetect vmdDetectFn) (*storage.BdevPrepareResponse, error) {
	resp := &storage.BdevPrepareResponse{}

	// Update request if VMD has been explicitly enabled and there are VMD endpoints configured.
	if err := updatePrepareRequest(sb.log, &req, vmdDetect); err != nil {
		return resp, errors.Wrapf(err, "update prepare request")
	}
	resp.VMDPrepared = req.EnableVMD

	return resp, errors.Wrap(sb.script.Reset(&req), "unbinding nvme devices from userspace drivers")
}

// Reset will perform a lookup on the requested target user to validate existence
// then reset non-VMD NVMe devices for use by the OS/kernel.
// If EnableVmd is true in request then attempt to use VMD NVMe devices.
// If DisableCleanHugepages is false in request then cleanup any leftover hugepages
// owned by the target user.
// Backend call executes the SPDK setup.sh script to rebind PCI devices as selected by
// devs specified in bdev_list and bdev_exclude provided in the server config file.
func (sb *spdkBackend) Reset(req storage.BdevPrepareRequest) (*storage.BdevPrepareResponse, error) {
	sb.log.Debugf("spdk backend reset (script call): %+v", req)
	return sb.reset(req, DetectVMD)
}

// Prepare will perform a lookup on the requested target user to validate existence
// then prepare non-VMD NVMe devices for use with SPDK.
// If EnableVmd is true in request then attempt to use VMD NVMe devices.
// If DisableCleanHugepages is false in request then cleanup any leftover hugepages
// owned by the target user.
// Backend call executes the SPDK setup.sh script to rebind PCI devices as selected by
// devs specified in bdev_list and bdev_exclude provided in the server config file.
func (sb *spdkBackend) Prepare(req storage.BdevPrepareRequest) (*storage.BdevPrepareResponse, error) {
	sb.log.Debugf("spdk backend prepare (script call): %+v", req)
	return sb.prepare(req, DetectVMD, cleanHugepages)
}

// groomDiscoveredBdevs ensures that for a non-empty device list, restrict output controller data
// to only those devices discovered and in device list and confirm that the devices specified in
// the device list have all been discovered. VMD addresses with no backing devices return error.
func groomDiscoveredBdevs(reqDevs *hardware.PCIAddressSet, discovered storage.NvmeControllers, vmdEnabled bool) (storage.NvmeControllers, error) {
	// if the request does not specify a device filter, return all discovered controllers
	if reqDevs.IsEmpty() {
		return discovered, nil
	}

	var missing hardware.PCIAddressSet
	out := make(storage.NvmeControllers, 0)

	var vmds map[string]storage.NvmeControllers
	if vmdEnabled {
		vmdMap, err := mapVMDToBackingDevs(discovered)
		if err != nil {
			return nil, err
		}
		vmds = vmdMap
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
		return nil, storage.FaultBdevNotFound(vmdEnabled, missing.Strings()...)
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
	sb.log.Debugf("spdk backend scan (bindings discover call) resp: %+v", foundDevs)

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
		resp.DeviceResponses[path] = createAioFile(sb.log, path, req)
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
		dl, err := substituteVMDAddresses(sb.log, needDevs, req.ScannedBdevs)
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

			dl, err := substituteVMDAddresses(sb.log, bdevs, req.ScannedBdevs)
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

// WriteConfig writes the SPDK configuration file.
func (sb *spdkBackend) WriteConfig(req storage.BdevWriteConfigRequest) (*storage.BdevWriteConfigResponse, error) {
	return &storage.BdevWriteConfigResponse{}, sb.writeNvmeConfig(req, writeJsonConfig)
}

// ReadConfig reads the SPDK configuration file.
// NB: Currently returns an empty response struct if the file is read
// and parsed successfully, but may be extended to return the
// parsed configuration.
func (sb *spdkBackend) ReadConfig(req storage.BdevReadConfigRequest) (*storage.BdevReadConfigResponse, error) {
	if req.ConfigPath == "" {
		return nil, errors.New("empty SPDK config path")
	}

	r, err := os.Open(req.ConfigPath)
	if err != nil {
		return nil, errors.Wrapf(err, "failed to open SPDK config at %q", req.ConfigPath)
	}
	defer r.Close()

	_, err = readSpdkConfig(r)
	if err != nil {
		return nil, err
	}

	// TODO: Reconstruct the WriteConfig request params? At the moment, all we care about is
	// that we can read and parse the config file, but in the future it might be useful to
	// be able to inspect its contents.
	resp := &storage.BdevReadConfigResponse{}
	return resp, nil
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
