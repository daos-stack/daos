//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"strings"

	"gopkg.in/yaml.v2"

	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/lib/hardware/hwprov"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// configCmd is the struct representing the top-level config subcommand.
type configCmd struct {
	Generate configGenCmd `command:"generate" alias:"gen" description:"Generate DAOS server configuration file based on discoverable locally-attached hardware devices"`
}

type configGenCmd struct {
	cmdutil.LogCmd `json:"-"`
	helperLogCmd   `json:"-"`

	AccessPoints string `short:"a" long:"access-points" required:"1" description:"Comma separated list of access point addresses <ipv4addr/hostname>"`
	NrEngines    int    `short:"e" long:"num-engines" description:"Set the number of DAOS Engine sections to be populated in the config file output. If unset then the value will be set to the number of NUMA nodes on storage hosts in the DAOS system."`
	MinNrSSDs    int    `default:"1" short:"s" long:"min-ssds" description:"Minimum number of NVMe SSDs required per DAOS Engine (SSDs must reside on the host that is managing the engine). Set to 0 to generate a config with no NVMe."`
	NetClass     string `default:"best-available" short:"c" long:"net-class" description:"Network class preferred" choice:"best-available" choice:"ethernet" choice:"infiniband"`
}

type getFabricFn func(context.Context, logging.Logger) (*control.HostFabric, error)

func getLocalFabric(ctx context.Context, log logging.Logger) (*control.HostFabric, error) {
	return GetLocalHostFabric(ctx, hwprov.DefaultFabricScanner(log), allProviders)
}

type getStorageFn func(context.Context, logging.Logger) (*control.HostStorage, error)

func getLocalStorage(ctx context.Context, log logging.Logger) (*control.HostStorage, error) {
	svc := server.NewStorageControlService(log, config.DefaultServer().Engines)

	nvmeResp, err := svc.NvmeScan(storage.BdevScanRequest{})
	if err != nil {
		return nil, err
	}

	scmResp, err := svc.ScmScan(storage.ScmScanRequest{})
	if err != nil {
		return nil, err
	}

	return &control.HostStorage{
		NvmeDevices:   nvmeResp.Controllers,
		ScmModules:    scmResp.Modules,
		ScmNamespaces: scmResp.Namespaces,
	}, nil
}

func (cmd *configGenCmd) confGen(ctx context.Context, getFabric getFabricFn, getStorage getStorageFn) (*config.Server, error) {
	cmd.Debugf("ConfigGenerate called with command parameters %+v", cmd)

	accessPoints := strings.Split(cmd.AccessPoints, ",")

	var ndc hardware.NetDevClass
	switch cmd.NetClass {
	case "ethernet":
		ndc = hardware.Ether
	case "infiniband":
		ndc = hardware.Infiniband
	default:
		ndc = hardware.NetDevAny
	}

	hf, err := getFabric(ctx, cmd.Logger)
	if err != nil {
		return nil, err
	}

	hs, err := getStorage(ctx, cmd.Logger)
	if err != nil {
		return nil, err
	}

	req := control.ConfigGenerateReq{
		Log:          cmd.Logger,
		NrEngines:    cmd.NrEngines,
		MinNrSSDs:    cmd.MinNrSSDs,
		NetClass:     ndc,
		AccessPoints: accessPoints,
	}

	cmd.Debugf("control API GenConfig called with req: %+v", req)

	resp, err := control.GenConfig(req, control.DefaultEngineCfg, hf, hs)
	if err != nil {
		return nil, err
	}

	cmd.Debugf("control API GenConfig resp: %+v", resp)
	return resp.ConfigOut, nil
}

// Execute is run when configGenCmd activates.
//
// Attempt to auto generate a server config file with populated storage and network hardware
// parameters suitable to be used on the local host. Use the control API to generate config from
// local scan results using the current process.
func (cmd *configGenCmd) Execute(_ []string) error {
	ctx := context.Background()

	cfg, err := cmd.confGen(ctx, getLocalFabric, getLocalStorage)
	if err != nil {
		return err
	}

	bytes, err := yaml.Marshal(cfg)
	if err != nil {
		return err
	}

	// output recommended server config yaml file
	cmd.Info(string(bytes))
	return nil
}
