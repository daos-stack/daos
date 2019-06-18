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
	"net"
	"strconv"
	"testing"
)

func TestHasPort(t *testing.T) {
	tests := []struct {
		addr        string
		expectedHas bool
	}{
		{"localhost", false},
		{"localhost:10000", true},
		{"192.168.1.1", false},
		{"192.168.1.1:10000", true},
	}

	for _, test := range tests {
		if has := hasPort(test.addr); has != test.expectedHas {
			t.Errorf("hasPort(%q) = %v", test.addr, has)
		}
	}
}

func TestCheckMgmtSvcReplica(t *testing.T) {
	defaultPort := strconv.Itoa(
		mockConfigFromFile(t, defaultMockExt(), defaultConfig).Port)

	tests := []struct {
		self              string
		accessPoints      []string
		expectedIsReplica bool
		expectedBootstrap bool
		expectedErr       bool
	}{
		// Specified self cases
		{"localhost:" + defaultPort, []string{"localhost:" + defaultPort}, true, true, false},
		{"localhost:" + defaultPort, []string{"localhost"}, true, true, false},
		{"localhost:" + defaultPort, []string{"192.168.1.111", "localhost"}, true, false, false},
		{"localhost:" + defaultPort, []string{"192.168.1.111", "192.168.1.112"}, false, false, false},

		// Unspecified self cases
		{"0.0.0.0:" + defaultPort, []string{"localhost:" + defaultPort}, true, true, false},
		{"0.0.0.0:" + defaultPort, []string{"localhost"}, true, true, false},
		{"0.0.0.0:" + defaultPort, []string{"192.168.1.111", "localhost"}, true, false, false},
		{"0.0.0.0:" + defaultPort, []string{"192.168.1.111", "192.168.1.112"}, false, false, false},
	}

	oldGetInterfaceAddrs := getInterfaceAddrs
	getInterfaceAddrs = func() ([]net.Addr, error) {
		// Sample interface addresses
		ifAddrs := []net.Addr{
			&net.IPNet{
				IP:   net.IP{0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xff, 0xff, 0x7f, 0x0, 0x0, 0x1},
				Mask: net.IPMask{0xff, 0x0, 0x0, 0x0},
			}, // 127.0.0.1/8
			&net.IPNet{
				IP:   net.IP{0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1},
				Mask: net.IPMask{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
			}, // ::1/128
			&net.IPNet{
				IP:   net.IP{0xfe, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1},
				Mask: net.IPMask{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0},
			}, // fe80::1/64
			&net.IPNet{
				IP:   net.IP{0xfe, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xc, 0x58, 0xc, 0x69, 0xd0, 0xe3, 0xca, 0xdd},
				Mask: net.IPMask{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0},
			}, // fe80::c58:c69:d0e3:cadd/64
			&net.IPNet{
				IP:   net.IP{0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xff, 0xff, 0xc0, 0xa8, 0xde, 0x69},
				Mask: net.IPMask{0xff, 0xff, 0xff, 0x0},
			}, // 192.168.222.105/24
			&net.IPNet{
				IP:   net.IP{0xfe, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xd0, 0x21, 0xb5, 0xff, 0xfe, 0xb2, 0x3c, 0xc9},
				Mask: net.IPMask{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0},
			}, // fe80::d021:b5ff:feb2:3cc9/64
			&net.IPNet{
				IP:   net.IP{0xfe, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xef, 0x8e, 0xb4, 0x97, 0xc4, 0x32, 0x38, 0x6e},
				Mask: net.IPMask{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0},
			}, // fe80::ef8e:b497:c432:386e/64
			&net.IPNet{
				IP:   net.IP{0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xff, 0xff, 0xa, 0xd3, 0x37, 0x2},
				Mask: net.IPMask{0xff, 0xff, 0xff, 0x0},
			}, // 10.211.55.2/24
			&net.IPNet{
				IP:   net.IP{0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xff, 0xff, 0xa, 0x25, 0x81, 0x2},
				Mask: net.IPMask{0xff, 0xff, 0xff, 0x0},
			}, // 10.37.129.2/24
		}
		return ifAddrs, nil
	}
	defer func() { getInterfaceAddrs = oldGetInterfaceAddrs }()

	for i, test := range tests {
		ta, err := net.ResolveTCPAddr("tcp", test.self)
		if err != nil {
			t.Errorf("test.self: %v", err)
		}
		isReplica, bootstrap, err := checkMgmtSvcReplica(ta, test.accessPoints)
		if isReplica != test.expectedIsReplica ||
			bootstrap != test.expectedBootstrap ||
			(err != nil) != test.expectedErr {
			t.Errorf("test %d checkMgmtSvcReplica(%v, %v) = (%v, %v, %v), want (%v, %v, %v)",
				i, ta, test.accessPoints, isReplica, bootstrap, err != nil,
				test.expectedIsReplica, test.expectedBootstrap, test.expectedErr)
		}
	}
}
