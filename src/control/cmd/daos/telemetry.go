//
// (C) Copyright 2025 Google LLC.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"path/filepath"
	"strings"

	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/pkg/errors"
)

/*
#include "util.h"

*/
import "C"

type containerTelemetryCmd struct {
	Enable  enableContainerTelemetryCmd  `command:"enable" description:"enable container telemetry"`
	Disable disableContainerTelemetryCmd `command:"disable" description:"disable container telemetry"`
}

type enableContainerTelemetryCmd struct {
	existingContainerCmd

	DumpPool PoolID      `long:"dump-pool" short:"P" description:"Pool hosting the telemetry dump container (default: this pool)"`
	DumpCont ContainerID `long:"dump-cont" short:"C" description:"Container hosting the telemetry dump"`
	DumpDir  string      `long:"dump-dir" short:"D" description:"Root directory (local or in dump container) for telemetry dump"`
}

func (cmd *enableContainerTelemetryCmd) Execute(_ []string) error {
	cleanup, err := cmd.resolveAndConnect(C.DAOS_PC_RW, nil)
	if err != nil {
		return err
	}
	defer cleanup()

	// Clear any existing telemetry attributes.
	if err := deleteContTelemAttrs(cmd.cContHandle); err != nil {
		return errors.Wrap(err, "failed to delete container telemetry attributes")
	}

	dumpPool := cmd.DumpPool.String()
	dumpCont := cmd.DumpCont.String()
	dumpDir := cmd.DumpDir

	if dumpPool == "" && dumpCont != "" {
		dumpPool = cmd.poolBaseCmd.Args.Pool.String()
	}
	if dumpPool != "" && dumpCont == "" {
		return errors.New("dump-cont must be set if dump-pool is set")
	}
	if dumpCont == "" && dumpDir == "" {
		return errors.New("dump-cont must be set if dump-dir is not set")
	}
	if dumpCont == cmd.ContainerID().String() {
		// TODO: More thorough check (e.g. sneaking in a container UUID)
		return errors.New("dump-cont may not be the same as the source container")
	}

	// TODO: Once we have the Container API, we could create the dump container if
	// it doesn't already exist.

	// TODO: Move this behind a Container API method, e.g. EnableTelemetry()
	dumpAttrs := attrList{}
	if dumpPool != "" {
		dumpAttrs = append(dumpAttrs, &attribute{Name: daos.ClientMetricsDumpPoolAttr, Value: []byte(dumpPool)})
	}
	if dumpCont != "" {
		dumpAttrs = append(dumpAttrs, &attribute{Name: daos.ClientMetricsDumpContAttr, Value: []byte(dumpCont)})
	}
	if dumpDir != "" {
		if !strings.HasPrefix(dumpDir, "/") {
			dumpDir = "/" + dumpDir
		}
		dumpDir = filepath.Clean(dumpDir)
		dumpAttrs = append(dumpAttrs, &attribute{Name: daos.ClientMetricsDumpDirAttr, Value: []byte(dumpDir)})
	}
	if err := setDaosAttributes(cmd.cContHandle, contAttr, dumpAttrs); err != nil {
		return err
	}

	cmd.Infof("Container telemetry enabled (dumping to %s:%s%s)", dumpPool, dumpCont, dumpDir)
	return nil
}

type disableContainerTelemetryCmd struct {
	existingContainerCmd
}

func deleteContTelemAttrs(cContHandle C.daos_handle_t) error {
	// TODO: Move this behind a Container API method, e.g. DisableTelemetry()
	for _, dumpAttr := range []string{daos.ClientMetricsDumpPoolAttr, daos.ClientMetricsDumpContAttr, daos.ClientMetricsDumpDirAttr} {
		if err := delDaosAttribute(cContHandle, contAttr, dumpAttr); err != nil {
			return err
		}
	}

	return nil
}

func (cmd *disableContainerTelemetryCmd) Execute(_ []string) error {
	cleanup, err := cmd.resolveAndConnect(C.DAOS_PC_RW, nil)
	if err != nil {
		return err
	}
	defer cleanup()

	if err := deleteContTelemAttrs(cmd.cContHandle); err != nil {
		return errors.Wrap(err, "failed to delete container telemetry attributes")
	}

	cmd.Info("Container telemetry disabled")
	return nil
}
