//
// (C) Copyright 2022-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"os"

	"github.com/pkg/errors"
	"gopkg.in/yaml.v2"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/hardware/hwprov"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/config"
)

// configCmd is the struct representing the top-level config subcommand.
type configCmd struct {
	Generate configGenCmd `command:"generate" alias:"gen" description:"Generate DAOS server configuration file based on discoverable locally-attached hardware devices"`
}

type configGenCmd struct {
	helperLogCmd
	cmdutil.LogCmd
	cmdutil.ConfGenCmd

	SkipPrep bool `long:"skip-prep" description:"Skip preparation of devices during scan."`
}

type getFabricFn func(context.Context, logging.Logger, string) (*control.HostFabric, error)

func getLocalFabric(ctx context.Context, log logging.Logger, provider string) (*control.HostFabric, error) {
	hf, err := GetLocalFabricIfaces(ctx, hwprov.DefaultFabricScanner(log).Scan, provider)
	if err != nil {
		return nil, errors.Wrap(err, "fetching local fabric interfaces")
	}

	topo, err := hwprov.DefaultTopologyProvider(log).GetTopology(ctx)
	if err != nil {
		return nil, errors.Wrap(err, "fetching local hardware topology")
	}

	hf.NumaCount = uint32(topo.NumNUMANodes())
	hf.CoresPerNuma = uint32(topo.NumCoresPerNUMA())

	return hf, nil
}

type getStorageFn func(context.Context, logging.Logger, bool) (*control.HostStorage, error)

func getLocalStorage(ctx context.Context, log logging.Logger, skipPrep bool) (*control.HostStorage, error) {
	var err error
	snc := &scanNVMeCmd{
		SkipPrep: skipPrep,
		nvmeCmd:  nvmeCmd{},
	}
	snc.nvmeCmd.IgnoreConfig = true // Process all NVMe devices.
	snc.nvmeCmd.Logger = log

	if err := snc.initWith(initNvmeCmd); err != nil {
		return nil, errors.Wrap(err, "nvme init")
	}

	nvmeResp, errNvme := scanNVMe(snc)
	if errNvme != nil {
		return nil, errors.Wrap(errNvme, "nvme scan")
	}

	ssc := &scanSCMCmd{
		scmCmd: scmCmd{},
	}
	ssc.IgnoreConfig = true // Process all SCM devices.
	ssc.Logger = log

	if err := ssc.initWith(initScmCmd); err != nil {
		return nil, errors.Wrap(err, "scm init")
	}

	scmResp, errScm := scanPMem(ssc)
	if errScm != nil {
		return nil, errors.Wrap(errScm, "scm scan")
	}

	mi, err := common.GetMemInfo()
	if err != nil {
		return nil, errors.Wrap(err, "get hugepage info")
	}

	return &control.HostStorage{
		NvmeDevices:   nvmeResp.Controllers,
		ScmModules:    scmResp.Modules,
		ScmNamespaces: scmResp.Namespaces,
		MemInfo:       mi,
	}, nil
}

func (cmd *configGenCmd) confGen(ctx context.Context, getFabric getFabricFn, getStorage getStorageFn) (*config.Server, error) {
	cmd.Debugf("ConfGen called with command parameters %+v", cmd)

	prov := cmd.NetProvider
	if prov == allProviders {
		prov = ""
	}

	hf, err := getFabric(ctx, cmd.Logger, prov)
	if err != nil {
		return nil, err
	}

	cmd.Debugf("fetched host fabric info on localhost: %+v", hf)

	hs, err := getStorage(ctx, cmd.Logger, cmd.SkipPrep)
	if err != nil {
		return nil, err
	}
	cmd.Debugf("fetched host storage info on localhost: %+v", hs)

	req := new(control.ConfGenerateReq)
	if err := convert.Types(cmd, req); err != nil {
		return nil, err
	}
	cmd.Debugf("control API ConfGenerate called with req: %+v", req)

	// Use a modified commandline logger to send all log messages to stderr in debug mode
	// during the generation of server config file parameters so stdout can be reserved for
	// config file output only. If not in debug mode, only log >=error to stderr.
	logger := logging.NewCommandLineLogger()
	if cmd.Logger.EnabledFor(logging.LogLevelTrace) {
		cmd.Debug("debug mode detected, writing all logs to stderr")
		logger.ClearLevel(logging.LogLevelInfo)
		logger.WithInfoLogger(logging.NewCommandLineInfoLogger(os.Stderr))
	} else {
		// Suppress info logging when not in debug mode.
		logger.SetLevel(logging.LogLevelError)
	}
	req.Log = logger

	resp, err := control.ConfGenerate(*req, control.DefaultEngineCfg, hf, hs)
	if err != nil {
		return nil, err
	}

	cmd.Debugf("control API ConfGenerate resp: %+v", resp)
	return &resp.Server, nil
}

func (cmd *configGenCmd) confGenPrint(ctx context.Context, getFabric getFabricFn, getStorage getStorageFn) error {
	cfg, err := cmd.confGen(ctx, getFabric, getStorage)
	if err != nil {
		return err
	}

	bytes, err := yaml.Marshal(cfg)
	if err != nil {
		return err
	}

	// Print generated config yaml file contents to stdout.
	cmd.Info(string(bytes))
	return nil
}

// Execute is run when configGenCmd activates.
//
// Attempt to auto generate a server config file with populated storage and network hardware
// parameters suitable to be used on the local host. Use the control API to generate config from
// local scan results using the current process.
func (cmd *configGenCmd) Execute(_ []string) error {
	if err := common.CheckDupeProcess(); err != nil {
		return err
	}

	return cmd.confGenPrint(cmd.MustLogCtx(), getLocalFabric, getLocalStorage)
}
