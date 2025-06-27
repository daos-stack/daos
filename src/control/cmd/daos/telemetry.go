//
// (C) Copyright 2025 Google LLC.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/daos/api"
)

type containerTelemetryCmd struct {
	Enable  containerEnableTelemetryCmd  `command:"enable" description:"enable container telemetry"`
	Disable containerDisableTelemetryCmd `command:"disable" description:"disable container telemetry"`
}

type containerEnableTelemetryCmd struct {
	existingContainerCmd

	DumpPool PoolID      `long:"dump-pool" short:"P" description:"Pool hosting the telemetry dump container (default: this pool)"`
	DumpCont ContainerID `long:"dump-cont" short:"C" description:"Container hosting the telemetry dump"`
	DumpDir  string      `long:"dump-dir" short:"D" description:"Root directory (local or in dump container) for telemetry dump"`
}

func (cmd *containerEnableTelemetryCmd) Execute(_ []string) error {
	ap, deallocCmdArgs, err := allocCmdArgs(cmd.Logger)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	cleanup, err := cmd.resolveAndOpen(daos.ContainerOpenFlagReadWrite, ap)
	if err != nil {
		return err
	}
	defer cleanup()

	req := api.ContainerTelemetryRequest{
		DumpPoolID:      cmd.DumpPool.String(),
		DumpContainerID: cmd.DumpCont.String(),
		DumpPathRoot:    cmd.DumpDir,
		DfuseMountPath:  cmd.Path,
	}
	if req.DumpPoolID == "" {
		req.DumpPoolID = cmd.pool.ID()
	}

	if err := cmd.container.EnableTelemetry(cmd.MustLogCtx(), req); err != nil {
		return err
	}

	cmd.Infof("Container telemetry enabled (dumping to %s:%s%s)", req.DumpPoolID, req.DumpContainerID, req.DumpPathRoot)
	return nil
}

type containerDisableTelemetryCmd struct {
	existingContainerCmd
}

func (cmd *containerDisableTelemetryCmd) Execute(_ []string) error {
	ap, deallocCmdArgs, err := allocCmdArgs(cmd.Logger)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	cleanup, err := cmd.resolveAndOpen(daos.ContainerOpenFlagReadWrite, ap)
	if err != nil {
		return err
	}
	defer cleanup()

	if err := cmd.container.DisableTelemetry(cmd.MustLogCtx(), &cmd.Path); err != nil {
		return err
	}

	cmd.Info("Container telemetry disabled")
	return nil
}
