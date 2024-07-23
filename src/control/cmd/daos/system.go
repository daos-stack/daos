//
// (C) Copyright 2023-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

/*
#include "util.h"
*/
import "C"

import (
	"unsafe"

	"github.com/pkg/errors"
)

type rankURI struct {
	Rank uint32 `json:"rank"`
	URI  string `json:"uri"`
}

type systemInfo struct {
	Name                string     `json:"system_name"`
	Provider            string     `json:"fabric_provider"`
	RankURIs            []*rankURI `json:"rank_uris"`
	AccessPointRankURIs []*rankURI `json:"access_point_rank_uris"`
}

type systemCmd struct {
	Query systemQueryCmd `command:"query" description:"query DAOS system via the daos_agent"`
}

type systemQueryCmd struct {
	daosCmd
}

func (cmd *systemQueryCmd) Execute(_ []string) error {
	var cSysInfo *C.struct_daos_sys_info
	rc := C.daos_mgmt_get_sys_info(nil, &cSysInfo)
	if err := daosError(rc); err != nil {
		return errors.Wrap(err, "querying DAOS system information")
	}
	defer C.daos_mgmt_put_sys_info(cSysInfo)

	sysInfo := &systemInfo{
		Name:     C.GoString(&cSysInfo.dsi_system_name[0]),
		Provider: C.GoString(&cSysInfo.dsi_fabric_provider[0]),
	}

	rankURIs := make(map[uint32]*rankURI)

	for _, cRank := range unsafe.Slice(cSysInfo.dsi_ranks, int(cSysInfo.dsi_nr_ranks)) {
		rankURI := &rankURI{
			Rank: uint32(cRank.dru_rank),
			URI:  C.GoString(cRank.dru_uri),
		}
		sysInfo.RankURIs = append(sysInfo.RankURIs, rankURI)
		rankURIs[rankURI.Rank] = rankURI
	}

	for _, cMSRank := range unsafe.Slice(cSysInfo.dsi_ms_ranks, int(cSysInfo.dsi_nr_ms_ranks)) {
		sysInfo.AccessPointRankURIs = append(sysInfo.AccessPointRankURIs, rankURIs[uint32(cMSRank)])
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
