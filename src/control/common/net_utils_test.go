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

package common

import (
	"net"
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestUtils_HasPort(t *testing.T) {
	for name, tc := range map[string]struct {
		addr   string
		expRes bool
	}{
		"host has port": {"localhost:10001", true},
		"host no port":  {"localhost", false},
		"ip has port":   {"192.168.1.1:10001", true},
		"ip no port":    {"192.168.1.1", false},
	} {
		t.Run(name, func(t *testing.T) {
			AssertEqual(t, tc.expRes, HasPort(tc.addr), name)
		})
	}
}

func TestUtils_SplitPort(t *testing.T) {
	for name, tc := range map[string]struct {
		addr      string
		dPort     int
		expHost   string
		expPort   string
		expErrMsg string
	}{
		"host has port": {"localhost:10001", 10000, "localhost", "10001", ""},
		"host no port":  {"localhost", 10000, "localhost", "10000", ""},
		"ip has port":   {"192.168.1.1:10001", 10000, "192.168.1.1", "10001", ""},
		"ip no port":    {"192.168.1.1", 10000, "192.168.1.1", "10000", ""},
		"empty port":    {"192.168.1.1:", 10000, "", "", "invalid port \"\""},
		"bad port":      {"192.168.1.1:abc", 10000, "", "", "invalid port \"abc\""},
		"bad address": {"192.168.1.1:10001:", 10000, "", "",
			"address 192.168.1.1:10001:: too many colons in address"},
	} {
		t.Run(name, func(t *testing.T) {
			h, p, err := SplitPort(tc.addr, tc.dPort)
			ExpectError(t, err, tc.expErrMsg, name)
			if tc.expErrMsg != "" {
				return
			}

			AssertEqual(t, tc.expHost, h, name)
			AssertEqual(t, tc.expPort, p, name)
		})
	}
}

func TestCommon_CmpTcpAddr(t *testing.T) {
	testA := &net.TCPAddr{IP: net.IPv4(127, 0, 0, 1)}
	testB := &net.TCPAddr{IP: net.IPv4(127, 0, 0, 2)}
	testC := &net.TCPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 1}
	testD := &net.TCPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 1, Zone: "quack"}

	for name, tc := range map[string]struct {
		a      *net.TCPAddr
		b      *net.TCPAddr
		expRes bool
	}{
		"both nil": {
			a:      nil,
			b:      nil,
			expRes: true,
		},
		"a nil": {
			a: nil,
			b: testB,
		},
		"b nil": {
			a: testA,
			b: nil,
		},
		"same": {
			a:      testA,
			b:      testA,
			expRes: true,
		},
		"different IP": {
			a: testA,
			b: testB,
		},
		"different port": {
			a: testA,
			b: testC,
		},
		"different zone": {
			a: testA,
			b: testD,
		},
	} {
		t.Run(name, func(t *testing.T) {
			if diff := cmp.Diff(tc.expRes, CmpTcpAddr(tc.a, tc.b)); diff != "" {
				t.Fatalf("unexpected result (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestCommon_IsLocalAddr(t *testing.T) {
	local := &net.TCPAddr{IP: net.IPv4(127, 0, 0, 1)}
	remote := &net.TCPAddr{IP: net.IPv4(127, 127, 127, 127)}

	for name, tc := range map[string]struct {
		a      *net.TCPAddr
		expRes bool
	}{
		"nil": {},
		"local": {
			a:      local,
			expRes: true,
		},
		"remote": {
			a: remote,
		},
	} {
		t.Run(name, func(t *testing.T) {
			if diff := cmp.Diff(tc.expRes, IsLocalAddr(tc.a)); diff != "" {
				t.Fatalf("unexpected result (-want, +got):\n%s\n", diff)
			}
		})
	}
}
