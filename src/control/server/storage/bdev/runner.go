//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package bdev

import (
	"fmt"
	"os"
	"os/exec"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/provider/system"
	"github.com/daos-stack/daos/src/control/server/storage"
)

const (
	spdkSetupPath      = "../share/daos/control/setup_spdk.sh"
	defaultNrHugepages = 1024 // default number applied by SPDK
	nrHugepagesEnv     = "_NRHUGE"
	hugeNodeEnv        = "_HUGENODE"
	targetUserEnv      = "_TARGET_USER"
	pciAllowListEnv    = "_PCI_ALLOWED"
	pciBlockListEnv    = "_PCI_BLOCKED"
	driverOverrideEnv  = "_DRIVER_OVERRIDE"
	vfioDisabledDriver = "uio_pci_generic"
	noDriver           = "none"
)

type runCmdFn func(logging.Logger, []string, string, ...string) (string, error)

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

	cmd := exec.Command(cmdPath, args...)
	cmd.Env = env
	out, err := cmd.CombinedOutput()
	if err != nil {
		log.Errorf("run script %q failed, env: %v", cmdPath, env)
		return "", &system.RunCmdError{
			Wrapped: err,
			Stdout:  string(out),
		}
	}
	log.Debugf("run script %q, env: %v, out:\n%s\n", cmdPath, env, out)

	return string(out), nil
}

type spdkSetupScript struct {
	log        logging.Logger
	scriptPath string
	env        map[string]string
	runCmd     runCmdFn
}

func defaultScriptRunner(log logging.Logger) *spdkSetupScript {
	return &spdkSetupScript{
		log:        log,
		scriptPath: spdkSetupPath,
		env:        make(map[string]string),
		runCmd:     run,
	}
}

func (s *spdkSetupScript) run(args ...string) error {
	envStrs := make([]string, 0, len(s.env))
	for k, v := range s.env {
		if k == "" || v == "" {
			continue
		}
		envStrs = append(envStrs, fmt.Sprintf("%s=%s", k, v))
	}
	out, err := s.runCmd(s.log, envStrs, s.scriptPath, args...)

	return errors.Wrapf(err, "spdk setup failed (%s)", out)
}

// Prepare executes setup script to allocate hugepages and rebind PCI devices (that don't have
// active mountpoints) from generic kernel driver to be used with SPDK. Either all PCI devices will
// be unbound by default if allow list parameter is not set, otherwise PCI devices can be specified
// by passing in an allow list of PCI addresses.
//
// NOTE: will make the controller disappear from /dev until reset() called.
func (s *spdkSetupScript) Prepare(req *storage.BdevPrepareRequest) error {
	// Always use min number of hugepages otherwise devices cannot be accessed.
	nrHugepages := req.HugepageCount
	if nrHugepages <= 0 {
		nrHugepages = defaultNrHugepages
	}

	s.env = map[string]string{
		"PATH":          os.Getenv("PATH"),
		pciBlockListEnv: req.PCIBlockList,
		targetUserEnv:   req.TargetUser,
		pciAllowListEnv: req.PCIAllowList,
		nrHugepagesEnv:  fmt.Sprintf("%d", nrHugepages),
		hugeNodeEnv:     req.HugeNodes,
	}

	if req.DisableVFIO {
		s.env[driverOverrideEnv] = vfioDisabledDriver
	}

	return s.run()
}

// Unbind executes setup script with DRIVERRIDE=none remove all driver bindings.
//
// Apply block list to cater for situation where some devices should be excluded from unbind, e.g.
// for use from the OS.
func (s *spdkSetupScript) Unbind(req *storage.BdevPrepareRequest) error {
	s.env = map[string]string{
		"PATH":            os.Getenv("PATH"),
		pciBlockListEnv:   req.PCIBlockList,
		driverOverrideEnv: noDriver,
	}

	return errors.Wrap(s.run(), "unbind devices")
}

// Reset executes setup script to reset hugepage allocations and rebind PCI devices (that don't have
// active mountpoints) from SPDK compatible driver e.g. VFIO and bind back to the kernel bdev driver
// to be used by the OS. Either all PCI devices will be unbound by default if allow list parameter
// is not set, otherwise PCI devices can be specified by passing in a allow list of PCI addresses.
//
// NOTE: will make the controller reappear in /dev.
func (s *spdkSetupScript) Reset(req *storage.BdevPrepareRequest) error {
	s.env = map[string]string{
		"PATH":          os.Getenv("PATH"),
		pciAllowListEnv: req.PCIAllowList,
		pciBlockListEnv: req.PCIBlockList,
	}

	return errors.Wrap(s.run("reset"), "reset")
}
