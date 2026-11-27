//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/server"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
)

type storageCmd struct {
	PrepareReformat storagePrepareReformatCmd `command:"prepare-reformat" description:"Remove the superblock of a local engine with daos_server stopped, so that the engine requests a format when daos_server is next started"`
}

type storagePrepareReformatCmd struct {
	cfgCmd
	cmdutil.LogCmd
	EngineIndex uint32 `short:"e" long:"engine-index" required:"1" description:"0-based position of the engine in the engines list of the server config file"`

	checkDupeProcess func() error
	sys              storage.SystemProvider
}

func (cmd *storagePrepareReformatCmd) prepareEngine() error {
	if cmd.config == nil {
		return errors.New("server config is required; --ignore-config is not supported")
	}
	if len(cmd.config.Engines) <= int(cmd.EngineIndex) {
		return errors.Errorf("engine with index %d not found", cmd.EngineIndex)
	}
	if err := cmd.config.Validate(cmd.Logger); err != nil {
		return err
	}

	cfg := cmd.config.Engines[cmd.EngineIndex].WithIndex(cmd.EngineIndex)
	provider := storage.DefaultProvider(cmd.Logger, int(cmd.EngineIndex), &cfg.Storage)
	if cmd.sys != nil {
		provider.Sys = cmd.sys
	}

	// With no daemon to mount storage, do not mistake an unmounted filesystem
	// for an already-removed superblock.
	if !cmd.config.Metadata.HasPath() || cmd.config.Metadata.DevicePath != "" {
		mounted, err := provider.ControlMetadataIsMounted()
		if err != nil {
			return errors.Wrap(err, "checking metadata mount")
		}
		if !mounted {
			return errors.Errorf("engine %d metadata storage is not mounted; "+
				"mount it before preparing it for reformat", cmd.EngineIndex)
		}
	}

	instance := server.NewEngineInstance(cmd.Logger, provider, nil,
		engine.NewRunner(cmd.Logger, cfg), nil)
	return server.PrepareEngineReformat(instance)
}

// Execute prepares local engine storage without contacting a running daos_server.
func (cmd *storagePrepareReformatCmd) Execute(_ []string) error {
	check := cmd.checkDupeProcess
	if check == nil {
		check = common.CheckDupeProcess
	}
	if err := check(); err != nil {
		return errors.Wrap(err, "stop daos_server before preparing local storage for reformat")
	}
	if err := cmd.prepareEngine(); err != nil {
		return err
	}

	cmd.Infof("Engine instance %d prepared for reformat (superblock removed).\n"+
		"Start the instance by starting daos_server; the engine will then wait for "+
		"\"dmg storage format\".", cmd.EngineIndex)
	return nil
}
