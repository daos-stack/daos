//
// (C) Copyright 2019 Intel Corporation.
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

package netdetect

import (
	"testing"
	"os"

	. "github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/log"
)

// TestParseTopology uses XML topology data to simulate real systems.
// hwloc will use this topology for queries instead of the local system
// running the test.
func TestParseTopology(t *testing.T) {

	tests := []struct {
		netDevsList []string
		topology    string
		expected    []string
	}{
		// boro-84 has two NUMA nodes, with eth0, eth1, ib0 on NUMA 0,
		// and no ib1 in the topology
		{[]string{""}, "testdata/boro-84.xml", []string{}},
		{[]string{"eth0"}, "testdata/boro-84.xml", []string{"eth0:0x000000ff,0xffff0000,0x00ffffff:0x00000001"}},
		{[]string{"eth1"}, "testdata/boro-84.xml", []string{"eth1:0x000000ff,0xffff0000,0x00ffffff:0x00000001"}},
		{[]string{"ib0"}, "testdata/boro-84.xml", []string{"ib0:0x000000ff,0xffff0000,0x00ffffff:0x00000001"}},
		{[]string{"eth0","eth1"}, "testdata/boro-84.xml", []string{"eth0:0x000000ff,0xffff0000,0x00ffffff:0x00000001","eth1:0x000000ff,0xffff0000,0x00ffffff:0x00000001"}},
		{[]string{"eth0","eth1","ib0"}, "testdata/boro-84.xml", []string{"ib0:0x000000ff,0xffff0000,0x00ffffff:0x00000001","eth0:0x000000ff,0xffff0000,0x00ffffff:0x00000001","eth1:0x000000ff,0xffff0000,0x00ffffff:0x00000001"}},
		{[]string{"ib0","eth0","ib1","eth1"}, "testdata/boro-84.xml", []string{"ib0:0x000000ff,0xffff0000,0x00ffffff:0x00000001","eth0:0x000000ff,0xffff0000,0x00ffffff:0x00000001","eth1:0x000000ff,0xffff0000,0x00ffffff:0x00000001"}},
		{[]string{"eth0","eth1","ib0"}, "testdata/boro-84.xml", []string{"ib0:0x000000ff,0xffff0000,0x00ffffff:0x00000001","eth0:0x000000ff,0xffff0000,0x00ffffff:0x00000001","eth1:0x000000ff,0xffff0000,0x00ffffff:0x00000001"}},
		{[]string{"eth0","eth1","ib0"}, "testdata/boro-84.xml", []string{"ib0:0x000000ff,0xffff0000,0x00ffffff:0x00000001","eth0:0x000000ff,0xffff0000,0x00ffffff:0x00000001","eth1:0x000000ff,0xffff0000,0x00ffffff:0x00000001"}},
		{[]string{"eth0","eth1","ib0"}, "testdata/boro-84.xml", []string{"ib0:0x000000ff,0xffff0000,0x00ffffff:0x00000001","eth0:0x000000ff,0xffff0000,0x00ffffff:0x00000001","eth1:0x000000ff,0xffff0000,0x00ffffff:0x00000001"}},
		// wolf-133 has two NUMA nodes, with eth0, eth1, ib0 on NUMA 0,
		// and ib1 on NUMA 1.  Notice that the cpuset and nodeset for ib1
		// reflect that they are on a different node
		{[]string{"eth0"}, "testdata/wolf-133.xml", []string{"eth0:0x000000ff,0xffff0000,0x00ffffff:0x00000001"}},
		{[]string{"eth1"}, "testdata/wolf-133.xml", []string{"eth1:0x000000ff,0xffff0000,0x00ffffff:0x00000001"}},
		{[]string{"ib0"}, "testdata/wolf-133.xml", []string{"ib0:0x000000ff,0xffff0000,0x00ffffff:0x00000001"}},
		{[]string{"ib1"}, "testdata/wolf-133.xml", []string{"ib1:0xffffff00,0x0000ffff,0xff000000:0x00000002"}},
		{[]string{"eth0","eth1"}, "testdata/wolf-133.xml", []string{"eth0:0x000000ff,0xffff0000,0x00ffffff:0x00000001","eth1:0x000000ff,0xffff0000,0x00ffffff:0x00000001"}},
		{[]string{"ib0", "eth0","eth1"}, "testdata/wolf-133.xml", []string{"ib0:0x000000ff,0xffff0000,0x00ffffff:0x00000001","eth0:0x000000ff,0xffff0000,0x00ffffff:0x00000001","eth1:0x000000ff,0xffff0000,0x00ffffff:0x00000001"}},
		{[]string{"ib0","eth0","ib1","eth1"}, "testdata/wolf-133.xml", []string{"ib0:0x000000ff,0xffff0000,0x00ffffff:0x00000001","eth0:0x000000ff,0xffff0000,0x00ffffff:0x00000001","eth1:0x000000ff,0xffff0000,0x00ffffff:0x00000001","ib1:0xffffff00,0x0000ffff,0xff000000:0x00000002"}},
}
	log.NewDefaultLogger(log.Debug, "", os.Stderr)
	for _, tt := range tests {
		_, err := os.Stat(tt.topology)
		AssertEqual(t, err, nil, "unable to load xmlTopology")
		os.Setenv("HWLOC_XMLFILE", tt.topology)
		netAdapterAffinity, err := GetAffinityForNetworkDevices(tt.netDevsList)
		if err != nil {
			log.Debugf("error from GetAffinityForNetworkDevices() %v", err)
		}
		os.Unsetenv("HWLOC_XMLFILE")
		AssertEqual(t, len(netAdapterAffinity), len(tt.expected), "number of devices expected vs found does not match")
		for j, i := range netAdapterAffinity {
			AssertEqual(t, i.String(), tt.expected[j],
				"unexpected mismatch with device and topology")
		}
	}
}
