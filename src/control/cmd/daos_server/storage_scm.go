//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"fmt"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/lib/hardware/hwprov"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
)

const MsgStoragePrepareWarn = "Memory allocation goals for PMem will be changed and namespaces " +
	"modified, this may be a destructive operation. Please ensure namespaces are unmounted " +
	"and locally attached PMem modules are not in use. Please be patient as it may take " +
	"several minutes and subsequent reboot maybe required.\n"

type scmPrepareResetFn func(storage.ScmPrepareRequest) (*storage.ScmPrepareResponse, error)

type scmCmd struct {
	Prepare prepareSCMCmd `command:"prepare" description:"Prepare SCM devices so that they can be used with DAOS"`
	Reset   resetSCMCmd   `command:"reset" description:"Reset SCM devices that have been used with DAOS"`
	Scan    scanSCMCmd    `command:"scan" description:"Scan SCM devices"`
}

type prepareSCMCmd struct {
	cmdutil.LogCmd `json:"-"`
	helperLogCmd   `json:"-"`
	cfgCmd         `json:"-"`
	socketCmd      `json:"-"`

	NrNamespacesPerSocket uint `short:"S" long:"scm-ns-per-socket" description:"Number of PMem namespaces to create per socket" default:"1"`
	Force                 bool `short:"f" long:"force" description:"Perform SCM operations without waiting for confirmation"`
}

func setSockFromCfg(log logging.Logger, cfg *config.Server, affSrc config.EngineAffinityFn, req *storage.ScmPrepareRequest) error {
	msgSkip := "so skip fetching sockid from config"

	if cfg == nil {
		log.Debug("nil input server config " + msgSkip)
		return nil
	}
	if req.SocketID != nil {
		log.Debug("sockid set in req " + msgSkip)
		return nil
	}

	// Set engine NUMA affinities accurately in config.
	if err := cfg.SetEngineAffinities(log, affSrc); err != nil {
		log.Error(fmt.Sprintf("failed to set engine affinities %s: %s", msgSkip, err))
		return nil
	}

	socksToPrep := make(map[uint]bool)

	for _, ec := range cfg.Engines {
		for _, scmCfg := range ec.Storage.Tiers.ScmConfigs() {
			if scmCfg.Class == storage.ClassRam {
				continue
			}
			socksToPrep[ec.Storage.NumaNodeIndex] = true
		}
	}

	// Only set socket ID in request if one engine has been configured in config file with SCM
	// storage specified, if engines exist assigned to different sockets, don't set the value.

	switch len(socksToPrep) {
	case 0:
	case 1:
		log.Debug("config contains scm-engines attached to a single socket, " +
			"only process that socket")
		for k := range socksToPrep {
			req.SocketID = &k
		}
	default:
		log.Debug("config contains scm-engines attached to different sockets, " +
			"process all sockets")
	}

	return nil
}

func (cmd *prepareSCMCmd) preparePMem(prepareBackend scmPrepareResetFn) error {
	if cmd.NrNamespacesPerSocket == 0 {
		return errors.New("(-S|--scm-ns-per-socket) should be set to at least 1")
	}

	cmd.Info("Prepare locally-attached PMem...")

	cmd.Info(MsgStoragePrepareWarn)
	if !cmd.Force && !common.GetConsent(cmd) {
		return errors.New("consent not given")
	}

	req := storage.ScmPrepareRequest{
		SocketID:              cmd.SocketID,
		NrNamespacesPerSocket: cmd.NrNamespacesPerSocket,
	}

	if err := setSockFromCfg(cmd.Logger, cmd.config, cmd.affinitySource, &req); err != nil {
		return errors.Wrap(err, "setting sockid in prepare request")
	}

	cmd.Debugf("scm prepare request parameters: %+v", req)

	// Prepare PMem modules to be presented as pmem device files.
	resp, err := prepareBackend(req)
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
		return errors.Errorf("unexpected state %q after scm prepare", state)
	}

	return nil
}

func genFiAffFn(fis *hardware.FabricInterfaceSet) config.EngineAffinityFn {
	return func(l logging.Logger, e *engine.Config) (uint, error) {
		fi, err := fis.GetInterfaceOnNetDevice(e.Fabric.Interface, e.Fabric.Provider)
		if err != nil {
			return 0, err
		}
		return fi.NUMANode, nil
	}
}

func getAffinitySource(log logging.Logger) (config.EngineAffinityFn, error) {
	scanner := hwprov.DefaultFabricScanner(log)

	fiSet, err := scanner.Scan(context.Background())
	if err != nil {
		return nil, errors.Wrap(err, "scan fabric")
	}

	return genFiAffFn(fiSet), nil
}

func (cmd *prepareSCMCmd) Execute(args []string) error {
	if err := cmd.setHelperLogFile(); err != nil {
		return err
	}

	if cmd.IgnoreConfig {
		cmd.config = nil
	} else {
		affSrc, err := getAffinitySource(cmd.Logger)
		if err != nil {
			cmd.Error(err.Error())
		} else {
			cmd.affinitySource = affSrc
		}
	}

	scs := server.NewStorageControlService(cmd.Logger, config.DefaultServer().Engines)

	cmd.Debugf("executing prepare scm command: %+v", cmd)
	return cmd.preparePMem(scs.ScmPrepare)
}

type resetSCMCmd struct {
	cmdutil.LogCmd `json:"-"`
	helperLogCmd   `json:"-"`
	cfgCmd         `json:"-"`
	socketCmd      `json:"-"`

	Force bool `short:"f" long:"force" description:"Perform PMem prepare operation without waiting for confirmation"`
}

func (cmd *resetSCMCmd) resetPMem(resetBackend scmPrepareResetFn) error {
	cmd.Info("Reset locally-attached PMem...")

	cmd.Info(MsgStoragePrepareWarn)
	if !cmd.Force && !common.GetConsent(cmd) {
		return errors.New("consent not given")
	}

	req := storage.ScmPrepareRequest{
		SocketID: cmd.SocketID,
		Reset:    true,
	}

	if err := setSockFromCfg(cmd.Logger, cmd.config, cmd.affinitySource, &req); err != nil {
		return errors.Wrap(err, "setting sockid in prepare (reset) request")
	}

	cmd.Debugf("scm prepare (reset) request parameters: %+v", req)

	// Reset PMem modules to default memory mode after removing any PMem namespaces.
	resp, err := resetBackend(req)
	if err != nil {
		return err
	}

	cmd.Debugf("scm prepare (reset) response: %+v", resp)

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
		return errors.Errorf("unexpected state %q after scm reset", state)
	}

	return nil
}

func (cmd *resetSCMCmd) Execute(args []string) error {
	if err := cmd.setHelperLogFile(); err != nil {
		return err
	}

	scs := server.NewStorageControlService(cmd.Logger, config.DefaultServer().Engines)

	cmd.Debugf("executing remove namespaces command: %+v", cmd)
	return cmd.resetPMem(scs.ScmPrepare)
}

type scanSCMCmd struct {
	cmdutil.LogCmd `json:"-"`
	helperLogCmd   `json:"-"`
}

func (cmd *scanSCMCmd) Execute(args []string) error {
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
