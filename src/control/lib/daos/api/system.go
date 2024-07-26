//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package api

import (
	"unsafe"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/daos"
)

/*
#include <daos_mgmt.h>

#cgo LDFLAGS: -ldaos
*/
import "C"

// GetSystemInfo queries for the connected system information.
func (p *Provider) GetSystemInfo() (*daos.SystemInfo, error) {
	var cSysInfo *C.struct_daos_sys_info
	rc := C.daos_mgmt_get_sys_info(nil, &cSysInfo)
	if err := daos.ErrorFromRC(int(rc)); err != nil {
		return nil, errors.Wrap(err, "querying DAOS system information")
	}
	defer C.daos_mgmt_put_sys_info(cSysInfo)

	sysInfo := &daos.SystemInfo{
		Name:      C.GoString(&cSysInfo.dsi_system_name[0]),
		Provider:  C.GoString(&cSysInfo.dsi_fabric_provider[0]),
		AgentPath: C.GoString(&cSysInfo.dsi_agent_path[0]),
	}

	rankURIs := make(map[uint32]*daos.RankURI)

	for _, cRank := range unsafe.Slice(cSysInfo.dsi_ranks, int(cSysInfo.dsi_nr_ranks)) {
		rankURI := &daos.RankURI{
			Rank: uint32(cRank.dru_rank),
			URI:  C.GoString(cRank.dru_uri),
		}
		sysInfo.RankURIs = append(sysInfo.RankURIs, rankURI)
		rankURIs[rankURI.Rank] = rankURI
	}

	for _, cMSRank := range unsafe.Slice(cSysInfo.dsi_ms_ranks, int(cSysInfo.dsi_nr_ms_ranks)) {
		sysInfo.AccessPointRankURIs = append(sysInfo.AccessPointRankURIs, rankURIs[uint32(cMSRank)])
	}

	return sysInfo, nil
}
