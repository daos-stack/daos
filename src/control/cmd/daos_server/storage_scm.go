//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"os"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/pbin"
	"github.com/daos-stack/daos/src/control/server"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/storage"
)

const MsgStoragePrepareWarn = "Memory allocation goals for PMem will be changed and namespaces " +
	"modified, this may be a destructive operation. Please ensure namespaces are unmounted " +
	"and locally attached PMem modules are not in use. Please be patient as it may take " +
	"several minutes and subsequent reboot maybe required.\n"

type scmPrepareResetFn func(storage.ScmPrepareRequest) (*storage.ScmPrepareResponse, error)

type scmCmd struct {
	Create createNamespacesCmd `command:"create-namespaces" description:"Configure PMem mode setting and create namespace block devices to be used by DAOS"`
	Remove removeNamespacesCmd `command:"remove-namespaces" description:"Remove PMem namespace block devices and reset mode setting"`
	Scan   scanPMemCmd         `command:"scan" description:"Scan PMem modules and namespaces"`
}

type createNamespacesCmd struct {
	cmdutil.LogCmd
	NrNamespacesPerSocket uint   `short:"S" long:"scm-ns-per-socket" description:"Number of SCM namespaces to create per socket" default:"1"`
	Force                 bool   `short:"f" long:"force" description:"Perform SCM prepare operation without waiting for confirmation"`
	HelperLogFile         string `short:"l" long:"helper-log-file" description:"Log file location for debug from daos_admin binary"`
}

func (cmd *createNamespacesCmd) preparePMem(backendCall scmPrepareResetFn) error {
	if cmd.NrNamespacesPerSocket == 0 {
		return errors.New("(-S|--scm-ns-per-socket) should be set to at least 1")
	}

	cmd.Info("Create locally-attached PMem namespaces...")

	cmd.Info(MsgStoragePrepareWarn)
	if !cmd.Force && !common.GetConsent(cmd) {
		return errors.New("consent not given")
	}

	req := storage.ScmPrepareRequest{
		NrNamespacesPerSocket: cmd.NrNamespacesPerSocket,
	}
	cmd.Debugf("scm prepare request parameters: %+v", req)

	// Prepare SCM modules to be presented as pmem device files.
	resp, err := backendCall(req)
	if err != nil {
		return err
	}

	cmd.Debugf("scm prepare response: %+v", resp)

	state := resp.Socket.State

	if resp.RebootRequired {
		// The only valid state that requires a reboot after SCM prepare is NoRegions.
		if state != storage.ScmNoRegions {
			return errors.Errorf("expected %s state if reboot required, got %s",
				storage.ScmNoRegions, state)
		}

		// If PMem resource allocations have been updated, prompt the user for reboot.
		cmd.Info("PMem AppDirect interleaved regions will be created on reboot. " +
			storage.ScmMsgRebootRequired)

		return nil
	}

	// Respond to state reported by prepare setup.
	switch state {
	case storage.ScmStateUnknown:
		return errors.New("failed to report state")
	case storage.ScmNoModules:
		return storage.FaultScmNoModules
	case storage.ScmNoRegions:
		return errors.New("failed to create regions")
	case storage.ScmFreeCap:
		return errors.New("failed to create namespaces")
	case storage.ScmNoFreeCap:
		if len(resp.Namespaces) == 0 {
			return errors.New("failed to find namespaces")
		}
		// Namespaces exist so print details.
		var bld strings.Builder
		if err := pretty.PrintScmNamespaces(resp.Namespaces, &bld); err != nil {
			return err
		}
		cmd.Infof("%s\n", bld.String())
	default:
		return errors.Errorf("unexpected state %q after scm create-namespaces", state)
	}

	return nil
}

func (cmd *createNamespacesCmd) Execute(args []string) error {
	if cmd.HelperLogFile != "" {
		if err := os.Setenv(pbin.DaosAdminLogFileEnvVar, cmd.HelperLogFile); err != nil {
			cmd.Errorf("unable to configure privileged helper logging: %s", err)
		}
	}

	scs := server.NewStorageControlService(cmd, config.DefaultServer().Engines)

	return cmd.preparePMem(scs.ScmPrepare)
}

type removeNamespacesCmd struct {
	cmdutil.LogCmd
	Force         bool   `short:"f" long:"force" description:"Perform SCM prepare operation without waiting for confirmation"`
	HelperLogFile string `short:"l" long:"helper-log-file" description:"Log file location for debug from daos_admin binary"`
}

func (cmd *removeNamespacesCmd) resetPMem(backendCall scmPrepareResetFn) error {
	cmd.Info("Remove locally-attached PMem namespaces...")

	cmd.Info(MsgStoragePrepareWarn)
	if !cmd.Force && !common.GetConsent(cmd) {
		return errors.New("consent not given")
	}

	req := storage.ScmPrepareRequest{
		Reset: true,
	}
	cmd.Debugf("scm prepare request parameters: %+v", req)

	// Prepare SCM modules to be presented as pmem device files.
	resp, err := backendCall(req)
	if err != nil {
		return err
	}

	cmd.Debugf("scm prepare response: %+v", resp)

	state := resp.Socket.State

	if resp.RebootRequired {
		// Address valid state that requires a reboot after SCM reset.
		msg := ""
		switch state {
		case storage.ScmNotInterleaved:
			msg = "PMem AppDirect non-interleaved regions will be removed on reboot. "
		case storage.ScmFreeCap:
			msg = "PMem AppDirect interleaved regions will be removed on reboot. "
		case storage.ScmNoFreeCap, storage.ScmPartFreeCap:
			msg = "PMem namespaces have been removed and AppDirect interleaved regions will be removed on reboot. "
		case storage.ScmNotHealthy, storage.ScmUnknownMode:
			msg = "PMem namespaces have been removed and regions (some with an unexpected state) will be removed on reboot. "
		default:
			return errors.Errorf("unexpected state %q after scm remove-namespaces", state)
		}
		cmd.Info(msg + storage.ScmMsgRebootRequired)

		return nil
	}

	switch state {
	case storage.ScmNoRegions:
		cmd.Info("PMem has been reset successfully!")
	case storage.ScmNoModules:
		return storage.FaultScmNoModules
	default:
		return errors.Errorf("unexpected state %q after scm remove-namespaces", state)
	}

	return nil
}

func (cmd *removeNamespacesCmd) Execute(args []string) error {
	if cmd.HelperLogFile != "" {
		if err := os.Setenv(pbin.DaosAdminLogFileEnvVar, cmd.HelperLogFile); err != nil {
			cmd.Errorf("unable to configure privileged helper logging: %s", err)
		}
	}

	scs := server.NewStorageControlService(cmd, config.DefaultServer().Engines)

	return cmd.resetPMem(scs.ScmPrepare)
}

type scanPMemCmd struct {
	cmdutil.LogCmd
	HelperLogFile string `short:"l" long:"helper-log-file" description:"Log debug from daos_admin binary."`
}

func (cmd *scanPMemCmd) Execute(args []string) error {
	var bld strings.Builder

	if cmd.HelperLogFile != "" {
		if err := os.Setenv(pbin.DaosAdminLogFileEnvVar, cmd.HelperLogFile); err != nil {
			cmd.Errorf("unable to configure privileged helper logging: %s", err)
		}
	}

	svc := server.NewStorageControlService(cmd.Logger, config.DefaultServer().Engines)

	cmd.Info("Scanning locally-attached PMem storage...\n")

	scmResp, err := svc.ScmScan(storage.ScmScanRequest{})
	if err != nil {
		return err
	}

	if len(scmResp.Namespaces) > 0 {
		if err := pretty.PrintScmNamespaces(scmResp.Namespaces, &bld); err != nil {
			return err
		}
	} else {
		if err := pretty.PrintScmModules(scmResp.Modules, &bld); err != nil {
			return err
		}
	}

	cmd.Info(bld.String())

	return nil
}
