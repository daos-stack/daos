//
// (C) Copyright 2022-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/server/storage"
)

const MsgStoragePrepareWarn = "Memory allocation goals for PMem will be changed and namespaces " +
	"modified, this may be a destructive operation. Please ensure namespaces are unmounted " +
	"and locally attached PMem modules are not in use. Please be patient as it may take " +
	"several minutes and subsequent reboot maybe required.\n"

var (
	errNoForceWithJSON = errors.New("JSON output only supported with force option")
	errNoConsent       = errors.New("consent not given")
)

type scmStorageCmd struct {
	Prepare prepareSCMCmd `command:"prepare" description:"Prepare SCM devices so that they can be used with DAOS"`
	Reset   resetSCMCmd   `command:"reset" description:"Reset SCM devices that have been used with DAOS"`
	Scan    scanSCMCmd    `command:"scan" description:"Scan SCM devices"`
}

type prepareSCMCmd struct {
	scmCmd
	NrNamespacesPerSocket uint `short:"S" long:"scm-ns-per-socket" description:"Number of PMem namespaces to create per socket" default:"1"`
	Force                 bool `short:"f" long:"force" description:"Perform SCM operations without waiting for confirmation"`
}

func preparePMem(cmd *prepareSCMCmd) (*storage.ScmPrepareResponse, error) {
	storageCtlSvc, err := scmCmdInit(&cmd.scmCmd)
	if err != nil {
		return nil, err
	}

	if cmd.NrNamespacesPerSocket == 0 {
		return nil, errors.New("(-S|--scm-ns-per-socket) should be set to at least 1")
	}

	cmd.Info("Prepare locally-attached PMem...")

	cmd.Info(MsgStoragePrepareWarn)
	if !cmd.Force {
		if cmd.JSONOutputEnabled() {
			return nil, errNoForceWithJSON
		}
		if !common.GetConsent(cmd) {
			return nil, errNoConsent
		}
	}

	req := storage.ScmPrepareRequest{
		SocketID:              cmd.SocketID,
		NrNamespacesPerSocket: cmd.NrNamespacesPerSocket,
	}
	cmd.Tracef("scm prepare request: %+v", req)

	// Prepare PMem modules to be presented as pmem device files.
	resp, err := storageCtlSvc.ScmPrepare(req)
	if err != nil {
		return nil, err
	}
	cmd.Tracef("scm prepare response: %+v", resp)

	if resp == nil {
		return nil, errors.New("scm prepare returned nil response")
	}
	if resp.Socket == nil {
		return nil, errors.New("scm prepare returned nil socket state")
	}
	state := resp.Socket.State

	if resp.RebootRequired {
		// The only valid state that requires a reboot after PMem prepare is NoRegions.
		if state != storage.ScmNoRegions {
			return nil, errors.Errorf("expected %s state if reboot required, got %s",
				storage.ScmNoRegions, state)
		}

		// If PMem resource allocations have been updated, prompt the user for reboot.
		cmd.Info("PMem AppDirect interleaved regions will be created on reboot. " +
			storage.ScmMsgRebootRequired)

		return nil, nil
	}

	// Respond to state reported by prepare setup.
	switch state {
	case storage.ScmStateUnknown:
		return nil, errors.New("failed to report state")
	case storage.ScmNoModules:
		return nil, storage.FaultScmNoPMem
	case storage.ScmNoRegions:
		return nil, errors.New("failed to create regions")
	case storage.ScmFreeCap:
		return nil, errors.New("failed to create namespaces")
	case storage.ScmNoFreeCap:
		if len(resp.Namespaces) == 0 {
			return nil, errors.New("failed to find namespaces")
		}

		return resp, nil
	default:
		return nil, errors.Errorf("unexpected state %q after scm prepare", state)
	}

	return nil, nil
}

func (cmd *prepareSCMCmd) Execute(_ []string) error {
	cmd.Debugf("executing prepare scm command: %+v", cmd)

	resp, err := preparePMem(cmd)
	if err != nil {
		return err
	}

	if resp != nil && len(resp.Namespaces) != 0 {
		if cmd.JSONOutputEnabled() {
			return cmd.OutputJSON(resp.Namespaces, nil)
		}

		// Namespaces exist so print details.
		var bld strings.Builder
		if err := pretty.PrintScmNamespaces(resp.Namespaces, &bld); err != nil {
			return err
		}
		cmd.Infof("%s\n", bld.String())
	}

	return nil
}

type resetSCMCmd struct {
	scmCmd
	Force bool `short:"f" long:"force" description:"Perform PMem prepare operation without waiting for confirmation"`
}

func resetPMem(cmd *resetSCMCmd) error {
	storageCtlSvc, err := scmCmdInit(&cmd.scmCmd)
	if err != nil {
		return err
	}

	cmd.Info("Reset locally-attached PMem...")

	cmd.Info(MsgStoragePrepareWarn)
	if !cmd.Force {
		if cmd.JSONOutputEnabled() {
			return errNoForceWithJSON
		}
		if !common.GetConsent(cmd) {
			return errNoConsent
		}
	}

	resetReq := storage.ScmPrepareRequest{
		SocketID: cmd.SocketID,
		Reset:    true,
	}
	cmd.Tracef("scm prepare (reset) request: %+v", resetReq)

	// Reset PMem modules to default memory mode after removing any PMem namespaces.
	resetResp, err := storageCtlSvc.ScmPrepare(resetReq)
	if err != nil {
		return err
	}
	cmd.Tracef("scm prepare (reset) response: %+v", resetResp)

	state := resetResp.Socket.State

	if resetResp.RebootRequired {
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
		return storage.FaultScmNoPMem
	default:
		return errors.Errorf("unexpected state %q after scm reset", state)
	}

	return nil
}

func (cmd *resetSCMCmd) Execute(_ []string) error {
	cmd.Debugf("executing reset scm command: %+v", cmd)

	return resetPMem(cmd)
}

type scanSCMCmd struct {
	scmCmd
}

func scanPMem(cmd *scanSCMCmd) (*storage.ScmScanResponse, error) {
	storageCtlSvc, err := scmCmdInit(&cmd.scmCmd)
	if err != nil {
		return nil, err
	}

	cmd.Info("Scanning locally-attached PMem storage...")

	req := storage.ScmScanRequest{
		SocketID: cmd.SocketID,
	}

	cmd.Tracef("scm scan request: %+v", req)
	return storageCtlSvc.ScmScan(req)
}

func (cmd *scanSCMCmd) Execute(_ []string) error {
	cmd.Debugf("executing scan scm command: %+v", cmd)

	resp, err := scanPMem(cmd)
	if err != nil {
		return err
	}
	cmd.Tracef("scm scan response: %+v", resp)

	if cmd.JSONOutputEnabled() {
		if len(resp.Namespaces) > 0 {
			return cmd.OutputJSON(resp.Namespaces, nil)
		} else {
			return cmd.OutputJSON(resp.Modules, nil)
		}
	}

	var bld strings.Builder
	if len(resp.Namespaces) > 0 {
		if err := pretty.PrintScmNamespaces(resp.Namespaces, &bld); err != nil {
			return err
		}
	} else {
		if err := pretty.PrintScmModules(resp.Modules, &bld); err != nil {
			return err
		}
	}
	cmd.Info(bld.String())

	return nil
}
