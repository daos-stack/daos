//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/control"
)

// ServerCmd is the struct representing the top-level server subcommand.
type ServerCmd struct {
	SetLogMasks serverSetLogMasksCmd `command:"set-logmasks" description:"Set log masks for a set of facilities to a given level on all DAOS system engines at runtime"`
}

// serverSetLogMasksCmd is the struct representing the command to set engine log
// levels at runtime across system.
type serverSetLogMasksCmd struct {
	logCmd
	ctlInvokerCmd
	hostListCmd
	jsonOutputCmd
	Masks string `long:"masks" alias:"m" description:"Set log masks for a set of facilities to a given level. The input string should look like: PREFIX1=LEVEL1,PREFIX2=LEVEL2,... where the syntax is identical to what is expected by 'D_LOG_MASK' environment variable. If unset then reset engine log masks to use the 'log_mask' value set in the server config file (for each engine) at the time of DAOS system format"`
}

// Execute is run when serverSetLogMasksCmd activates.
func (cmd *serverSetLogMasksCmd) Execute(_ []string) (errOut error) {
	defer func() {
		errOut = errors.Wrap(errOut, "set engine log level failed")
	}()

	req := &control.SetEngineLogMasksReq{
		Masks: cmd.Masks,
		Reset: cmd.Masks == "",
	}
	req.SetHostList(cmd.hostlist)

	resp, err := control.SetEngineLogMasks(context.Background(), cmd.ctlInvoker, req)
	if err != nil {
		return err // control api returned an error, disregard response
	}

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(resp, resp.Errors())
	}

	if resp.Errors() == nil {
		cmd.log.Info("Engine log levels have been updated successfully.")
	}

	return resp.Errors()
}
