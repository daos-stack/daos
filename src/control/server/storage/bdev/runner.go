//
// (C) Copyright 2019-2022 Intel Corporation.
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
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
	devTypeEnv         = "_DEV_TYPE"
	devTypeVMD         = "VMD"
	devTypeNVMe        = "NVME"
)

// devType returns the SPDK setup.sh DEV_TYPE value to restrict device binding to either VMD or
// NVMe devices, as appropriate for the (already VMD-detection-resolved) request. Mandating one of
// these two types prevents setup.sh from touching unrelated SPDK-supported device classes (e.g.
// IOAT, DSA, IAA, VIRTIO, AE4DMA) that DAOS does not manage.
func devType(req *storage.BdevPrepareRequest) string {
	if req.EnableVMD {
		return devTypeVMD
	}

	return devTypeNVMe
}

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

// validateDevType enforces that DEV_TYPE is always explicitly set to either VMD or NVME before
// invoking the SPDK setup script. Without this, any future caller/method that forgets to set
// DEV_TYPE in the env map would silently fall back to SPDK's default of selecting every
// SPDK-supported device class (IOAT, AE4DMA, DSA, IAA, VIRTIO, VMD, NVME), not just the ones
// DAOS manages.
func (s *spdkSetupScript) validateDevType() error {
	switch s.env[devTypeEnv] {
	case devTypeVMD, devTypeNVMe:
		return nil
	default:
		return errors.Errorf("invalid or unset %s %q, expected %q or %q", devTypeEnv,
			s.env[devTypeEnv], devTypeVMD, devTypeNVMe)
	}
}

func (s *spdkSetupScript) run(args ...string) error {
	if err := s.validateDevType(); err != nil {
		return errors.Wrap(err, "refusing to run spdk setup script")
	}

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
		devTypeEnv:      devType(req),
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
		pciAllowListEnv:   req.PCIAllowList,
		pciBlockListEnv:   req.PCIBlockList,
		driverOverrideEnv: noDriver,
		devTypeEnv:        devType(req),
	}

	return errors.Wrap(s.run(), "unbind devices")
}

// Reset executes setup script to reset hugepage allocations and rebind PCI devices (that don't have
// active mountpoints) from SPDK compatible driver e.g. VFIO and bind back to the kernel bdev driver
// to be used by the OS. Either all PCI devices will be unbound by default if allow list parameter
// is not set, otherwise PCI devices can be specified by passing in a allow list of PCI addresses.
//
// NOTE: will make the controller reappear in /dev.
// TODO DAOS-18606: Should allowlist not be sent so all non-blocked devices get bound back to kernel?
func (s *spdkSetupScript) Reset(req *storage.BdevPrepareRequest) error {
	s.env = map[string]string{
		"PATH":          os.Getenv("PATH"),
		pciAllowListEnv: req.PCIAllowList,
		pciBlockListEnv: req.PCIBlockList,
		devTypeEnv:      devType(req),
	}

	return errors.Wrap(s.run("reset"), "reset")
}
