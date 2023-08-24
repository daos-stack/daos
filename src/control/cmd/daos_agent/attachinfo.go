//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"fmt"
	"os"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
)

type dumpAttachInfoCmd struct {
	configCmd
	ctlInvokerCmd
	cmdutil.JSONOutputCmd
	Output string `short:"o" long:"output" default:"stdout" description:"Dump output to this location"`
}

func (cmd *dumpAttachInfoCmd) Execute(_ []string) error {
	out := os.Stdout
	if cmd.Output != "stdout" {
		f, err := os.Create(cmd.Output)
		if err != nil {
			return errors.Wrapf(err, "failed to create %q", cmd.Output)
		}
		defer f.Close()
		out = f
	}

	ctx := context.Background()
	req := &control.GetAttachInfoReq{
		AllRanks: true,
	}
	req.SetSystem(cmd.cfg.SystemName)
	resp, err := control.GetAttachInfo(ctx, cmd.ctlInvoker, req)
	if err != nil {
		return errors.Wrap(err, "GetAttachInfo failed")
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(resp, err)
	}

	system := cmd.cfg.SystemName
	if resp.System != "" {
		system = resp.System
	}
	/**
	 * cart/crt_group.c:crt_group_config_save()
	 *
	 * Save attach info to file with the name
	 * "<singleton_attach_path>/grpid.attach_info_tmp".
	 * The format of the file is:
	 * line 1: the process set name
	 * line 2: process set size
	 * line 3: "all" or "self"
	 *         "all" means dump all ranks' uri
	 *         "self" means only dump this rank's uri
	 * line 4 ~ N: <rank> <uri>
	 *
	 * An example file named daos_server.attach_info_tmp:
	 * ========================
	 * name daos_server
	 * size 1
	 * all
	 * 0 tcp://192.168.0.1:1234
	 * ========================
	 */
	ew := txtfmt.NewErrWriter(out)
	fmt.Fprintf(ew, "name %s\n", system)
	fmt.Fprintf(ew, "size %d\n", len(resp.ServiceRanks))
	fmt.Fprintln(ew, "all")
	for _, psr := range resp.ServiceRanks {
		fmt.Fprintf(ew, "%d %s\n", psr.Rank, psr.Uri)
	}

	return ew.Err
}
