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
	cmdShowRegions       = "ipmctl show -d PersistentMemoryType,FreeCapacity -region"
	cmdCreateRegions     = "ipmctl create -f -goal PersistentMemoryType=AppDirect"
	cmdRemoveRegions     = "ipmctl create -f -goal MemoryMode=100"
	cmdDeleteGoal        = "ipmctl delete -goal"
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
	log      logging.Logger
	binding  ipmctl.IpmCtl
	runCmd   runCmdFn
	lookPath lookPathFn
}

func (cr *cmdRunner) showIpmctlVersion() (string, error) {
	return cr.runCmd(cmdShowIpmctlVersion)
}

func (cr *cmdRunner) showRegions() (string, error) {
	return cr.runCmd(cmdShowRegions)
}

func (cr *cmdRunner) createRegions() (string, error) {
	return cr.runCmd(cmdCreateRegions)
}

func (cr *cmdRunner) removeRegions() (string, error) {
	return cr.runCmd(cmdRemoveRegions)
}

func (cr *cmdRunner) deleteGoal() (string, error) {
	return cr.runCmd(cmdDeleteGoal)
}

// checkIpmctl verifies ipmctl application version is acceptable.
func (cr *cmdRunner) checkIpmctl(badList []semVer) error {
	cmdOut, err := cr.showIpmctlVersion()
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

// Discover scans the system for SCM modules and returns a list of them.
func (cr *cmdRunner) Discover() (storage.ScmModules, error) {
	discovery, err := cr.binding.Discover()
	if err != nil {
		return nil, errors.Wrap(err, "failed to discover SCM modules")
	}
	cr.log.Debugf("discovered %d DCPM modules", len(discovery))

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

// getState establishes state of SCM regions and namespaces on local server.
func (cr *cmdRunner) GetPmemState() (storage.ScmState, error) {
	if err := cr.checkNdctl(); err != nil {
		return storage.ScmStateUnknown, err
	}

	// TODO: discovery should provide SCM region details
	out, err := cr.showRegions()
	if err != nil {
		return storage.ScmStateUnknown, errors.WithMessage(err, "show regions cmd")
	}

	cr.log.Debugf("show region output: %s\n", out)

	if strings.Contains(out, outScmNoRegions) {
		return storage.ScmStateNoRegions, nil
	}

	bytes, err := freeCapacity(out)
	if err != nil {
		return storage.ScmStateUnknown,
			errors.WithMessage(err, "checking scm region capacity")
	}
	if bytes > 0 {
		return storage.ScmStateFreeCapacity, nil
	}

	return storage.ScmStateNoCapacity, nil
}

// Prep executes commands to configure SCM modules into AppDirect interleaved
// regions/sets hosting pmem device file namespaces.
//
// Presents of nonvolatile memory modules is assumed in this method and state
// is established based on presence and free capacity of regions.
//
// Actions based on state:
// * modules exist and no regions -> create all regions (needs reboot)
// * regions exist and free capacity -> create all namespaces, return created
// * regions exist but no free capacity -> no-op, return namespaces
//
// Command output from external tools will be returned. State will be passed in.
func (cr *cmdRunner) Prep(state storage.ScmState) (needsReboot bool, pmemDevs storage.ScmNamespaces, err error) {
	if err = cr.checkNdctl(); err != nil {
		return
	}
	if err = cr.checkIpmctl(badIpmctlVers); err != nil {
		return
	}

	cr.log.Debugf("scm in state %s\n", state)

	switch state {
	case storage.ScmStateNoRegions:
		// clear any pre-existing goals first
		if _, err = cr.deleteGoal(); err != nil {
			err = errors.WithMessage(err, "delete goal cmd")
			return
		}
		if _, err = cr.createRegions(); err != nil {
			err = errors.WithMessage(err, "create regions cmd")
			return
		}
		// if successful, memory allocation change read on reboot
		needsReboot = true
	case storage.ScmStateFreeCapacity:
		pmemDevs, err = cr.createNamespaces()
	case storage.ScmStateNoCapacity:
		pmemDevs, err = cr.GetPmemNamespaces()
	case storage.ScmStateUnknown:
		err = errors.New("unknown scm state")
	default:
		err = errors.Errorf("unhandled scm state %q", state)
	}

	return
}

// PrepReset executes commands to remove namespaces and regions on SCM modules.
//
// Returns indication of whether a reboot is required alongside error.
// Command output from external tools will be returned. State will be passed in.
func (cr *cmdRunner) PrepReset(state storage.ScmState) (bool, error) {
	if err := cr.checkNdctl(); err != nil {
		return false, nil
	}

	cr.log.Debugf("scm in state %s\n", state)

	switch state {
	case storage.ScmStateNoRegions:
		cr.log.Info("SCM is already reset\n")
		return false, nil
	case storage.ScmStateFreeCapacity, storage.ScmStateNoCapacity:
	case storage.ScmStateUnknown:
		return false, errors.New("unknown scm state")
	default:
		return false, errors.Errorf("unhandled scm state %q", state)
	}

	namespaces, err := cr.GetPmemNamespaces()
	if err != nil {
		return false, err
	}

	for _, dev := range namespaces {
		if err := cr.removeNamespace(dev.Name); err != nil {
			return false, err
		}
	}

	cr.log.Infof("resetting SCM memory allocations\n")
	// clear any pre-existing goals first
	if _, err := cr.deleteGoal(); err != nil {
		return false, errors.WithMessage(err, "delete goal cmd")
	}
	if out, err := cr.removeRegions(); err != nil {
		cr.log.Error(out)
		return false, errors.WithMessage(err, "remove regions cmd")
	}

	return true, nil // memory allocation reset requires a reboot
}

func (cr *cmdRunner) removeNamespace(devName string) error {
	cr.log.Infof("removing SCM namespace %q, may take a few minutes...\n", devName)

	if _, err := cr.disableNamespace(devName); err != nil {
		return errors.WithMessagef(err, "disable namespace cmd (%s)", devName)
	}

	if _, err := cr.destroyNamespace(devName); err != nil {
		return errors.WithMessagef(err, "destroy namespace cmd (%s)", devName)
	}

	return nil
}

// freeCapacity takes output from ipmctl and returns free capacity.
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
// FIXME: implementation to be replaced by using libipmctl directly through bindings
func freeCapacity(text string) (uint64, error) {
	lines := strings.Split(text, "\n")
	if len(lines) < 4 {
		return 0, errors.Errorf("expecting at least 4 lines, got %d",
			len(lines))
	}

	var appDirect bool
	var capacity uint64
	for _, line := range lines {
		entry := strings.TrimSpace(line)

		kv := strings.Split(entry, "=")
		if len(kv) != 2 {
			continue
		}

		switch kv[0] {
		case "PersistentMemoryType":
			if kv[1] == "AppDirect" {
				appDirect = true
				continue
			}
		case "FreeCapacity":
			if !appDirect {
				continue
			}
			c, err := humanize.ParseBytes(kv[1])
			if err != nil {
				return 0, err
			}
			capacity += c
		}
		appDirect = false
	}

	return capacity, nil
}

// createNamespaces repeatedly creates namespaces until no free capacity.
func (cr *cmdRunner) createNamespaces() (storage.ScmNamespaces, error) {
	devs := make(storage.ScmNamespaces, 0)

	for {
		cr.log.Infof("creating SCM namespace, may take a few minutes...\n")

		out, err := cr.createNamespace()
		if err != nil {
			return nil, errors.WithMessage(err, "create namespace cmd")
		}

		newDevs, err := parseNamespaces(out)
		if err != nil {
			return nil, errors.WithMessage(err, "parsing pmem devs")
		}
		devs = append(devs, newDevs...)

		state, err := cr.GetPmemState()
		if err != nil {
			return nil, errors.WithMessage(err, "getting state")
		}

		switch state {
		case storage.ScmStateNoCapacity:
			return devs, nil
		case storage.ScmStateFreeCapacity:
		default:
			return nil, errors.Errorf("unexpected state: want %s, got %s",
				storage.ScmStateFreeCapacity.String(), state.String())
		}
	}
}

// GetPmemNamespaces calls ndctl to list pmem namespaces and returns converted
// native storage types.
func (cr *cmdRunner) GetPmemNamespaces() (storage.ScmNamespaces, error) {
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

	cr.log.Debugf("discovered %d DCPM namespaces", len(nss))
	return nss, nil
}

func parseNamespaces(jsonData string) (nss storage.ScmNamespaces, err error) {
	// turn single entries into arrays
	if !strings.HasPrefix(jsonData, "[") {
		jsonData = "[" + jsonData + "]"
	}

	err = json.Unmarshal([]byte(jsonData), &nss)

	return
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
