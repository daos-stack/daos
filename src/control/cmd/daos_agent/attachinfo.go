//
// (C) Copyright 2020-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
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
	cmdutil.LogCmd
	cmdutil.JSONOutputCmd
	Output      string `short:"o" long:"output" default:"stdout" description:"Dump output to this location"`
	ProviderIdx *uint  `short:"n" long:"provider_idx" description:"Index of provider to fetch (if multiple)"`
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

	ctx := cmd.MustLogCtx()
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

	providerIdx := cmd.cfg.ProviderIdx
	if cmd.ProviderIdx != nil {
		providerIdx = *cmd.ProviderIdx
	}

	ranks, err := getServiceRanksForProviderIdx(resp, providerIdx)
	if err != nil {
		return err
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
	fmt.Fprintf(ew, "size %d\n", len(ranks))
	fmt.Fprintln(ew, "all")
	for _, psr := range ranks {
		fmt.Fprintf(ew, "%d %s\n", psr.Rank, psr.Uri)
	}

	return ew.Err
}

func getServiceRanksForProviderIdx(inResp *control.GetAttachInfoResp, idx uint) ([]*control.PrimaryServiceRank, error) {
	if idx == 0 {
		// Primary provider
		return inResp.ServiceRanks, nil
	}

	secIdx := int(idx) - 1
	if secIdx < 0 || secIdx >= len(inResp.AlternateClientNetHints) {
		return nil, errors.Errorf("provider index must be in range 0 <= idx <= %d", len(inResp.AlternateClientNetHints))
	}

	hint := inResp.AlternateClientNetHints[secIdx]
	ranks := make([]*control.PrimaryServiceRank, 0)
	for _, r := range inResp.AlternateServiceRanks {
		if r.ProviderIdx == hint.ProviderIdx {
			ranks = append(ranks, r)
		}
	}

	if len(ranks) == 0 {
		return nil, errors.Errorf("no ranks for provider %q (idx %d)", hint.Provider, idx)
	}

	return ranks, nil
}
