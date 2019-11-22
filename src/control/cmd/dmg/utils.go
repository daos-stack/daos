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

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/client"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
)

type HostGroup struct {
	Port    string
	HostSet *hostlist.HostSet
}

type HostGroups []*HostGroup

func (hgs HostGroups) String() string {
	var ranges []string

	for _, hg := range hgs {
		ranges = append(ranges,
			fmt.Sprintf("%d:%s", hg.Port, hg.HostSet.RangedString()))
	}

	return strings.Join(ranges, ",")
}

type FailedHostGroups struct {
	Groups HostGroups
	Error  string
}

func (fhg FailedHostGroups) String() string {
	return fmt.Sprintf("%s: %s\n", fhg.Error, fhg.Groups)
}

// hostsByPort takes slice of address patterns and returns a map of host slice
// for each port (after expanding each port specific nodeset).
//
// e.g. for input patterns string[]{"intelA[1-10]:10000", "intelB[2,3]:10001"}
// 	returns map[int][]string{10000: ["intelA1", ..."intelA10"],
// 				 10001: ["intelB2", "intelB3"]}
func hostsByPort(addrPatterns []string, defaultPort int) (groups HostGroups, err error) {
	var ports []string
	portHosts := make(map[string]*hostlist.HostSet)

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
			return nil, errors.New("invalid port")
		}

		if _, exists := portHosts[port]; !exists {
			portHosts[port] = new(hostlist.HostSet)
		}

		if _, err = portHosts[port].Insert(hp[0]); err != nil {
			return
		}
	}

	for port := range portHosts {
		ports = append(ports, port)
	}
	sort.Strings(ports)

	for _, port := range ports {
		groups = append(groups,
			&HostGroup{Port: port, HostSet: portHosts[port]})
	}

	return
}

// flattenHostAddrs takes nodeset:port patterns and returns individual addresses
// after expanding nodesets and mapping to ports.
func flattenHostAddrs(addrPatterns string) (addrs []string, err error) {
	var portHostGroups HostGroups

	// expand any compressed nodesets for specific ports
	// example opts.HostList: intelA[1-10]:10000,intelB[2,3]:10001
	portHostGroups, err = hostsByPort(strings.Split(addrPatterns, ","), 0)
	if err != nil {
		return
	}

	// reconstruct slice of all "host:port" addresses from map
	for _, group := range portHostGroups {
		hosts := strings.Split(group.HostSet.DerangedString(), ",")
		for _, host := range hosts {
			addrs = append(addrs, fmt.Sprintf("%s:%s", host, group.Port))
		}
	}

	return
}

// checkConns analyses connection results and returns summary compressed hostlists.
func checkConns(results client.ResultMap) (active HostGroups, inactive []*FailedHostGroups, err error) {
	var addrs []string
	var msgs []string
	var groups HostGroups
	errHosts := make(map[string][]string)

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
			errHosts[errMsg] = append(errHosts[errMsg], addr)
			continue
		}
		addrs[i] = addr
		i++
	}
	addrs = addrs[:i]

	// group hosts by port from successful connection addresses
	active, err = hostsByPort(addrs, 0)
	if err != nil {
		return
	}

	for msg := range errHosts {
		msgs = append(msgs, msg)
	}
	sort.Strings(msgs)

	// group hosts by port and reassign to msg key for unsuccessful connection addresses
	for _, msg := range msgs {
		groups, err = hostsByPort(errHosts[msg], 0)
		if err != nil {
			return
		}
		inactive = append(inactive,
			&FailedHostGroups{Groups: groups, Error: msg})
	}

	return
}
