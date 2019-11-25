//
// (C) Copyright 2018-2019 Intel Corporation.
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

package main

import (
	"fmt"
	"strconv"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/client"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
)

const (
	defaultHostPort = 10001
)

// hostsByPort takes slice of address patterns and returns a HostGroup
// for each port (after expanding each port specific nodeset).
//
// e.g. for input patterns string[]{"intelA[1-10]:10000", "intelB[2,3]:10001"}
// 	returns HostGroups{{10000, ["intelA1", ..."intelA10"]},
//		{10001, ["intelB2", "intelB3"]}}
func hostsByPort(addrPatterns []string, defaultPort int) (hostlist.HostGroup, error) {
	portHosts := make(hostlist.HostGroup)

	for _, p := range addrPatterns {
		var port string
		// separate port from compressed host string
		hp := strings.Split(p, ":")

		switch len(hp) {
		case 1:
			// no port specified, use default
			port = strconv.Itoa(defaultPort)
		case 2:
			port = hp[1]
		default:
			return nil, errors.Errorf("cannot parse %s", p)
		}

		if port == "" || port == "0" {
			return nil, errors.Errorf("invalid port %q", port)
		}

		if err := portHosts.AddHost(port, hp[0]); err != nil {
			return nil, err
		}
	}

	return portHosts, nil
}

// flattenHostAddrs takes nodeset:port patterns and returns individual addresses
// after expanding nodesets and mapping to ports.
func flattenHostAddrs(addrPatterns string) (addrs []string, err error) {
	// expand any compressed nodesets for specific ports
	// example opts.HostList: intelA[1-10]:10000,intelB[2,3]:10001
	portHostSets, err := hostsByPort(strings.Split(addrPatterns, ","), defaultHostPort)
	if err != nil {
		return
	}

	ports := portHostSets.Keys()
	// reconstruct slice of all "host:port" addresses from map
	for _, port := range ports {
		set := portHostSets[port]
		hosts := strings.Split(set.DerangedString(), ",")
		for _, host := range hosts {
			addrs = append(addrs, fmt.Sprintf("%s:%s", host, port))
		}
	}

	return
}

// checkConns analyses connection results and returns summary compressed active
// and inactive hostlists.
func checkConns(results client.ResultMap) (active hostlist.HostGroup, inactive hostlist.HostGroup, err error) {
	active = make(hostlist.HostGroup)
	inactive = make(hostlist.HostGroup)

	for addr := range results {
		if results[addr].Err != nil {
			// group failed conn attempts by error msg
			errMsg := results[addr].Err.Error()
			if err = inactive.AddHost(errMsg, addr); err != nil {
				return
			}
			continue
		}
		if err = active.AddHost("connected", addr); err != nil {
			return
		}
	}

	return
}
