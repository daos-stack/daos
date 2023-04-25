//
// (C) Copyright 2021-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/lib/control"
)

// serverCmd is the struct representing the top-level server subcommand.
type serverCmd struct {
	SetLogMasks serverSetLogMasksCmd `command:"set-logmasks" alias:"slm" description:"Set log masks for a set of facilities to a given level and optionally specify debug streams to enable. Setting will be applied to all running DAOS I/O Engines present in the configured dmg hostlist."`
}

// serverSetLogMasksCmd is the struct representing the command to set engine log
// levels at runtime across system.
type serverSetLogMasksCmd struct {
	baseCmd
	ctlInvokerCmd
	hostListCmd
	jsonOutputCmd

	Args struct {
		Masks   string `position-args-name:"masks" description:"Set log masks for a set of facilities to a given level. The input string should look like PREFIX1=LEVEL1,PREFIX2=LEVEL2,... where the syntax is identical to what is expected by 'D_LOG_MASK' environment variable. If the 'PREFIX=' part is omitted, then the level applies to all defined facilities (e.g. a value of 'WARN' sets everything to WARN). If unset then reset engine log masks to use the 'log_mask' value set in the server config file (for each engine) at the time of DAOS system format. Supported levels are FATAL, CRIT, ERR, WARN, NOTE, INFO, DEBUG"`
		Streams string `position-args-name:"streams" description:"Employ finer grained control over debug streams. Mask bits are set as the first argument passed in D_DEBUG(mask, ...) and this input string (DD_MASK) can be set to enable different debug streams. The expected syntax is a comma separated list of stream identifiers and If not set will default to enabling all debug streams. Accepted DAOS Debug Streams are md,pl,mgmt,epc,df,rebuild,daos_default and Common Debug Streams (GURT) are any,trace,mem,net,io"`
		Rest    []string
	} `positional-args:"yes"`
}

// Execute is run when serverSetLogMasksCmd activates.
func (cmd *serverSetLogMasksCmd) Execute(_ []string) (errOut error) {
	defer func() {
		errOut = errors.Wrap(errOut, "set engine log level failed")
	}()

	if len(cmd.Args.Rest) > 0 {
		return errors.Errorf("expected 0-2 positional args but got %d",
			len(cmd.Args.Rest)+2)
	}

	req := &control.SetEngineLogMasksReq{
		Masks:   cmd.Args.Masks,
		Streams: cmd.Args.Streams,
	}
	req.SetHostList(cmd.getHostList())

	cmd.Debugf("set log masks request: %+v", req)

	resp, err := control.SetEngineLogMasks(context.Background(), cmd.ctlInvoker, req)
	if err != nil {
		return err // control api returned an error, disregard response
	}

	cmd.Debugf("set log masks response: %+v", resp)

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(resp, resp.Errors())
	}

	var outErr strings.Builder
	if err := pretty.PrintResponseErrors(resp, &outErr); err != nil {
		return err
	}
	if outErr.Len() > 0 {
		cmd.Error(outErr.String())
	} else {
		cmd.Info("Engine log levels have been updated successfully.")
	}

	return resp.Errors()
}
