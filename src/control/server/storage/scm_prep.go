//
// (C) Copyright 2019 Intel Corporation.
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

package storage

import (
	"encoding/json"
	"fmt"
	"os/exec"
	"strings"

	"github.com/pkg/errors"

	types "github.com/daos-stack/daos/src/control/common/storage"
	"github.com/daos-stack/daos/src/control/logging"
)

const (
	cmdScmShowRegions = "ipmctl show -d PersistentMemoryType,FreeCapacity -region"
	outScmNoRegions   = "\nThere are no Regions defined in the system.\n"
	// creates a AppDirect/Interleaved memory allocation goal across all DCPMMs on a system.
	cmdScmCreateRegions    = "ipmctl create -f -goal PersistentMemoryType=AppDirect"
	cmdScmRemoveRegions    = "ipmctl create -f -goal MemoryMode=100"
	cmdScmDeleteGoal       = "ipmctl delete -goal"
	cmdScmCreateNamespace  = "ndctl create-namespace" // returns json ns info
	cmdScmListNamespaces   = "ndctl list -N"          // returns json ns info
	cmdScmDisableNamespace = "ndctl disable-namespace %s"
	cmdScmDestroyNamespace = "ndctl destroy-namespace %s"
)

type PmemDev struct {
	UUID     string
	Blockdev string
	Dev      string
	NumaNode int `json:"numa_node"`
}

func (pd *PmemDev) String() string {
	return fmt.Sprintf("%s, numa %d", pd.Blockdev, pd.NumaNode)
}

type runCmdFn func(string) (string, error)

type runCmdError struct {
	wrapped error
	stdout  string
}

func (rce *runCmdError) Error() string {
	if ee, ok := rce.wrapped.(*exec.ExitError); ok {
		return fmt.Sprintf("%s: stdout: %s; stderr: %s", ee.ProcessState,
			rce.stdout, ee.Stderr)
	}

	return fmt.Sprintf("%s: stdout: %s", rce.wrapped.Error(), rce.stdout)
}

// run wraps exec.Command().Output() to enable mocking of command output.
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

// PrepScm interface provides capability to prepare SCM storage
type PrepScm interface {
	Prep(types.ScmState) (bool, []PmemDev, error)
	PrepReset(types.ScmState) (bool, error)
	GetState() (types.ScmState, error)
	GetNamespaces() ([]PmemDev, error)
}

type prepScm struct {
	log    logging.Logger
	runCmd runCmdFn
}

func newPrepScm(log logging.Logger, myRun runCmdFn) PrepScm {
	return &prepScm{runCmd: myRun, log: log}
}

// getState establishes state of SCM regions and namespaces on local server.
func (ps *prepScm) GetState() (types.ScmState, error) {
	// TODO: discovery should provide SCM region details
	out, err := ps.runCmd(cmdScmShowRegions)
	if err != nil {
		return types.ScmStateUnknown, err
	}

	ps.log.Debugf("show region output: %s\n", out)

	if out == outScmNoRegions {
		return types.ScmStateNoRegions, nil
	}

	ok, err := hasFreeCapacity(out)
	if err != nil {
		return types.ScmStateUnknown, err
	}
	if ok {
		return types.ScmStateFreeCapacity, nil
	}

	return types.ScmStateNoCapacity, nil
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
func (ps *prepScm) Prep(state types.ScmState) (needsReboot bool, pmemDevs []PmemDev,
	err error) {

	ps.log.Debugf("scm in state %s\n", state)

	switch state {
	case types.ScmStateNoRegions:
		// clear any pre-existing goals first
		if _, err = ps.runCmd(cmdScmDeleteGoal); err != nil {
			err = errors.WithMessage(err, "clear goal")
			return
		}
		// if successful, memory allocation change read on reboot
		if _, err = ps.runCmd(cmdScmCreateRegions); err == nil {
			needsReboot = true
		}
	case types.ScmStateFreeCapacity:
		pmemDevs, err = ps.createNamespaces()
	case types.ScmStateNoCapacity:
		pmemDevs, err = ps.GetNamespaces()
	case types.ScmStateUnknown:
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
func (ps *prepScm) PrepReset(state types.ScmState) (bool, error) {
	ps.log.Debugf("scm in state %s\n", state)

	switch state {
	case types.ScmStateNoRegions:
		ps.log.Info("SCM is already reset\n")
		return false, nil
	case types.ScmStateFreeCapacity, types.ScmStateNoCapacity:
	case types.ScmStateUnknown:
		return false, errors.New("unknown scm state")
	default:
		return false, errors.Errorf("unhandled scm state %q", state)
	}

	pmemDevs, err := ps.GetNamespaces()
	if err != nil {
		return false, err
	}

	for _, dev := range pmemDevs {
		if err := ps.removeNamespace(dev.Dev); err != nil {
			return false, err
		}
	}

	ps.log.Infof("resetting SCM memory allocations\n")
	// clear any pre-existing goals first
	if _, err := ps.runCmd(cmdScmDeleteGoal); err != nil {
		return false, err
	}
	if out, err := ps.runCmd(cmdScmRemoveRegions); err != nil {
		ps.log.Error(out)
		return false, err
	}

	return true, nil // memory allocation reset requires a reboot
}

func (ps *prepScm) removeNamespace(devName string) (err error) {
	ps.log.Infof("removing SCM namespace, may take a few minutes...\n")

	_, err = ps.runCmd(fmt.Sprintf(cmdScmDisableNamespace, devName))
	if err != nil {
		return
	}

	_, err = ps.runCmd(fmt.Sprintf(cmdScmDestroyNamespace, devName))
	if err != nil {
		return
	}

	return
}

// hasFreeCapacity takes output from ipmctl and checks for free capacity.
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
func hasFreeCapacity(text string) (hasCapacity bool, err error) {
	lines := strings.Split(text, "\n")
	if len(lines) < 4 {
		return false, errors.Errorf("expecting at least 4 lines, got %d",
			len(lines))
	}

	for _, line := range lines {
		entry := strings.TrimSpace(line)

		kv := strings.Split(entry, "=")
		if len(kv) != 2 {
			continue
		}

		if kv[0] == "PersistentMemoryType" && kv[1] == "AppDirect" {
			hasCapacity = true
			continue
		}

		if kv[0] != "FreeCapacity" {
			continue
		}

		if hasCapacity && kv[1] != "0.0 GiB" {
			return
		}

		hasCapacity = false
	}

	return
}

func parsePmemDevs(jsonData string) (devs []PmemDev, err error) {
	// turn single entries into arrays
	if !strings.HasPrefix(jsonData, "[") {
		jsonData = "[" + jsonData + "]"
	}

	err = json.Unmarshal([]byte(jsonData), &devs)

	return
}

// createNamespaces runs create until no free capacity.
func (ps *prepScm) createNamespaces() ([]PmemDev, error) {
	devs := make([]PmemDev, 0)

	for {
		ps.log.Infof("creating SCM namespace, may take a few minutes...\n")

		out, err := ps.runCmd(cmdScmCreateNamespace)
		if err != nil {
			return nil, errors.WithMessage(err, "create namespace cmd")
		}

		newDevs, err := parsePmemDevs(out)
		if err != nil {
			return nil, errors.WithMessage(err, "parsing pmem devs")
		}
		devs = append(devs, newDevs...)

		state, err := ps.GetState()
		if err != nil {
			return nil, errors.WithMessage(err, "getting state")
		}

		switch state {
		case types.ScmStateNoCapacity:
			return devs, nil
		case types.ScmStateFreeCapacity:
		default:
			return nil, errors.Errorf("unexpected state: want %s, got %s",
				types.ScmStateFreeCapacity.String(), state.String())
		}
	}
}

func (ps prepScm) GetNamespaces() (devs []PmemDev, err error) {
	out, err := ps.runCmd(cmdScmListNamespaces)
	if err != nil {
		return nil, err
	}

	return parsePmemDevs(out)
}
