//
// (C) Copyright 2020 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
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
