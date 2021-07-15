//
// (C) Copyright 2018-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"bytes"
	"encoding/csv"
	"fmt"
	"sort"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/dustin/go-humanize"
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

func parseUint64Array(in string) (out []uint64, err error) {
	arr, err := csv.NewReader(strings.NewReader(in)).Read()
	if err != nil {
		return
	}

	out = make([]uint64, len(arr))
	for idx, elemStr := range arr {
		out[idx], err = humanize.ParseBytes(elemStr)
		if err != nil {
			return
		}
	}
	return
}
