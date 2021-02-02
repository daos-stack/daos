//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package common

import (
	"fmt"
	"net"
	"strconv"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
)

// HasPort checks if addr specifies a port. This only works with IPv4
// addresses at the moment.
func HasPort(addr string) bool {
	return strings.Contains(addr, ":")
}

// SplitPort separates port from host in address and can apply default port if
// address doesn't contain one.
func SplitPort(addrPattern string, defaultPort int) (string, string, error) {
	host, port, err := net.SplitHostPort(addrPattern)
	if err != nil {
		if !strings.Contains(err.Error(), "missing port in address") {
			return "", "", err
		}

		return net.SplitHostPort(
			fmt.Sprintf("%s:%d", addrPattern, defaultPort))
	}

	if _, err := strconv.Atoi(port); err != nil {
		return "", "", errors.Errorf("invalid port %q", port)
	}

	return host, port, err
}

// CmpTCPAddr compares two *net.TCPAddr instances and returns
// true if they are equivalent, false otherwise.
func CmpTCPAddr(a, b *net.TCPAddr) bool {
	if a == nil && b == nil {
		return true
	}
	if a == nil || b == nil {
		return false
	}
	if !a.IP.Equal(b.IP) {
		return false
	}
	if a.Port != b.Port {
		return false
	}
	return a.Zone == b.Zone
}

// IsLocalAddr returns true if the supplied net.TCPAddr
// matches one of the local IP addresses, false otherwise.
func IsLocalAddr(testAddr *net.TCPAddr) bool {
	if testAddr == nil {
		return false
	}

	ifaceAddrs, err := net.InterfaceAddrs()
	if err != nil {
		return false
	}

	for _, ia := range ifaceAddrs {
		if in, ok := ia.(*net.IPNet); ok {
			if in.IP.Equal(testAddr.IP) {
				return true
			}
		}
	}

	return false
}

// LocalhostCtrlAddr returns a *net.TCPAddr representing
// the default control address on localhost.
func LocalhostCtrlAddr() *net.TCPAddr {
	return &net.TCPAddr{
		IP:   net.IPv4(127, 0, 0, 1),
		Port: build.DefaultControlPort,
	}
}

// ParseHostList validates and deduplicates the given list of host
// strings. Any hosts missing a port will have one added according
// to the defaultPort parameter.
func ParseHostList(in []string, defaultPort int) (out []string, err error) {
	if len(in) == 0 {
		return
	}

	var set *hostlist.HostSet
	set, err = hostlist.CreateSet(strings.Join(in, ","))
	if err != nil {
		return nil, err
	}
	out = strings.Split(set.DerangedString(), ",")

	for i, host := range out {
		hostPort := strings.Split(host, ":")
		switch len(hostPort) {
		case 1:
			out[i] = fmt.Sprintf("%s:%d", host, defaultPort)
		case 2:
			_, err = strconv.Atoi(hostPort[1])
		default:
			err = errors.New("host should conform to hostname[:port]")
		}

		if err != nil {
			return nil, errors.Wrapf(err, "invalid host %q", host)
		}
	}

	return
}
