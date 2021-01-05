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
// the request. The logic for determining the list is as follows:
//
// 1.  If the request has an explicit hostlist set, use it regardless
//     of request type. This allows for flexibility and config overrides,
//     but generally should be used for non-AP requests to specific hosts.
// 2.  If there is no hostlist set on the request, check the request type
//     and use the following decision tree:
// 2a. If the request is destined for an Access Point (DAOS MS Replica),
//     pick the first host in the configuration's hostlist. By convention
//     this host will be an AP and the request should succeed. If for some
//     reason the request fails, a future mechanism will attempt to find
//     a working AP replica to service the request.
// 2b. If the request is not destined for an AP, then the request is sent
//     to the entire hostlist set in the configuration.
//
// Will always return at least 1 host, or an error.
func getRequestHosts(cfg *Config, req targetChooser) (hosts []string, err error) {
	hosts = req.getHostList()
	if len(hosts) == 0 {
		if len(cfg.HostList) == 0 {
			return nil, FaultConfigEmptyHostList
		}
		hosts = cfg.HostList
	}

	if cfg.ControlPort == 0 {
		return nil, FaultConfigBadControlPort
	}

	hosts, err = common.ParseHostList(hosts, cfg.ControlPort)
	if err != nil {
		return nil, err
	}

	if req.isMSRequest() {
		// pick first host as AP, by convention
		hosts = hosts[:1]
	}

	return hosts, nil
}
