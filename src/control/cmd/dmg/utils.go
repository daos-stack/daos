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
	"bytes"
	"fmt"
	"sort"
	"strconv"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/client"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
)

// splitPort separates port from compressed host string
func splitPort(addrPattern string, defaultPort int) (string, string, error) {
	var port string
	hp := strings.Split(addrPattern, ":")

	switch len(hp) {
	case 1:
		// no port specified, use default
		port = strconv.Itoa(defaultPort)
	case 2:
		port = hp[1]
		if port == "" {
			return "", "", errors.Errorf("invalid port %q", port)
		}
		if _, err := strconv.Atoi(port); err != nil {
			return "", "", errors.WithMessagef(err, "cannot parse %q",
				addrPattern)
		}
	default:
		return "", "", errors.Errorf("cannot parse %q", addrPattern)
	}

	if hp[0] == "" {
		return "", "", errors.Errorf("invalid host %q", hp[0])
	}

	return hp[0], port, nil
}

// hostsByPort takes slice of address patterns and returns a HostGroups mapping
// of ports to HostSets.
func hostsByPort(addrPatterns string, defaultPort int) (portHosts hostlist.HostGroups, err error) {
	var hostSet, port string
	var inHostSet *hostlist.HostList
	portHosts = make(hostlist.HostGroups)

	inHostSet, err = hostlist.Create(addrPatterns)
	if err != nil {
		return
	}

	for _, ptn := range strings.Split(inHostSet.DerangedString(), ",") {
		hostSet, port, err = splitPort(ptn, defaultPort)
		if err != nil {
			return
		}

		if err = portHosts.AddHost(port, hostSet); err != nil {
			return
		}
	}

	return
}

// flattenHostAddrs takes nodeset:port patterns and returns individual addresses
// after expanding nodesets and mapping to ports.
func flattenHostAddrs(addrPatterns string, defaultPort int) (addrs []string, err error) {
	var portHosts hostlist.HostGroups

	// expand any compressed nodesets for specific ports, should fail if no
	// port in pattern.
	portHosts, err = hostsByPort(addrPatterns, defaultPort)
	if err != nil {
		return
	}

	// reconstruct slice of all "host:port" addresses from map
	for _, port := range portHosts.Keys() {
		hosts := strings.Split(portHosts[port].DerangedString(), ",")
		for _, host := range hosts {
			addrs = append(addrs, fmt.Sprintf("%s:%s", host, port))
		}
	}

	sort.Strings(addrs)

	return
}

// checkConns analyses connection results and returns summary compressed active
// and inactive hostlists (but disregards connection port).
func checkConns(results client.ResultMap) (connStates hostlist.HostGroups, err error) {
	connStates = make(hostlist.HostGroups)

	for addr := range results {
		resultErr := results[addr].Err
		if resultErr != nil {
			if err = connStates.AddHost(resultErr.Error(), addr); err != nil {
				return
			}
			continue
		}
		if err = connStates.AddHost("connected", addr); err != nil {
			return
		}
	}

	return
}

// formatHostGroupResults adds group title header per group results.
func formatHostGroupResults(buf *bytes.Buffer, groups hostlist.HostGroups) string {
	for _, res := range groups.Keys() {
		hostset := groups[res].RangedString()
		lineBreak := strings.Repeat("-", len(hostset))
		fmt.Fprintf(buf, "%s\n%s\n%s\n%s", lineBreak, hostset, lineBreak, res)
	}

	return buf.String()
}

// groupSummaryTable is a helper function that prints hostgroups with 2 column entries
func groupSummaryTable(firstTitle, secondTitle, thirdTitle string, groups hostlist.HostGroups) (string, error) {
	formatter := NewTableFormatter([]string{firstTitle, secondTitle, thirdTitle})
	var table []TableRow

	for _, result := range groups.Keys() {
		row := TableRow{firstTitle: groups[result].RangedString()}

		summary := strings.Split(result, rowFieldSep)
		if len(summary) != 2 {
			return "", errors.New("unexpected summary format")
		}
		row[secondTitle] = summary[0]
		row[thirdTitle] = summary[1]

		table = append(table, row)
	}

	return formatter.Format(table), nil
}
