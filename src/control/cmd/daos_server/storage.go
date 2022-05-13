//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"os"
	"os/user"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/cmdutil"
	commands "github.com/daos-stack/daos/src/control/common/storage"
	"github.com/daos-stack/daos/src/control/lib/hardware/sysfs"
	"github.com/daos-stack/daos/src/control/pbin"
	"github.com/daos-stack/daos/src/control/server"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/storage"
)

type storageCmd struct {
	Prepare storagePrepareCmd `command:"prepare" description:"Prepare SCM and NVMe storage attached to remote servers."`
	Scan    storageScanCmd    `command:"scan" description:"Scan SCM and NVMe storage attached to local server"`
}

type storagePrepareCmd struct {
	scs *server.StorageControlService
	cmdutil.LogCmd
	commands.StoragePrepareCmd
	HelperLogFile string `short:"l" long:"helper-log-file" description:"Log debug from daos_admin binary."`
}

type errs []error

func (es *errs) add(err error) error {
	if es == nil {
		return errors.New("nil errs")
	}
	*es = append(*es, err)

	return nil
}

type nvmePrepFn func(storage.BdevPrepareRequest) (*storage.BdevPrepareResponse, error)

func (cmd *storagePrepareCmd) prepNvme(prep nvmePrepFn, iommuEnabled bool) error {
	if doPrep, _, err := cmd.Validate(); err != nil || !doPrep {
		return err
	}

	if cmd.TargetUser == "" {
		runningUser, err := user.Current()
		if err != nil {
			return errors.Wrap(err, "couldn't lookup running user")
		}
		cmd.TargetUser = runningUser.Username
	}

	if cmd.TargetUser != "root" {
		if cmd.DisableVFIO {
			return errors.New("VFIO can not be disabled if running as non-root user")
		}
		if !iommuEnabled {
			return errors.New("no IOMMU detected, to discover NVMe devices enable " +
				"IOMMU per the DAOS Admin Guide")
		}
	}

	op := "Prepare"
	if cmd.Reset {
		op = "Reset"
	}

	cmd.Info(op + " locally-attached NVMe storage...")

	req := storage.BdevPrepareRequest{
		HugePageCount: cmd.NrHugepages,
		TargetUser:    cmd.TargetUser,
		PCIAllowList:  cmd.PCIAllowList,
		PCIBlockList:  cmd.PCIBlockList,
		Reset_:        cmd.Reset,
		DisableVFIO:   cmd.DisableVFIO,
		EnableVMD:     !cmd.DisableVFIO && iommuEnabled, // vmd will be prepared if available
	}

	cmd.Debugf("request parameters: %+v", req)

	// Prepare NVMe access through SPDK
	_, err := prep(req)

	return err
}

type scmPrepFn func(storage.ScmPrepareRequest) (*storage.ScmPrepareResponse, error)

func (cmd *storagePrepareCmd) prepScm(scanErrors *errs, prep scmPrepFn) error {
	if _, doPrep, err := cmd.Validate(); err != nil || !doPrep {
		return err
	}

	if cmd.NrNamespacesPerSocket == 0 {
		return errors.New("(-S|--scm-ns-per-socket) should be set to at least 1")
	}

	op := "Prepare"
	if cmd.Reset {
		op = "Reset"
	}

	cmd.Info(op + " locally-attached SCM...")

	if err := cmd.Warn(cmd.Logger); err != nil {
		return scanErrors.add(err)
	}

	// Prepare SCM modules to be presented as pmem device files.
	resp, err := prep(storage.ScmPrepareRequest{
		Reset:                 cmd.Reset,
		NrNamespacesPerSocket: cmd.NrNamespacesPerSocket,
	})
	if err != nil {
		return scanErrors.add(err)
	}
	cmd.Debugf("%s scm resp: %+v", op, resp)

	state := resp.State

	// PMem resource allocations have been updated, prompt the user for reboot.
	// State reported indicates state before reboot.
	if resp.RebootRequired {
		msg := ""
		switch state {
		case storage.ScmStateNoRegions:
			msg = "PMem AppDirect interleaved regions will be created. "
		case storage.ScmStateNotInterleaved:
			msg = "PMem AppDirect non-interleaved regions will be removed. "
		case storage.ScmStateFreeCapacity:
			msg = "PMem AppDirect interleaved regions will be removed. "
		case storage.ScmStateNoFreeCapacity:
			msg = "PMem namespaces removed and AppDirect interleaved regions will be removed. "
		}
		cmd.Info(msg + storage.ScmMsgRebootRequired)

		return nil
	}

	if state == storage.ScmStateUnknown {
		return scanErrors.add(errors.New("failed to report state"))
	}
	if state == storage.ScmStateNoModules {
		return scanErrors.add(storage.FaultScmNoModules)
	}

	if cmd.Reset {
		// Respond to resultant state on prepare reset.
		if state == storage.ScmStateNoRegions {
			cmd.Info("SCM reset successfully!")
			return nil
		}

		return scanErrors.add(errors.Errorf("unexpected state %q after prepare reset", state))
	}

	// Respond to state reported by prepare setup.
	switch state {
	case storage.ScmStateNoRegions:
		scanErrors.add(errors.New("failed to create regions"))
	case storage.ScmStateFreeCapacity:
		scanErrors.add(errors.New("failed to create namespaces"))
	case storage.ScmStateNoFreeCapacity:
		if len(resp.Namespaces) == 0 {
			scanErrors.add(errors.New("failed to find namespaces"))
			break
		}
		// Namespaces exist.
		var bld strings.Builder
		if err := pretty.PrintScmNamespaces(resp.Namespaces, &bld); err != nil {
			return err
		}
		cmd.Infof("%s\n", bld.String())
	default:
		scanErrors.add(errors.Errorf("unexpected state %q after prepare reset", state))
	}

	return nil
}

func (cmd *storagePrepareCmd) Execute(args []string) error {
	if cmd.HelperLogFile != "" {
		if err := os.Setenv(pbin.DaosAdminLogFileEnvVar, cmd.HelperLogFile); err != nil {
			cmd.Errorf("unable to configure privileged helper logging: %s", err)
		}
	}

	// This is a little ugly, but allows for easier unit testing.
	// FIXME: With the benefit of hindsight, it seems apparent
	// that we should have made these Execute() methods thin
	// wrappers around more easily-testable functions.
	if cmd.scs == nil {
		cmd.scs = server.NewStorageControlService(cmd.Logger, config.DefaultServer().Engines)
	}

	scanErrors := make(errs, 0, 2)

	iommuEnabled, err := sysfs.NewProvider(cmd).IsIOMMUEnabled()
	if err != nil {
		return err
	}

	if err := cmd.prepNvme(cmd.scs.NvmePrepare, iommuEnabled); err != nil {
		scanErrors.add(err)
	}

	if err := cmd.prepScm(&scanErrors, cmd.scs.ScmPrepare); err != nil {
		return err
	}

	switch len(scanErrors) {
	case 0:
		return nil
	case 1:
		return scanErrors[0]
	default:
		// When calling ConcatErrors the Fault resolution is lost.
		return common.ConcatErrors(scanErrors, nil)
	}
}

type storageScanCmd struct {
	cmdutil.LogCmd
	HelperLogFile string `short:"l" long:"helper-log-file" description:"Log debug from daos_admin binary."`
	DisableVMD    bool   `short:"d" long:"disable-vmd" description:"Disable VMD-aware scan."`
}

func (cmd *storageScanCmd) Execute(args []string) error {
	if cmd.HelperLogFile != "" {
		if err := os.Setenv(pbin.DaosAdminLogFileEnvVar, cmd.HelperLogFile); err != nil {
			cmd.Errorf("unable to configure privileged helper logging: %s", err)
		}
	}

	svc := server.NewStorageControlService(cmd.Logger, config.DefaultServer().Engines)
	if !cmd.DisableVMD {
		svc.WithVMDEnabled()
	}

	msg := "Scanning locally-attached storage..."

	cmd.Info(msg)

	var bld strings.Builder
	scanErrors := make([]error, 0, 2)

	nvmeResp, err := svc.NvmeScan(storage.BdevScanRequest{})
	if err != nil {
		scanErrors = append(scanErrors, err)
	} else {
		_, err := fmt.Fprintf(&bld, "\n")
		if err != nil {
			return err
		}
		if err := pretty.PrintNvmeControllers(nvmeResp.Controllers, &bld); err != nil {
			return err
		}
	}

	scmResp, err := svc.ScmScan(storage.ScmScanRequest{})
	switch {
	case err != nil:
		scanErrors = append(scanErrors, err)
	case len(scmResp.Namespaces) > 0:
		_, err := fmt.Fprintf(&bld, "\n")
		if err != nil {
			return err
		}
		if err := pretty.PrintScmNamespaces(scmResp.Namespaces, &bld); err != nil {
			return err
		}
	default:
		_, err := fmt.Fprintf(&bld, "\n")
		if err != nil {
			return err
		}
		if err := pretty.PrintScmModules(scmResp.Modules, &bld); err != nil {
			return err
		}
	}

	cmd.Info(bld.String())

	if len(scanErrors) > 0 {
		errStr := "scan error(s):\n"
		for _, err := range scanErrors {
			errStr += fmt.Sprintf("  %s\n", err.Error())
		}
		return errors.New(errStr)
	}

	return nil
}
