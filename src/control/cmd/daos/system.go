//
// (C) Copyright 2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

/*
#include "util.h"

struct daos_rank_uri *
get_rank_uri_at_idx(struct daos_rank_uri *uris, uint32_t i)
{
	return &uris[i];
}
*/
import "C"

import (
	"github.com/pkg/errors"
)

type rankURI struct {
	Rank int    `json:"rank"`
	URI  string `json:"uri"`
}

type systemInfo struct {
	Name     string     `json:"system_name"`
	Provider string     `json:"fabric_provider"`
	RankURIs []*rankURI `json:"rank_uris"`
}

type systemCmd struct {
	Query systemQueryCmd `command:"query" description:"query DAOS system via the daos_agent"`
}

type systemQueryCmd struct {
	daosCmd
}

func (cmd *systemQueryCmd) Execute(_ []string) error {
	var cSysInfo *C.struct_daos_sys_info
	rc := C.daos_mgmt_sys_info_alloc(&cSysInfo)
	if err := daosError(rc); err != nil {
		return errors.Wrap(err, "querying DAOS system information")
	}
	defer C.daos_mgmt_sys_info_free(cSysInfo)

	sysInfo := &systemInfo{
		Name:     C.GoString(&cSysInfo.dsi_system_name[0]),
		Provider: C.GoString(&cSysInfo.dsi_provider[0]),
	}

	for i := C.uint32_t(0); i < cSysInfo.dsi_nr_ms_ranks; i++ {
		cRankURI := C.get_rank_uri_at_idx(cSysInfo.dsi_ms_ranks, i)
		sysInfo.RankURIs = append(sysInfo.RankURIs, &rankURI{
			Rank: int(cRankURI.dru_rank),
			URI:  C.GoString(cRankURI.dru_uri),
		})
	}

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(sysInfo, nil)
	}

	cmd.Infof("connected to DAOS system:")
	cmd.Infof("\tname: %s", sysInfo.Name)
	cmd.Infof("\tfabric provider: %s", sysInfo.Provider)
	cmd.Info("\trank URIs:")
	for _, rankURI := range sysInfo.RankURIs {
		cmd.Infof("\t\trank[%d]: %s", rankURI.Rank, rankURI.URI)
	}
	return nil
}
