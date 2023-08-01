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
	"github.com/daos-stack/daos/src/control/common/cmdutil"
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
	cmdutil.JSONOutputCmd
	Masks      *string `short:"m" long:"masks" description:"Set log masks for a set of facilities to a given level. The input string should look like PREFIX1=LEVEL1,PREFIX2=LEVEL2,... where the syntax is identical to what is expected by 'D_LOG_MASK' environment variable. If the 'PREFIX=' part is omitted, then the level applies to all defined facilities (e.g. a value of 'WARN' sets everything to WARN). If unset then reset engine log masks to use the 'log_mask' value set in the server config file (for each engine) at the time of DAOS system format. Supported levels are FATAL, CRIT, ERR, WARN, NOTE, INFO, DEBUG"`
	Streams    *string `short:"d" long:"streams" description:"Employ finer grained control over debug streams. Mask bits are set as the first argument passed in D_DEBUG(mask, ...) and this input string (DD_MASK) can be set to enable different debug streams. The expected syntax is a comma separated list of stream identifiers and accepted DAOS Debug Streams are md,pl,mgmt,epc,df,rebuild,daos_default and Common Debug Streams (GURT) are any,trace,mem,net,io. If not set, streams will be read from server config file and if set to an empty string then all debug streams will be enabled"`
	Subsystems *string `short:"s" long:"subsystems" description:"This input string is equivalent to the use of the DD_SUBSYS environment variable and can be set to enable logging for specific subsystems or facilities. The expected syntax is a comma separated list of facility identifiers. Accepted DAOS facilities are common,tree,vos,client,server,rdb,pool,container,object,placement,rebuild,tier,mgmt,bio,tests, Common facilities (GURT) are MISC,MEM and CaRT facilities RPC,BULK,CORPC,GRP,LM,HG,ST,IV If not set, subsystems to enable will be read from server config file and if set to an empty string then logging all subsystems will be enabled"`
}

// Execute is run when serverSetLogMasksCmd activates.
func (cmd *serverSetLogMasksCmd) Execute(_ []string) (errOut error) {
	defer func() {
		errOut = errors.Wrap(errOut, "set engine log level failed")
	}()

	req := &control.SetEngineLogMasksReq{
		Masks:      cmd.Masks,
		Streams:    cmd.Streams,
		Subsystems: cmd.Subsystems,
	}
	req.SetHostList(cmd.getHostList())

	cmd.Debugf("set log masks request: %+v", req)

	resp, err := control.SetEngineLogMasks(context.Background(), cmd.ctlInvoker, req)
	if err != nil {
		return err // control api returned an error, disregard response
	}

	cmd.Debugf("set log masks response: %+v", resp)

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(resp, resp.Errors())
	}

	var out, outErr strings.Builder
	if err := pretty.PrintSetEngineLogMasksResp(resp, &out, &outErr); err != nil {
		return err
	}
	if outErr.Len() > 0 {
		cmd.Error(outErr.String())
	}
	if out.Len() > 0 {
		cmd.Info(out.String())
	}

	return resp.Errors()
}
