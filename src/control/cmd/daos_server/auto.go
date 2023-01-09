//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"strings"

	"github.com/pkg/errors"
	"gopkg.in/yaml.v2"

	"github.com/daos-stack/daos/src/control/common"
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

	AccessPoints string `default:"localhost" short:"a" long:"access-points" description:"Comma separated list of access point addresses <ipv4addr/hostname>"`
	NrEngines    int    `short:"e" long:"num-engines" description:"Set the number of DAOS Engine sections to be populated in the config file output. If unset then the value will be set to the number of NUMA nodes on storage hosts in the DAOS system."`
	MinNrSSDs    int    `default:"1" short:"s" long:"min-ssds" description:"Minimum number of NVMe SSDs required per DAOS Engine (SSDs must reside on the host that is managing the engine). Set to 0 to generate a config with no NVMe."`
	NetClass     string `default:"infiniband" short:"c" long:"net-class" description:"Set the network class to be used" choice:"ethernet" choice:"infiniband"`
	NetProvider  string `short:"p" long:"net-provider" description:"Set the network provider to be used"`
}

type getFabricFn func(context.Context, logging.Logger) (*control.HostFabric, error)

func getLocalFabric(ctx context.Context, log logging.Logger) (*control.HostFabric, error) {
	hf, err := GetLocalFabricIfaces(ctx, hwprov.DefaultFabricScanner(log), allProviders)
	if err != nil {
		return nil, errors.Wrapf(err, "fetching local fabric interfaces")
	}

	topo, err := hwprov.DefaultTopologyProvider(log).GetTopology(ctx)
	if err != nil {
		return nil, errors.Wrapf(err, "fetching local hardware topology")
	}

	hf.NumaCount = uint32(topo.NumNUMANodes())
	hf.CoresPerNuma = uint32(topo.NumCoresPerNUMA())

	return hf, nil
}

type getStorageFn func(context.Context, logging.Logger) (*control.HostStorage, error)

func getLocalStorage(ctx context.Context, log logging.Logger) (*control.HostStorage, error) {
	svc := server.NewStorageControlService(log, config.DefaultServer().Engines)

	nvmeResp, err := svc.NvmeScan(storage.BdevScanRequest{})
	if err != nil {
		return nil, errors.Wrapf(err, "nvme scan")
	}

	scmResp, err := svc.ScmScan(storage.ScmScanRequest{})
	if err != nil {
		return nil, errors.Wrapf(err, "scm scan")
	}

	hpi, err := common.GetHugePageInfo()
	if err != nil {
		return nil, errors.Wrapf(err, "get hugepage info")
	}

	return &control.HostStorage{
		NvmeDevices:   nvmeResp.Controllers,
		ScmModules:    scmResp.Modules,
		ScmNamespaces: scmResp.Namespaces,
		HugePageInfo: control.HugePageInfo{
			PageSizeKb: hpi.PageSizeKb,
		},
	}, nil
}

func (cmd *configGenCmd) confGen(ctx context.Context, getFabric getFabricFn, getStorage getStorageFn) (*config.Server, error) {
	cmd.Debugf("ConfGen called with command parameters %+v", cmd)

	accessPoints := strings.Split(cmd.AccessPoints, ",")

	var ndc hardware.NetDevClass
	switch cmd.NetClass {
	case "ethernet":
		ndc = hardware.Ether
	case "infiniband":
		ndc = hardware.Infiniband
	default:
		return nil, errors.Errorf("unrecognized net-class value %s", cmd.NetClass)
	}

	hf, err := getFabric(ctx, cmd.Logger)
	if err != nil {
		return nil, err
	}
	cmd.Debugf("fetched host fabric info on localhost: %+v", hf)

	hs, err := getStorage(ctx, cmd.Logger)
	if err != nil {
		return nil, err
	}
	cmd.Debugf("fetched host storage info on localhost: %+v", hs)

	req := control.ConfGenerateReq{
		Log:          cmd.Logger,
		NrEngines:    cmd.NrEngines,
		MinNrSSDs:    cmd.MinNrSSDs,
		NetClass:     ndc,
		NetProvider:  cmd.NetProvider,
		AccessPoints: accessPoints,
	}

	cmd.Debugf("control API ConfGenerate called with req: %+v", req)

	resp, err := control.ConfGenerate(req, control.DefaultEngineCfg, hf, hs)
	if err != nil {
		return nil, err
	}

	cmd.Debugf("control API ConfGenerate resp: %+v", resp)
	return &resp.Server, nil
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
