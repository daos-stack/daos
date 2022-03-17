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

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/ipmctl"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
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
	// command for showing regions only used for debug output
	cmdShowRegions   = "ipmctl show -d PersistentMemoryType,FreeCapacity -region"
	cmdCreateRegions = "ipmctl create -f -goal PersistentMemoryType=AppDirect"
	cmdRemoveRegions = "ipmctl create -f -goal MemoryMode=100"
	cmdDeleteGoal    = "ipmctl delete -goal"
)

// constants for ndctl commandline calls
const (
	cmdCreateNamespace  = "ndctl create-namespace"  // returns json ns info
	cmdListNamespaces   = "ndctl list -N -v"        // returns json ns info
	cmdDisableNamespace = "ndctl disable-namespace" // expect device name param
	cmdDestroyNamespace = "ndctl destroy-namespace" // expect device name param
)

type cmdRunner struct {
	log      logging.Logger
	binding  ipmctl.IpmCtl
	runCmd   runCmdFn
	lookPath lookPathFn
}

// checkIpmctl verifies ipmctl application version is acceptable.
func (cr *cmdRunner) checkIpmctl(badList []semVer) error {
	cmdOut, err := cr.runCmd(cmdShowIpmctlVersion)
	if err != nil {
		return errors.WithMessage(err, "show version cmd")
	}

	re := regexp.MustCompile(`(\d{2}).(\d{2}).(\d{2}).(\d{4})`)
	matched := re.FindStringSubmatch(cmdOut)

	if matched == nil {
		return errors.Errorf("could not read ipmctl version (%s)", cmdOut)
	}

	ipmctlBinVer := matched[1:]
	cr.log.Debugf("ipmctl binary semver: %v", ipmctlBinVer)

	if err := validateSemVer(ipmctlBinVer, badList); err != nil {
		return err
	}

	return nil
}

func (cr *cmdRunner) showRegions() {
	if err := cr.checkIpmctl(badIpmctlVers); err != nil {
		cr.log.Error(errors.WithMessage(err, "checkIpmctl").Error())
		return
	}

	// Print ipmctl commandline output for show regions for debug purposes.
	out, err := cr.runCmd(cmdShowRegions)
	if err != nil {
		cr.log.Error(errors.WithMessage(err, "show regions cmd").Error())
		return
	}

	cr.log.Debugf("show region output: %s", out)
}

func (cr *cmdRunner) createRegions() error {
	if err := cr.checkIpmctl(badIpmctlVers); err != nil {
		return errors.WithMessage(err, "checkIpmctl")
	}

	if err := cr.deleteGoal(); err != nil {
		return err
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

	if err := cr.deleteGoal(); err != nil {
		return err
	}

	cr.log.Debug("set memory mode goal to remove regions")

	out, err := cr.runCmd(cmdRemoveRegions)
	if err != nil {
		return errors.Wrapf(err, "cmd %q", cmdRemoveRegions)
	}
	cr.log.Debugf("%q cmd returned: %q", cmdRemoveRegions, out)

	return nil
}

func (cr *cmdRunner) deleteGoal() error {
	cr.log.Debug("delete any previous resource allocation goals")

	out, err := cr.runCmd(cmdDeleteGoal)
	if err != nil {
		return errors.Wrapf(err, "cmd %q", cmdDeleteGoal)
	}
	cr.log.Debugf("%q cmd returned: %q", cmdDeleteGoal, out)

	return nil
}

func (cr *cmdRunner) createNamespace() (string, error) {
	return cr.runCmd(cmdCreateNamespace)
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
func (cr *cmdRunner) checkNdctl() error {
	_, err := cr.lookPath("ndctl")
	if err != nil {
		return FaultMissingNdctl
	}

	return nil
}

// getModules scans the storage host for PMem modules and returns a slice of them.
func (cr *cmdRunner) getModules() (storage.ScmModules, error) {
	discovery, err := cr.binding.GetModules()
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

// getState establishes state of PMem by fetching region detail from ipmctl bindings and checking
// memory mode types and free capacity.
func (cr *cmdRunner) getState() (storage.ScmState, error) {
	regions, err := cr.binding.GetRegions(cr.log)
	if err != nil {
		cr.showRegions()
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
			cr.showRegions()
			cr.log.Errorf("unexpected PMem region health %q, want %q",
				health, ipmctl.RegionHealthNormal)
		}

		regionType := ipmctl.PMemRegionType(region.Type)
		switch regionType {
		case ipmctl.RegionTypeNotInterleaved:
			cr.showRegions()
			return storage.ScmStateNotInterleaved, nil
		case ipmctl.RegionTypeAppDirect:
		default:
			cr.showRegions()
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
func (cr *cmdRunner) prep(scanRes *storage.ScmScanResponse) (resp *storage.ScmPrepareResponse, err error) {
	state := scanRes.State
	resp = &storage.ScmPrepareResponse{State: state}

	cr.log.Debugf("scm backend prep: state %q", state)

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
		resp.Namespaces, err = cr.createNamespaces()
		if err != nil {
			break
		}
		resp.State = storage.ScmStateNoFreeCapacity
	case storage.ScmStateNoFreeCapacity:
		// Regions and namespaces extant, return namespaces.
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

// createNamespaces repeatedly creates namespaces until no free capacity.
func (cr *cmdRunner) createNamespaces() (storage.ScmNamespaces, error) {
	if err := cr.checkNdctl(); err != nil {
		return nil, err
	}

	devs := make(storage.ScmNamespaces, 0)

	for {
		cr.log.Debug("creating pmem namespace")

		out, err := cr.createNamespace()
		if err != nil {
			return nil, errors.WithMessage(err, "create namespace cmd")
		}

		newDevs, err := parseNamespaces(out)
		if err != nil {
			return nil, errors.WithMessage(err, "parsing pmem devs")
		}
		devs = append(devs, newDevs...)

		state, err := cr.getState()
		if err != nil {
			return nil, errors.WithMessage(err, "getting state")
		}

		switch state {
		case storage.ScmStateNoFreeCapacity:
			return devs, nil
		case storage.ScmStateFreeCapacity:
		default:
			return nil, errors.Errorf("unexpected state: want %s, got %s",
				storage.ScmStateFreeCapacity.String(), state.String())
		}
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
	return newCmdRunner(log, &ipmctl.NvmMgmt{}, run, exec.LookPath)
}

func newCmdRunner(log logging.Logger, lib ipmctl.IpmCtl, runCmd runCmdFn, lookPath lookPathFn) *cmdRunner {
	return &cmdRunner{
		log:      log,
		binding:  lib,
		runCmd:   runCmd,
		lookPath: lookPath,
	}
}
