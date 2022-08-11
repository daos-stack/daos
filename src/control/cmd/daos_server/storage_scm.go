//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/server"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/storage"
)

const MsgStoragePrepareWarn = "Memory allocation goals for PMem will be changed and namespaces " +
	"modified, this may be a destructive operation. Please ensure namespaces are unmounted " +
	"and locally attached PMem modules are not in use. Please be patient as it may take " +
	"several minutes and subsequent reboot maybe required.\n"

type scmPrepareResetFn func(storage.ScmPrepareRequest) (*storage.ScmPrepareResponse, error)

type pmemCmd struct {
	Create createNamespacesCmd `command:"create-namespaces" description:"Configure PMem mode setting and create namespace block devices to be used by DAOS"`
	Remove removeNamespacesCmd `command:"remove-namespaces" description:"Remove PMem namespace block devices and reset mode setting"`
	Scan   scanPMemCmd         `command:"scan" description:"Scan PMem modules and namespaces"`
}

type createNamespacesCmd struct {
	cmdutil.LogCmd `json:"-"`
	helperLogCmd   `json:"-"`

	NrNamespacesPerSocket uint `short:"S" long:"scm-ns-per-socket" description:"Number of PMem namespaces to create per socket" default:"1"`
	Force                 bool `short:"f" long:"force" description:"Perform PMem prepare operation without waiting for confirmation"`
}

func (cmd *createNamespacesCmd) preparePMem(backendCall scmPrepareResetFn) error {
	if cmd.NrNamespacesPerSocket == 0 {
		return errors.New("(-S|--pmem-ns-per-socket) should be set to at least 1")
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

	// Prepare PMem modules to be presented as pmem device files.
	resp, err := backendCall(req)
	if err != nil {
		return err
	}

	cmd.Debugf("scm prepare response: %+v", resp)

	if resp == nil {
		return errors.New("scm prepare returned nil response")
	}
	state := resp.Socket.State

	if resp.RebootRequired {
		// The only valid state that requires a reboot after PMem prepare is NoRegions.
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
		return errors.Errorf("unexpected state %q after pmem create-namespaces", state)
	}

	return nil
}

func (cmd *createNamespacesCmd) Execute(args []string) error {
	if err := cmd.setHelperLogFile(); err != nil {
		return err
	}

	scs := server.NewStorageControlService(cmd.Logger, config.DefaultServer().Engines)

	cmd.Debugf("executing create namespaces command: %+v", cmd)
	return cmd.preparePMem(scs.ScmPrepare)
}

type removeNamespacesCmd struct {
	cmdutil.LogCmd `json:"-"`
	helperLogCmd   `json:"-"`

	Force bool `short:"f" long:"force" description:"Perform PMem prepare operation without waiting for confirmation"`
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

	// Prepare PMem modules to be presented as pmem device files.
	resp, err := backendCall(req)
	if err != nil {
		return err
	}

	cmd.Debugf("scm prepare response: %+v", resp)

	state := resp.Socket.State

	if resp.RebootRequired {
		// Address valid state that requires a reboot after PMem reset.
		msg := ""
		switch state {
		case storage.ScmNotInterleaved:
			msg = "Non-interleaved regions will be removed on reboot. "
		case storage.ScmFreeCap:
			msg = "Interleaved regions will be removed on reboot. "
		case storage.ScmNoFreeCap, storage.ScmPartFreeCap:
			msg = "Namespaces have been removed and regions will be removed on reboot. "
		case storage.ScmNotHealthy, storage.ScmUnknownMode:
			msg = "Namespaces have been removed and regions (some with an unexpected state) will be removed on reboot. "
		default:
			return errors.Errorf("unexpected state if reboot is required (%s)", state)
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
		return errors.Errorf("unexpected state %q after pmem remove-namespaces", state)
	}

	return nil
}

func (cmd *removeNamespacesCmd) Execute(args []string) error {
	if err := cmd.setHelperLogFile(); err != nil {
		return err
	}

	scs := server.NewStorageControlService(cmd.Logger, config.DefaultServer().Engines)

	cmd.Debugf("executing remove namespaces command: %+v", cmd)
	return cmd.resetPMem(scs.ScmPrepare)
}

type scanPMemCmd struct {
	cmdutil.LogCmd `json:"-"`
	helperLogCmd   `json:"-"`
}

func (cmd *scanPMemCmd) Execute(args []string) error {
	var bld strings.Builder

	if err := cmd.setHelperLogFile(); err != nil {
		return err
	}

	svc := server.NewStorageControlService(cmd.Logger, config.DefaultServer().Engines)

	cmd.Info("Scanning locally-attached PMem storage...\n")

	pmemResp, err := svc.ScmScan(storage.ScmScanRequest{})
	if err != nil {
		return err
	}

	if len(pmemResp.Namespaces) > 0 {
		if err := pretty.PrintScmNamespaces(pmemResp.Namespaces, &bld); err != nil {
			return err
		}
	} else {
		if err := pretty.PrintScmModules(pmemResp.Modules, &bld); err != nil {
			return err
		}
	}

	cmd.Info(bld.String())

	return nil
}
