//
// (C) Copyright 2023-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"os"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/lib/hardware/hwprov"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/pbin"
	"github.com/daos-stack/daos/src/control/server"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// helperLogCmd is an embeddable type that extends a command with
// server helper privileged binary logging capabilities.
type helperLogCmd struct {
	HelperLogFile string `short:"l" long:"helper-log-file" description:"Log file location for debug from daos_server_helper binary"`
}

func (hlc *helperLogCmd) setHelperLogFile() error {
	filename := hlc.HelperLogFile
	if filename == "" {
		return nil
	}

	return errors.Wrap(os.Setenv(pbin.DaosPrivHelperLogFileEnvVar, filename),
		"unable to configure privileged helper logging")
}

type iommuCheckFn func() (bool, error)

type iommuChecker interface {
	setIOMMUChecker(iommuCheckFn)
}

type iommuCheckerCmd struct {
	isIOMMUEnabled iommuCheckFn
}

func (icc *iommuCheckerCmd) setIOMMUChecker(fn iommuCheckFn) {
	if icc == nil {
		return
	}
	icc.isIOMMUEnabled = fn
}

// IsIOMMUEnabled implements hardware.IOMMUDetector interface.
func (icc *iommuCheckerCmd) IsIOMMUEnabled() (bool, error) {
	if icc == nil {
		return false, errors.New("nil pointer receiver")
	}
	if icc.isIOMMUEnabled == nil {
		return false, errors.New("nil isIOMMUEnabled function")
	}

	return icc.isIOMMUEnabled()
}

func storageCmdInit(cmd *baseScanCmd) (*server.StorageControlService, error) {
	if err := common.CheckDupeProcess(); err != nil {
		return nil, err
	}

	// Set storage control service to handle backend calls.
	engCfgs := config.DefaultServer().Engines
	if cmd.config != nil {
		engCfgs = cmd.config.Engines
	}
	scs := server.NewStorageControlService(cmd.Logger, engCfgs)

	cmd.Logger.Tracef("storage control service set: %+v", scs)
	return scs, nil
}

type scmSocketCmd struct {
	SocketID *uint `long:"socket" description:"Perform PMem namespace operations on the socket identified by this ID (defaults to all sockets). PMem region operations will be performed across all sockets."`
}

type nvmeCmd struct {
	baseScanCmd     `json:"-"`
	helperLogCmd    `json:"-"`
	iommuCheckerCmd `json:"-"`
	ctlSvc          *server.StorageControlService
}

func (cmd *nvmeCmd) initWith(initFn initNvmeCmdFn) error {
	var err error
	cmd.ctlSvc, err = initFn(cmd)
	if err != nil {
		return err
	}

	return nil
}

type initNvmeCmdFn func(cmd *nvmeCmd) (*server.StorageControlService, error)

func initNvmeCmd(cmd *nvmeCmd) (*server.StorageControlService, error) {
	if err := cmd.setHelperLogFile(); err != nil {
		return nil, err
	}

	cmd.setIOMMUChecker(hwprov.DefaultIOMMUDetector(cmd.Logger).IsIOMMUEnabled)

	return storageCmdInit(&cmd.baseScanCmd)
}

type scmCmd struct {
	baseScanCmd  `json:"-"`
	helperLogCmd `json:"-"`
	scmSocketCmd `json:"-"`
	ctlSvc       *server.StorageControlService
}

func (cmd *scmCmd) initWith(initFn initScmCmdFn) error {
	var err error
	cmd.ctlSvc, err = initFn(cmd)
	if err != nil {
		return err
	}

	return nil
}

func genFiAffFn(fis *hardware.FabricInterfaceSet) config.EngineAffinityFn {
	return func(l logging.Logger, e *engine.Config) (uint, error) {
		prov, err := e.Fabric.GetPrimaryProvider()
		if err != nil {
			return 0, errors.Wrap(err, "getting primary provider")
		}

		fi, err := fis.GetInterfaceOnNetDevice(e.Fabric.Interface, prov)
		if err != nil {
			return 0, err
		}
		return fi.NUMANode, nil
	}
}

func getAffinitySource(ctx context.Context, log logging.Logger, cfg *config.Server) (config.EngineAffinityFn, error) {
	scanner := hwprov.DefaultFabricScanner(log)

	provs, err := cfg.Fabric.GetProviders()
	if err != nil {
		return nil, errors.Wrap(err, "getting configured providers")
	}

	fiSet, err := scanner.Scan(ctx, provs...)
	if err != nil {
		return nil, errors.Wrap(err, "scan fabric")
	}

	return genFiAffFn(fiSet), nil
}

func getSockFromCfg(log logging.Logger, cfg *config.Server, affSrc config.EngineAffinityFn) *uint {
	msgSkip := "so skip fetching sockid from config"

	if cfg == nil {
		log.Debug("nil input server config " + msgSkip)
		return nil
	}
	if affSrc == nil {
		log.Debug("engine affinity cannot be determined " + msgSkip)
		return nil
	}

	if err := cfg.SetEngineAffinities(log, affSrc); err != nil {
		log.Errorf("failed to set engine affinities %s: %s", msgSkip, err)
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
	// storage specified, if engines exist assigned to multiple sockets, don't set the value.
	switch len(socksToPrep) {
	case 0:
	case 1:
		log.Debug("config contains scm-engines attached to a single socket, " +
			"only process that socket")
		for k := range socksToPrep {
			return &k
		}
	default:
		log.Debug("config contains scm-engines attached to different sockets, " +
			"process all sockets")
	}

	return nil
}

type initScmCmdFn func(cmd *scmCmd) (*server.StorageControlService, error)

func initScmCmd(cmd *scmCmd) (*server.StorageControlService, error) {
	if err := cmd.setHelperLogFile(); err != nil {
		return nil, err
	}
	if cmd.IgnoreConfig {
		cmd.config = nil
	} else if cmd.config != nil && cmd.SocketID == nil {
		// Read SocketID from config if not set explicitly in command.
		affSrc, err := getAffinitySource(cmd.MustLogCtx(), cmd.Logger, cmd.config)
		if err != nil {
			cmd.Error(err.Error())
		} else {
			cmd.SocketID = getSockFromCfg(cmd.Logger, cmd.config, affSrc)
		}
	}

	return storageCmdInit(&cmd.baseScanCmd)
}
