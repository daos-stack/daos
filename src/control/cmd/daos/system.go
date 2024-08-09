//
// (C) Copyright 2023-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"github.com/pkg/errors"
)

type systemCmd struct {
	Query systemQueryCmd `command:"query" description:"query DAOS system via the daos_agent"`
}

type systemQueryCmd struct {
	daosCmd
}

func (cmd *systemQueryCmd) Execute(_ []string) error {
	sysInfo, err := cmd.apiProvider.GetSystemInfo(cmd.MustLogCtx())
	if err != nil {
		return errors.Wrap(err, "failed to query DAOS system")
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(sysInfo, nil)
	}

	cmd.Infof("connected to DAOS system:")
	cmd.Infof("\tname: %s", sysInfo.Name)
	cmd.Infof("\tfabric provider: %s", sysInfo.Provider)
	cmd.Info("\taccess point ranks:")
	for _, apRankURI := range sysInfo.AccessPointRankURIs {
		cmd.Infof("\t\trank[%d]: %s", apRankURI.Rank, apRankURI.URI)
	}
	cmd.Info("\trank URIs:")
	for _, rankURI := range sysInfo.RankURIs {
		cmd.Infof("\t\trank[%d]: %s", rankURI.Rank, rankURI.URI)
	}
	return nil
}
