//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package daos

import (
	"net/url"
	"sort"
)

type (
	// RankURI expresses a rank-to-fabric-URI mapping.
	RankURI struct {
		Rank uint32 `json:"rank"`
		URI  string `json:"uri"`
	}

	// SystemInfo represents information about the connected system.
	SystemInfo struct {
		Name                string     `json:"system_name"`
		Provider            string     `json:"fabric_provider"`
		AgentPath           string     `json:"agent_path"`
		RankURIs            []*RankURI `json:"rank_uris"`
		AccessPointRankURIs []*RankURI `json:"access_point_rank_uris"`
	}
)

// AccessPoints returns a string slice representation of the system access points.
func (si *SystemInfo) AccessPoints() []string {
	apSet := make(map[string]struct{})
	for _, ri := range si.AccessPointRankURIs {
		// NB: This is intended for IP-based address schemes. If we can't
		// pretty-print the URI as an address, then the fallback is to use
		// the raw URI. Not as nice, but better than nothing.
		url, err := url.Parse(ri.URI)
		if err == nil {
			apSet[url.Hostname()] = struct{}{}
		} else {
			apSet[ri.URI] = struct{}{}
		}
	}

	apList := make([]string, 0, len(apSet))
	for ap := range apSet {
		apList = append(apList, ap)
	}
	sort.Strings(apList)

	return apList
}
