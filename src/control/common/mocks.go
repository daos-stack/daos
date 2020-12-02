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
	"fmt"
	"net"
	"strconv"
	"strings"
)

var hostAddrs = make(map[int32]*net.TCPAddr)

// MockListPoolsResult mocks list pool results.
type MockListPoolsResult struct {
	Status int32
	Err    error
}

// GetIndex return suitable index value for auto generating mocks.
func GetIndex(varIdx ...int32) int32 {
	if len(varIdx) == 0 {
		varIdx = append(varIdx, 1)
	}

	return varIdx[0]
}

// MockUUID returns mock UUID values for use in tests.
func MockUUID(varIdx ...int32) string {
	idx := GetIndex(varIdx...)
	idxStr := strconv.Itoa(int(idx))

	return fmt.Sprintf("%s-%s-%s-%s-%s",
		strings.Repeat(idxStr, 8),
		strings.Repeat(idxStr, 4),
		strings.Repeat(idxStr, 4),
		strings.Repeat(idxStr, 4),
		strings.Repeat(idxStr, 12),
	)
}

// MockHostAddr returns mock tcp addresses for use in tests.
func MockHostAddr(varIdx ...int32) *net.TCPAddr {
	idx := GetIndex(varIdx...)

	if _, exists := hostAddrs[idx]; !exists {
		addr, err := net.ResolveTCPAddr("tcp", fmt.Sprintf("10.0.0.%d:10001", idx))
		if err != nil {
			panic(err)
		}
		hostAddrs[idx] = addr
	}

	return hostAddrs[idx]
}

// MockPCIAddr returns mock PCIAddr values for use in tests.
func MockPCIAddr(varIdx ...int32) string {
	idx := GetIndex(varIdx...)

	return fmt.Sprintf("0000:%02d:00.0", idx)
}

func MockPCIAddrs(num int) (addrs []string) {
	for i := 1; i < num+1; i++ {
		addrs = append(addrs, MockPCIAddr(int32(i)))
	}

	return
}
