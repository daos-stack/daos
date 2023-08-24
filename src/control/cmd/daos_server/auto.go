//
// (C) Copyright 2022-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"os"
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

var ErrTmpfsNoExtMDPath = errors.New("--use-tmpfs-scm will generate an md-on-ssd config and so " +
	"--control-metadata-path must also be set")

// configCmd is the struct representing the top-level config subcommand.
type configCmd struct {
	Generate configGenCmd `command:"generate" alias:"gen" description:"Generate DAOS server configuration file based on discoverable locally-attached hardware devices"`
}

type configGenCmd struct {
	cmdutil.LogCmd  `json:"-"`
	helperLogCmd    `json:"-"`
	AccessPoints    string `default:"localhost" short:"a" long:"access-points" description:"Comma separated list of access point addresses <ipv4addr/hostname>"`
	NrEngines       int    `short:"e" long:"num-engines" description:"Set the number of DAOS Engine sections to be populated in the config file output. If unset then the value will be set to the number of NUMA nodes on storage hosts in the DAOS system."`
	SCMOnly         bool   `short:"s" long:"scm-only" description:"Create a SCM-only config without NVMe SSDs."`
	NetClass        string `default:"infiniband" short:"c" long:"net-class" description:"Set the network class to be used" choice:"ethernet" choice:"infiniband"`
	NetProvider     string `short:"p" long:"net-provider" description:"Set the network provider to be used"`
	UseTmpfsSCM     bool   `short:"t" long:"use-tmpfs-scm" description:"Use tmpfs for scm rather than PMem"`
	ExtMetadataPath string `short:"m" long:"control-metadata-path" description:"External storage path to store control metadata in MD-on-SSD mode"`
	SkipPrep        bool   `long:"skip-prep" description:"Skip preparation of devices during scan."`
}

type getFabricFn func(context.Context, logging.Logger, string) (*control.HostFabric, error)

func getLocalFabric(ctx context.Context, log logging.Logger, provider string) (*control.HostFabric, error) {
	hf, err := GetLocalFabricIfaces(ctx, hwprov.DefaultFabricScanner(log), provider)
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

type getStorageFn func(context.Context, logging.Logger, bool) (*control.HostStorage, error)

func getLocalStorage(ctx context.Context, log logging.Logger, skipPrep bool) (*control.HostStorage, error) {
	svc := server.NewStorageControlService(log, config.DefaultServer().Engines).
		WithVMDEnabled() // use vmd if present

	var nc *nvmeCmd
	if !skipPrep {
		nc = &nvmeCmd{}
		nc.Logger = log
		nc.IgnoreConfig = true
		if err := nc.init(); err != nil {
			return nil, errors.Wrap(err, "could not init nvme cmd")
		}

		req := storage.BdevPrepareRequest{}
		if err := prepareNVMe(req, nc, svc.NvmePrepare); err != nil {
			return nil, errors.Wrap(err, "nvme prep before fetching local storage failed, "+
				"try cmd again with --skip-prep after performing a manual nvme prepare")
		}
	}

	nvmeResp, err := svc.NvmeScan(storage.BdevScanRequest{})
	if err != nil {
		return nil, errors.Wrapf(err, "nvme scan")
	}

	if !skipPrep {
		// TODO SPDK-2926: If VMD is enabled and PCI_ALLOWED list is set to a subset of VMD
		//                 controllers (as specified in the server config file) then the
		//                 backing devices of the unselected VMD controllers will be bound
		//                 to no driver and therefore inaccessible from both OS and SPDK.
		//                 Workaround is to run nvme scan --ignore-config to reset driver
		//                 bindings.

		req := storage.BdevPrepareRequest{Reset_: true}
		if err := resetNVMe(req, nc, svc.NvmePrepare); err != nil {
			return nil, errors.Wrap(err, "nvme reset after fetching local storage failed, "+
				"try cmd again with --skip-prep after performing a manual nvme reset")
		}
	}

	scmResp, err := svc.ScmScan(storage.ScmScanRequest{})
	if err != nil {
		return nil, errors.Wrapf(err, "scm scan")
	}

	mi, err := common.GetMemInfo()
	if err != nil {
		return nil, errors.Wrapf(err, "get hugepage info")
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

	if cmd.UseTmpfsSCM && cmd.ExtMetadataPath == "" {
		return nil, ErrTmpfsNoExtMDPath
	}

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

	req := control.ConfGenerateReq{
		NrEngines:       cmd.NrEngines,
		SCMOnly:         cmd.SCMOnly,
		NetClass:        ndc,
		NetProvider:     cmd.NetProvider,
		AccessPoints:    accessPoints,
		UseTmpfsSCM:     cmd.UseTmpfsSCM,
		ExtMetadataPath: cmd.ExtMetadataPath,
	}
	cmd.Debugf("control API ConfGenerate called with req: %+v", req)

	// Use a modified commandline logger to send all log messages to stderr during the
	// generation of server config file parameters so stdout can be reserved for config file
	// output only.
	logStderr := logging.NewCommandLineLogger()
	logStderr.ClearLevel(logging.LogLevelInfo)
	logStderr.WithInfoLogger(logging.NewCommandLineInfoLogger(os.Stderr))
	req.Log = logStderr

	resp, err := control.ConfGenerate(req, control.DefaultEngineCfg, hf, hs)
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

	return cmd.confGenPrint(context.Background(), getLocalFabric, getLocalStorage)
}
