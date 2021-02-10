//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import "github.com/daos-stack/daos/src/control/common"

// getRequestHosts returns a list of control plane addresses for
// the request. If the request does not supply its own hostlist,
// create one from the configuration's hostlist.
func getRequestHosts(cfg *Config, req targetChooser) (hosts []string, err error) {
	if len(req.getHostList()) == 0 && len(cfg.HostList) == 0 {
		return nil, FaultConfigEmptyHostList
	}
	if cfg.ControlPort == 0 {
		return nil, FaultConfigBadControlPort
	}

	hosts, err = common.ParseHostList(req.getHostList(), cfg.ControlPort)
	if err != nil {
		return nil, err
	}

	if len(hosts) == 0 {
		hosts, err = common.ParseHostList(cfg.HostList, cfg.ControlPort)
		if err != nil {
			return nil, err
		}
	}

	return hosts, nil
}
