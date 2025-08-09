//
// (C) Copyright 2023-2024 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"sort"

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
	sort.Slice(sysInfo.AccessPointRankURIs, func(i, j int) bool {
		return sysInfo.AccessPointRankURIs[i].Rank < sysInfo.AccessPointRankURIs[j].Rank
	})
	for _, apRankURI := range sysInfo.AccessPointRankURIs {
		cmd.Infof("\t\trank[%d]: %s", apRankURI.Rank, apRankURI.URI)
	}
	cmd.Info("\trank URIs:")
	sort.Slice(sysInfo.RankURIs, func(i, j int) bool {
		return sysInfo.RankURIs[i].Rank < sysInfo.RankURIs[j].Rank
	})
	for _, rankURI := range sysInfo.RankURIs {
		cmd.Infof("\t\trank[%d]: %s", rankURI.Rank, rankURI.URI)
	}
	return nil
}
