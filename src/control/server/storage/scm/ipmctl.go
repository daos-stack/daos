//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package scm

import (
	"encoding/json"
	"fmt"
	"os/exec"
	"regexp"
	"strings"
	"sync"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/ipmctl"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

const (
	minNrNssPerSocket = 1
	maxNrNssPerSocket = 8

	createNsAnyRegion = -1

	alignmentBoundaryBytes = humanize.MiByte * 2
)

var badIpmctlVers = []semVer{
	// https://github.com/intel/ipmctl/commit/9e3898cb15fa9eed3ef3e9de4488be1681d53ff4
	{"02", "00", "00", "3809"},
	{"02", "00", "00", "3814"},
	{"02", "00", "00", "3816"},
}

type (
	semVer      []string
	runCmdFn    func(string) (string, error)
	lookPathFn  func(string) (string, error)
	runCmdError struct {
		wrapped error
		stdout  string
	}
)

func (sv semVer) String() string {
	return strings.Join(sv, ".")
}

func validateSemVer(sv semVer, badList []semVer) error {
	for _, badVer := range badList {
		if sv.String() == badVer.String() {
			return FaultIpmctlBadVersion(sv.String())
		}
	}

	return nil
}

func (rce *runCmdError) Error() string {
	if ee, ok := rce.wrapped.(*exec.ExitError); ok {
		return fmt.Sprintf("%s: stdout: %s; stderr: %s", ee.ProcessState,
			rce.stdout, ee.Stderr)
	}

	return fmt.Sprintf("%s: stdout: %s", rce.wrapped.Error(), rce.stdout)
}

func run(cmd string) (string, error) {
	out, err := exec.Command("bash", "-c", cmd).Output()
	if err != nil {
		return "", &runCmdError{
			wrapped: err,
			stdout:  string(out),
		}
	}

	return string(out), nil
}

// constants for ipmctl commandline calls
//
// Manage AppDirect/Interleaved memory allocation goals across all DCPMMs on a system.
const (
	cmdShowIpmctlVersion = "ipmctl version"
	cmdShowRegions       = "ipmctl show -d PersistentMemoryType,FreeCapacity -region"
	cmdCreateRegions     = "ipmctl create -f -goal PersistentMemoryType=AppDirect"
	cmdRemoveRegions     = "ipmctl create -f -goal MemoryMode=100"
	outScmNoRegions      = "no Regions defined"
)

// constants for ndctl commandline calls
const (
	cmdCreateNamespace  = "ndctl create-namespace"  // returns json ns info
	cmdListNamespaces   = "ndctl list -N -v"        // returns json ns info
	cmdDisableNamespace = "ndctl disable-namespace" // expect device name param
	cmdDestroyNamespace = "ndctl destroy-namespace" // expect device name param
)

type cmdRunner struct {
	log       logging.Logger
	binding   ipmctl.IpmCtl
	runCmd    runCmdFn
	lookPath  lookPathFn
	checkOnce sync.Once
}

// checkIpmctl verifies ipmctl application version is acceptable.
func (cr *cmdRunner) checkIpmctl(badList []semVer) (errOut error) {
	cr.checkOnce.Do(func() {
		cmdOut, err := cr.runCmd(cmdShowIpmctlVersion)
		if err != nil {
			errOut = errors.WithMessage(err, "show version cmd")
			return
		}

		re := regexp.MustCompile(`(\d{2}).(\d{2}).(\d{2}).(\d{4})`)
		matched := re.FindStringSubmatch(cmdOut)

		if matched == nil {
			errOut = errors.Errorf("could not read ipmctl version (%s)", cmdOut)
			return
		}

		ipmctlBinVer := matched[1:]
		cr.log.Debugf("ipmctl binary semver: %v", ipmctlBinVer)

		errOut = validateSemVer(ipmctlBinVer, badList)
	})

	return
}

func (cr *cmdRunner) showRegions() (string, error) {
	if err := cr.checkIpmctl(badIpmctlVers); err != nil {
		return "", errors.WithMessage(err, "checkIpmctl")
	}

	out, err := cr.runCmd(cmdShowRegions)
	if err != nil {
		return "", errors.Wrapf(err, "cmd %q", cmdShowRegions)
	}
	cr.log.Debugf("%q cmd returned: %q", cmdShowRegions, out)

	return out, nil
}

func (cr *cmdRunner) createRegions() error {
	if err := cr.checkIpmctl(badIpmctlVers); err != nil {
		return errors.WithMessage(err, "checkIpmctl")
	}

	cr.log.Debug("set interleaved appdirect goal to create regions")

	out, err := cr.runCmd(cmdCreateRegions)
	if err != nil {
		return errors.Wrapf(err, "cmd %q", cmdCreateRegions)
	}
	cr.log.Debugf("%q cmd returned: %q", cmdCreateRegions, out)

	return nil
}

func (cr *cmdRunner) removeRegions() error {
	if err := cr.checkIpmctl(badIpmctlVers); err != nil {
		return errors.WithMessage(err, "checkIpmctl")
	}

	cr.log.Debug("set memory mode goal to remove regions")

	out, err := cr.runCmd(cmdRemoveRegions)
	if err != nil {
		return errors.Wrapf(err, "cmd %q", cmdRemoveRegions)
	}
	cr.log.Debugf("%q cmd returned: %q", cmdRemoveRegions, out)

	return nil
}

func (cr *cmdRunner) deleteGoals() error {
	cr.log.Debug("delete any existing resource allocation goals")
	return errors.Wrap(cr.binding.DeleteConfigGoals(cr.log), "failed to delete config goals")
}

func (cr *cmdRunner) createNamespace(regionIdx int, sizeBytes uint64) (string, error) {
	cmd := cmdCreateNamespace
	if regionIdx > createNsAnyRegion {
		cmd = fmt.Sprintf("%s --region %d", cmd, regionIdx)
	}
	if sizeBytes > 0 {
		cmd = fmt.Sprintf("%s --size %d", cmd, sizeBytes)
	}

	return cr.runCmd(cmd)
}

func (cr *cmdRunner) listNamespaces() (string, error) {
	return cr.runCmd(cmdListNamespaces)
}

func (cr *cmdRunner) disableNamespace(name string) (string, error) {
	return cr.runCmd(fmt.Sprintf("%s %s", cmdDisableNamespace, name))
}

func (cr *cmdRunner) destroyNamespace(name string) (string, error) {
	return cr.runCmd(fmt.Sprintf("%s %s", cmdDestroyNamespace, name))
}

// checkNdctl verifies ndctl application is installed.
func (cr *cmdRunner) checkNdctl() (errOut error) {
	cr.checkOnce.Do(func() {
		if _, err := cr.lookPath("ndctl"); err != nil {
			errOut = FaultMissingNdctl
		}
	})

	return
}

// getModules scans the storage host for PMem modules and returns a slice of them.
func (cr *cmdRunner) getModules() (storage.ScmModules, error) {
	discovery, err := cr.binding.GetModules(cr.log)
	if err != nil {
		return nil, errors.Wrap(err, "failed to discover pmem modules")
	}
	cr.log.Debugf("discovered %d pmem modules", len(discovery))

	modules := make(storage.ScmModules, 0, len(discovery))
	for _, d := range discovery {
		modules = append(modules, &storage.ScmModule{
			ChannelID:        uint32(d.Channel_id),
			ChannelPosition:  uint32(d.Channel_pos),
			ControllerID:     uint32(d.Memory_controller_id),
			SocketID:         uint32(d.Socket_id),
			PhysicalID:       uint32(d.Physical_id),
			Capacity:         d.Capacity,
			UID:              d.Uid.String(),
			PartNumber:       d.Part_number.String(),
			FirmwareRevision: d.Fw_revision.String(),
		})
	}

	return modules, nil
}

func scmFirmwareUpdateStatusFromIpmctl(ipmctlStatus uint32) storage.ScmFirmwareUpdateStatus {
	switch ipmctlStatus {
	case ipmctl.FWUpdateStatusFailed:
		return storage.ScmUpdateStatusFailed
	case ipmctl.FWUpdateStatusSuccess:
		return storage.ScmUpdateStatusSuccess
	case ipmctl.FWUpdateStatusStaged:
		return storage.ScmUpdateStatusStaged
	}
	return storage.ScmUpdateStatusUnknown
}

func uidStringToIpmctl(uidStr string) (ipmctl.DeviceUID, error) {
	var uid ipmctl.DeviceUID
	n := copy(uid[:], uidStr)
	if n == 0 {
		return ipmctl.DeviceUID{}, errors.New("invalid SCM module UID")
	}
	return uid, nil
}

// noFirmwareVersion is the version string reported if there is no firmware version
const noFirmwareVersion = "00.00.00.0000"

// GetFirmwareStatus gets the current firmware status for a specific device.
func (cr *cmdRunner) GetFirmwareStatus(deviceUID string) (*storage.ScmFirmwareInfo, error) {
	uid, err := uidStringToIpmctl(deviceUID)
	if err != nil {
		return nil, errors.New("invalid SCM module UID")
	}
	info, err := cr.binding.GetFirmwareInfo(uid)
	if err != nil {
		return nil, errors.Wrapf(err, "failed to get firmware info for device %q", deviceUID)
	}

	// Avoid displaying the staged version string if there is no staged version
	stagedVersion := info.StagedFWVersion.String()
	if stagedVersion == noFirmwareVersion {
		stagedVersion = ""
	}

	return &storage.ScmFirmwareInfo{
		ActiveVersion:     info.ActiveFWVersion.String(),
		StagedVersion:     stagedVersion,
		ImageMaxSizeBytes: info.FWImageMaxSize * 4096,
		UpdateStatus:      scmFirmwareUpdateStatusFromIpmctl(info.FWUpdateStatus),
	}, nil
}

// UpdateFirmware attempts to update the firmware on the given device with the binary at
// the path provided.
func (cr *cmdRunner) UpdateFirmware(deviceUID string, firmwarePath string) error {
	uid, err := uidStringToIpmctl(deviceUID)
	if err != nil {
		return errors.New("invalid SCM module UID")
	}
	// Force option permits minor version downgrade.
	err = cr.binding.UpdateFirmware(uid, firmwarePath, true)
	if err != nil {
		return errors.Wrapf(err, "failed to update firmware for device %q", deviceUID)
	}
	return nil
}

// parseShowRegionOutput takes output from ipmctl and returns free capacity.
//
// external tool commands return:
// $ ipmctl show -d PersistentMemoryType,FreeCapacity -region
//
// ---ISetID=0x2aba7f4828ef2ccc---
//    PersistentMemoryType=AppDirect
//    FreeCapacity=3012.0 GiB
// ---ISetID=0x81187f4881f02ccc---
//    PersistentMemoryType=AppDirect
//    FreeCapacity=3012.0 GiB
//
// FIXME DAOS-10173: When libipmctl nvm_get_region() is fixed so that it doesn't take
//                   a minute to return, replace this with getRegionState() below to use
//                   libipmctl directly through bindings.
func parseShowRegionOutput(log logging.Logger, text string) (regionFreeBytes []uint64, err error) {
	defer func() {
		err = errors.Wrap(err, "parse show regions cmd output")
	}()

	lines := strings.Split(strings.TrimSpace(text), "\n")
	if len(lines) < 3 {
		return nil, errors.Errorf("expecting at least 3 lines, got %d", len(lines))
	}

	var appDirect bool
	for _, line := range lines {
		entry := strings.TrimSpace(line)

		kv := strings.Split(entry, "=")
		if len(kv) != 2 {
			continue
		}

		switch kv[0] {
		case "PersistentMemoryType":
			if kv[1] == "AppDirectNotInterleaved" {
				return nil, storage.FaultScmNotInterleaved
			}
			if kv[1] == "AppDirect" {
				appDirect = true
				continue
			}
		case "FreeCapacity":
			if !appDirect {
				continue
			}
			fc, err := humanize.ParseBytes(kv[1])
			if err != nil {
				return nil, err
			}
			regionFreeBytes = append(regionFreeBytes, fc)
		}
		appDirect = false
	}

	return regionFreeBytes, nil
}

func (cr *cmdRunner) getRegionFreeSpace() ([]uint64, error) {
	out, err := cr.showRegions()
	if err != nil {
		return nil, errors.WithMessage(err, "show regions cmd")
	}

	cr.log.Debugf("show region output: %s\n", out)

	if strings.Contains(out, outScmNoRegions) {
		return nil, nil
	}

	regionFreeBytes, err := parseShowRegionOutput(cr.log, out)
	if err != nil {
		return nil, errors.Wrap(err, "parse show region cmd output")
	}
	if len(regionFreeBytes) == 0 {
		return nil, errors.New("no AppDirect PMem regions parsed")
	}

	return regionFreeBytes, nil
}

func (cr *cmdRunner) getRegionState() (storage.ScmState, error) {
	regionFreeBytes, err := cr.getRegionFreeSpace()
	if err != nil {
		if err == storage.FaultScmNotInterleaved {
			return storage.ScmStateNotInterleaved, nil // state handled in callen
		}
		return storage.ScmStateUnknown, err
	}

	if len(regionFreeBytes) == 0 {
		return storage.ScmStateNoRegions, nil
	}

	for _, freeBytes := range regionFreeBytes {
		if freeBytes > 0 {
			cr.log.Debugf("PMem regions have %v free bytes", regionFreeBytes)
			return storage.ScmStateFreeCapacity, nil
		}
	}

	cr.log.Debug("PMem regions have no free capacity")
	return storage.ScmStateNoFreeCapacity, nil
}

// getRegionStateFromBindings establishes state of PMem by fetching region detail from ipmctl bindings and checking
// memory mode types and free capacity.
func (cr *cmdRunner) getRegionStateFromBindings() (storage.ScmState, error) {
	regions, err := cr.binding.GetRegions(cr.log)
	if err != nil {
		return storage.ScmStateUnknown, errors.Wrap(err, "failed to discover PMem regions")
	}
	cr.log.Debugf("discovered pmem regions: %+v", regions)

	if len(regions) == 0 {
		return storage.ScmStateNoRegions, nil
	}

	var totalFree uint64
	for _, region := range regions {
		health := ipmctl.PMemRegionHealth(region.Health)
		if health != ipmctl.RegionHealthNormal && health != ipmctl.RegionHealthPending {
			cr.log.Errorf("unexpected PMem region health %q, want %q",
				health, ipmctl.RegionHealthNormal)
		}

		regionType := ipmctl.PMemRegionType(region.Type)
		switch regionType {
		case ipmctl.RegionTypeNotInterleaved:
			return storage.ScmStateNotInterleaved, nil
		case ipmctl.RegionTypeAppDirect:
		default:
			return storage.ScmStateUnknown, errors.Errorf(
				"unexpected PMem region type %q, want %q", regionType,
				ipmctl.RegionTypeAppDirect)
		}

		if region.Free_capacity > 0 {
			totalFree += region.Free_capacity
		}
	}

	if totalFree > 0 {
		cr.log.Debugf("PMem regions have %s free capacity", humanize.Bytes(totalFree))
		return storage.ScmStateFreeCapacity, nil
	}
	cr.log.Debug("PMem regions have no free capacity")
	return storage.ScmStateNoFreeCapacity, nil
}

// prep executes commands to configure PMem modules into AppDirect interleaved
// regions (sets) hosting pmem block-device namespaces.
//
// State is established based on presence and free capacity of regions.
//
// Actions based on state:
// * no modules exist -> return state
// * modules exist and no regions -> create all regions (needs reboot)
// * regions exist but regions not in interleaved mode -> return state
// * regions exist and free capacity -> create all namespaces, return created
// * regions exist but no free capacity -> no-op, return namespaces
func (cr *cmdRunner) prep(req storage.ScmPrepareRequest, scanRes *storage.ScmScanResponse) (resp *storage.ScmPrepareResponse, err error) {
	state := scanRes.State
	resp = &storage.ScmPrepareResponse{State: state}

	cr.log.Debugf("scm backend prep: state %q", state)

	if err = cr.deleteGoals(); err != nil {
		return nil, err
	}

	// Handle unspecified NrNamespacesPerSocket in request.
	if req.NrNamespacesPerSocket == 0 {
		req.NrNamespacesPerSocket = minNrNssPerSocket
	}

	switch state {
	case storage.ScmStateNotInterleaved:
		// Non-interleaved AppDirect memory mode is unsupported.
		err = storage.FaultScmNotInterleaved
	case storage.ScmStateNoRegions:
		// No regions exist, create interleaved AppDirect PMem regions.
		if err = cr.createRegions(); err != nil {
			break
		}
		resp.RebootRequired = true
	case storage.ScmStateFreeCapacity:
		// Regions exist but no namespaces, create block devices on PMem regions.
		resp.Namespaces, err = cr.createNamespaces(req.NrNamespacesPerSocket)
		if err != nil {
			break
		}
		resp.State = storage.ScmStateNoFreeCapacity
	case storage.ScmStateNoFreeCapacity:
		// Regions and namespaces exist so sanity check number of namespaces matches the
		// number requested before returning details.
		var regionFreeBytes []uint64
		regionFreeBytes, err = cr.getRegionFreeSpace()
		if err != nil {
			break
		}

		nrRegions := uint(len(regionFreeBytes))
		nrNamespaces := uint(len(scanRes.Namespaces))

		// Assume 1:1 mapping between region and NUMA node.
		if (nrRegions * req.NrNamespacesPerSocket) != nrNamespaces {
			err = storage.FaultScmNamespacesNrMismatch(req.NrNamespacesPerSocket,
				nrRegions, nrNamespaces)
			break
		}
		resp.Namespaces = scanRes.Namespaces
	default:
		err = errors.Errorf("unhandled scm state %q", state)
	}

	if err != nil {
		return nil, err
	}
	return resp, nil
}

// prepReset executes commands to remove namespaces and regions on PMem modules.
//
// Returns indication of whether a reboot is required alongside error.
// Command output from external tools will be returned. State will be passed in.
func (cr *cmdRunner) prepReset(scanRes *storage.ScmScanResponse) (*storage.ScmPrepareResponse, error) {
	state := scanRes.State
	resp := &storage.ScmPrepareResponse{State: state}

	cr.log.Debugf("scm backend prep reset: state %q", state)

	if err := cr.deleteGoals(); err != nil {
		return nil, err
	}

	switch state {
	case storage.ScmStateNoRegions:
		return resp, nil
	case storage.ScmStateFreeCapacity, storage.ScmStateNoFreeCapacity, storage.ScmStateNotInterleaved:
		// Continue to remove namespaces and regions.
		resp.RebootRequired = true
	default:
		return nil, errors.Errorf("unhandled scm state %q", state)
	}

	for _, dev := range scanRes.Namespaces {
		if err := cr.removeNamespace(dev.Name); err != nil {
			return nil, err
		}
	}

	cr.log.Infof("Resetting PMem memory allocations.")

	if err := cr.removeRegions(); err != nil {
		return nil, err
	}

	return resp, nil
}

func (cr *cmdRunner) removeNamespace(devName string) error {
	if err := cr.checkNdctl(); err != nil {
		return err
	}

	cr.log.Debugf("removing pmem namespace %q", devName)

	if _, err := cr.disableNamespace(devName); err != nil {
		return errors.WithMessagef(err, "disable namespace cmd (%s)", devName)
	}

	if _, err := cr.destroyNamespace(devName); err != nil {
		return errors.WithMessagef(err, "destroy namespace cmd (%s)", devName)
	}

	return nil
}

func (cr *cmdRunner) createSingleNsPerSocket() (storage.ScmNamespaces, error) {
	for {
		cr.log.Debug("creating pmem namespace")

		if _, err := cr.createNamespace(createNsAnyRegion, 0); err != nil {
			return nil, errors.WithMessage(err, "create namespace cmd")
		}

		state, err := cr.getRegionState()
		if err != nil {
			return nil, errors.WithMessage(err, "getting state")
		}

		if state == storage.ScmStateNoFreeCapacity {
			break
		}
		if state == storage.ScmStateFreeCapacity {
			continue
		}

		return nil, errors.Errorf("unexpected state: want %s, got %s",
			storage.ScmStateFreeCapacity.String(), state.String())
	}

	return cr.getNamespaces()
}

func (cr *cmdRunner) createMultiNsPerSocket(nrNsPerSocket uint) (storage.ScmNamespaces, error) {
	// For each region, create two namespaces each with half the total free capacity.
	// Sanity check alignment.

	if nrNsPerSocket < minNrNssPerSocket || nrNsPerSocket > maxNrNssPerSocket {
		return nil, errors.Errorf("unexpected number of namespaces per socket: want [%d-%d], got %d",
			minNrNssPerSocket, maxNrNssPerSocket, nrNsPerSocket)
	}

	regionFreeBytes, err := cr.getRegionFreeSpace()
	if err != nil {
		return nil, err
	}

	for regionIndex, freeBytes := range regionFreeBytes {
		if freeBytes == 0 {
			// Catch edge case where trying to create multiple namespaces per socket
			// but a region already has a single namespace taking full capacity.
			cr.log.Errorf("createMultiNsPerSocket: region %d has no free space", regionIndex)

			nss, err := cr.getNamespaces()
			if err != nil {
				return nil, err
			}

			return nil, storage.FaultScmNamespacesNrMismatch(nrNsPerSocket,
				uint(len(regionFreeBytes)), uint(len(nss)))
		}

		// Check value is 2MiB aligned and (TODO) multiples of interleave width.
		pmemBytes := freeBytes / uint64(nrNsPerSocket)

		if pmemBytes%alignmentBoundaryBytes != 0 {
			return nil, errors.New("free region size is not 2MiB aligned")
		}

		// Create specified number of namespaces on a single region (NUMA node).
		for j := uint(0); j < nrNsPerSocket; j++ {
			if _, err := cr.createNamespace(regionIndex, pmemBytes); err != nil {
				return nil, errors.WithMessage(err, "create namespace cmd")
			}
		}
	}

	state, err := cr.getRegionState()
	if err != nil {
		return nil, errors.WithMessage(err, "getting state")
	}
	if state != storage.ScmStateNoFreeCapacity {
		return nil, errors.Errorf("unexpected state: want %s, got %s",
			storage.ScmStateNoFreeCapacity.String(), state.String())
	}

	return cr.getNamespaces()
}

// createNamespaces repeatedly creates namespaces until no free capacity.
func (cr *cmdRunner) createNamespaces(nrNssPerSocket uint) (storage.ScmNamespaces, error) {
	if err := cr.checkNdctl(); err != nil {
		return nil, err
	}

	switch nrNssPerSocket {
	case 0:
		return nil, errors.New("number of namespaces per socket can not be 0")
	case 1:
		cr.log.Debug("creating one pmem namespace per socket")
		return cr.createSingleNsPerSocket()
	default:
		cr.log.Debug("creating multiple pmem namespaces per socket")
		return cr.createMultiNsPerSocket(nrNssPerSocket)
	}
}

// getNamespaces calls ndctl to list pmem namespaces and returns converted
// native storage types.
func (cr *cmdRunner) getNamespaces() (storage.ScmNamespaces, error) {
	if err := cr.checkNdctl(); err != nil {
		return nil, err
	}

	out, err := cr.listNamespaces()
	if err != nil {
		return nil, errors.WithMessage(err, "list namespaces cmd")
	}

	nss, err := parseNamespaces(out)
	if err != nil {
		return nil, err
	}

	return nss, nil
}

func parseNamespaces(jsonData string) (storage.ScmNamespaces, error) {
	nss := storage.ScmNamespaces{}

	// turn single entries into arrays
	if !strings.HasPrefix(jsonData, "[") {
		jsonData = "[" + jsonData + "]"
	}

	if err := json.Unmarshal([]byte(jsonData), &nss); err != nil {
		return nil, err
	}

	return nss, nil
}

func defaultCmdRunner(log logging.Logger) *cmdRunner {
	cr, err := newCmdRunner(log, &ipmctl.NvmMgmt{}, run, exec.LookPath)
	if err != nil {
		panic(err)
	}

	return cr
}

func newCmdRunner(log logging.Logger, lib ipmctl.IpmCtl, runCmd runCmdFn, lookPath lookPathFn) (*cmdRunner, error) {
	if lib == nil {
		lib = &ipmctl.NvmMgmt{}
	}
	if err := lib.Init(log); err != nil {
		return nil, err
	}

	return &cmdRunner{
		log:      log,
		binding:  lib,
		runCmd:   runCmd,
		lookPath: lookPath,
	}, nil
}
