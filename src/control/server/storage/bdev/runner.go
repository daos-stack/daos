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
	"fmt"
	"os"
	"os/exec"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
)

const (
	spdkSetupPath      = "../share/daos/control/setup_spdk.sh"
	defaultNrHugepages = 4096
	nrHugepagesEnv     = "_NRHUGE"
	targetUserEnv      = "_TARGET_USER"
	pciWhiteListEnv    = "_PCI_WHITELIST"
	pciBlackListEnv    = "_PCI_BLACKLIST"
	driverOverrideEnv  = "_DRIVER_OVERRIDE"
	vfioDisabledDriver = "uio_pci_generic"
)

type runCmdFn func(logging.Logger, []string, string, ...string) (string, error)

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

func run(log logging.Logger, env []string, cmdStr string, args ...string) (string, error) {
	if os.Geteuid() != 0 {
		return "", errors.New("must be run with root privileges")
	}

	var cmdPath string
	var err error
	if cmdPath, err = exec.LookPath(cmdStr); err != nil {
		cmdPath, err = common.GetAdjacentPath(cmdStr)
		if err != nil {
			return "", errors.Wrapf(err, "unable to resolve path to %s", cmdStr)
		}
		if _, err := os.Stat(cmdPath); err != nil {
			return "", err
		}
	}

	log.Debugf("running script: %s", cmdPath)
	cmd := exec.Command(cmdPath, args...)
	cmd.Env = env
	out, err := cmd.CombinedOutput()
	if err != nil {
		return "", &runCmdError{
			wrapped: err,
			stdout:  string(out),
		}
	}

	return string(out), nil
}

type spdkSetupScript struct {
	log        logging.Logger
	scriptPath string
	runCmd     runCmdFn
}

func defaultScriptRunner(log logging.Logger) *spdkSetupScript {
	return &spdkSetupScript{
		log:        log,
		scriptPath: spdkSetupPath,
		runCmd:     run,
	}
}

// Reset executes setup script to deallocate hugepages & return PCI devices
// to previous driver bindings.
//
// NOTE: will make the controller reappear in /dev.
func (s *spdkSetupScript) Reset() error {
	out, err := s.runCmd(s.log, nil, s.scriptPath, "reset")
	return errors.Wrapf(err, "spdk reset failed (%s)", out)
}

// Prepare executes setup script to allocate hugepages and unbind PCI devices
// (that don't have active mountpoints) from generic kernel driver to be
// used with SPDK. Either all PCI devices will be unbound by default if wlist
// parameter is not set, otherwise PCI devices can be specified by passing in a
// whitelist of PCI addresses.
//
// NOTE: will make the controller disappear from /dev until reset() called.
func (s *spdkSetupScript) Prepare(req PrepareRequest) error {
	nrHugepages := req.HugePageCount
	if nrHugepages <= 0 {
		nrHugepages = defaultNrHugepages
	}

	env := []string{
		fmt.Sprintf("PATH=%s", os.Getenv("PATH")),
		fmt.Sprintf("%s=%d", nrHugepagesEnv, nrHugepages),
		fmt.Sprintf("%s=%s", targetUserEnv, req.TargetUser),
	}

	if req.PCIWhitelist != "" && req.PCIBlacklist != "" {
		return errors.New("bdev_include and bdev_exclude can't be used together\n")
	}

	if req.PCIWhitelist != "" {
		env = append(env, fmt.Sprintf("%s=%s", pciWhiteListEnv, req.PCIWhitelist))
	}
	if req.PCIBlacklist != "" {
		env = append(env, fmt.Sprintf("%s=%s", pciBlackListEnv, req.PCIBlacklist))
	}
	if req.DisableVFIO {
		env = append(env, fmt.Sprintf("%s=%s", driverOverrideEnv, vfioDisabledDriver))
	}

	s.log.Debugf("spdk setup env: %v", env)
	out, err := s.runCmd(s.log, env, s.scriptPath)
	s.log.Debugf("spdk setup stdout:\n%s\n", out)
	return errors.Wrapf(err, "spdk setup failed (%s)", out)
}
