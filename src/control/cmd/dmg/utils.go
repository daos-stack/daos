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
	"sort"
	"strconv"
	"strings"

	"github.com/daos-stack/daos/src/control/client"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/pkg/errors"
)

// hostsByPort takes slice of address patterns and returns a map of host slice
// for each port (after expanding each port specific nodeset).
//
// e.g. for input patterns string[]{"intelA[1-10]:10000", "intelB[2,3]:10001"}
// 	returns map[int][]string{10000: ["intelA1", ..."intelA10"],
// 				 10001: ["intelB2", "intelB3"]}
func hostsByPort(addrPatterns []string, defaultPort int) (map[int][]string, error) {
	portHosts := make(map[int][]string)

	for _, p := range addrPatterns {
		var port int
		var err error
		var expanded string
		// separate port from compressed host string
		portHost := strings.Split(p, ":")

		switch len(portHost) {
		case 1:
			// no port specified, use default
			port = defaultPort
		case 2:
			port, err = strconv.Atoi(portHost[1])
			if err != nil {
				return nil, errors.WithMessage(err, "invalid port")
			}
		default:
			return nil, errors.Errorf("cannot parse %s", p)
		}

		if port == 0 {
			return nil, errors.New("invalid port")
		}

		expanded, err = hostlist.Expand(portHost[0])
		if err != nil {
			return nil, err
		}

		portHosts[port] = append(portHosts[port],
			strings.Split(expanded, ",")...)
	}

	return portHosts, nil
}

// flattenHostAddrs takes nodeset:port patterns and returns individual addresses
// after expanding nodesets and mapping to ports.
func flattenHostAddrs(addrPatterns string) ([]string, error) {
	var addrs []string
	var ports []int

	// expand any compressed nodesets for specific ports
	// example opts.HostList: intelA[1-10]:10000,intelB[2,3]:10001
	portHosts, err := hostsByPort(strings.Split(addrPatterns, ","), 0)
	if err != nil {
		return nil, err
	}

	for port := range portHosts {
		ports = append(ports, port)
	}
	sort.Ints(ports)

	// reconstruct slice of all "host:port" addresses from map
	for _, port := range ports {
		for _, host := range portHosts[port] {
			addrs = append(addrs, fmt.Sprintf("%s:%d", host, port))
		}
	}

	return addrs, nil
}

// rangeByPort takes host:port addresses and returns patterns after expanding
// nodesets and mapping to ports.
func rangeByPort(addrs []string) ([]string, error) {
	var patterns []string
	var ports []int

	// port should always be populated and will fail if default 0 used
	portHosts, err := hostsByPort(addrs, 0)
	if err != nil {
		return nil, errors.Errorf("failed to group hosts by port %v (%s)", addrs, err)
	}

	for port := range portHosts {
		ports = append(ports, port)
	}
	sort.Ints(ports)

	// reconstruct pattern of all successfully connected hosts per port
	for _, port := range ports {
		hosts := portHosts[port]
		compressed, err := hostlist.Compress(strings.Join(hosts, ","))
		if err != nil {
			return nil, errors.Errorf("failed to compress hostlist %v (%s)\n",
				hosts, err)
		}
		patterns = append(patterns, fmt.Sprintf("%s:%d", compressed, port))
	}

	return patterns, nil
}

// checkConns analyses connection results and returns summary compressed hostlists.
func checkConns(results client.ResultMap) (active []string, inactive map[string][]string, err error) {
	var addrs []string
	inactive = make(map[string][]string)

	// map keys always processed in order
	for addr := range results {
		addrs = append(addrs, addr)
	}
	sort.Strings(addrs)

	i := 0
	for _, addr := range addrs {
		if results[addr].Err != nil {
			// group failed conn attempts by error msg
			errMsg := results[addr].Err.Error()
			inactive[errMsg] = append(inactive[errMsg], addr)
			continue
		}
		addrs[i] = addr
		i++
	}
	addrs = addrs[:i]

	// group hosts by port from successful connection addresses
	active, err = rangeByPort(addrs)
	if err != nil {
		return nil, nil, errors.WithMessage(err, "reducing active addresses")
	}

	// group hosts by port and reassign to msg key for unsuccessful connection addresses
	for msg, addrs := range inactive {
		patterns, err := rangeByPort(addrs)
		if err != nil {
			return nil, nil, errors.WithMessage(err, "reducing inactive addresses")
		}
		inactive[msg] = patterns
	}

	return
}
