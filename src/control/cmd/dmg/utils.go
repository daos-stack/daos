//
// (C) Copyright 2018-2021 Intel Corporation.
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
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
	"github.com/daos-stack/daos/src/control/system"
)

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
		hostSet, port, err = common.SplitPort(ptn, defaultPort)
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

// formatHostGroups adds group title header per group results.
func formatHostGroups(buf *bytes.Buffer, groups hostlist.HostGroups) string {
	for _, res := range groups.Keys() {
		hostset := groups[res].RangedString()
		lineBreak := strings.Repeat("-", len(hostset))
		fmt.Fprintf(buf, "%s\n%s\n%s\n%s", lineBreak, hostset, lineBreak, res)
	}

	return buf.String()
}

// tabulateRankGroups is a helper function representing rankgroups in a tabular form.
func tabulateRankGroups(groups system.RankGroups, titles ...string) (string, error) {
	if len(titles) < 2 {
		return "", errors.New("insufficient number of column titles")
	}
	groupTitle := titles[0]
	columnTitles := titles[1:]

	formatter := txtfmt.NewTableFormatter(titles...)
	var table []txtfmt.TableRow

	for _, result := range groups.Keys() {
		row := txtfmt.TableRow{groupTitle: groups[result].RangedString()}

		summary := strings.Split(result, rowFieldSep)
		if len(summary) != len(columnTitles) {
			return "", errors.New("unexpected summary format")
		}
		for i, title := range columnTitles {
			row[title] = summary[i]
		}

		table = append(table, row)
	}

	return formatter.Format(table), nil
}

// errIncompatFlags accepts a base flag and a set of incompatible
// flags in order to generate a user-comprehensible error when an
// incompatible set of parameters was supplied.
func errIncompatFlags(key string, incompat ...string) error {
	base := fmt.Sprintf("--%s may not be mixed", key)
	if len(incompat) == 0 {
		// kind of a weird error but better than nothing
		return errors.New(base)
	}

	return errors.Errorf("%s with --%s", base, strings.Join(incompat, " or --"))
}
