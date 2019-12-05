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
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	. "github.com/daos-stack/daos/src/control/client"
	. "github.com/daos-stack/daos/src/control/common"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
)

func TestFlattenAddrs(t *testing.T) {
	for name, tc := range map[string]struct {
		addrPatterns string
		expAddrs     string
		expErr       error
	}{
		"single addr": {
			addrPatterns: "abc:10000",
			expAddrs:     "abc:10000",
		},
		"multiple nodesets": {
			addrPatterns: "abc[1-5]:10000,abc[6-10]:10001,def[1-3]:10000",
			expAddrs:     "abc1:10000,abc2:10000,abc3:10000,abc4:10000,abc5:10000,def1:10000,def2:10000,def3:10000,abc6:10001,abc7:10001,abc8:10001,abc9:10001,abc10:10001",
		},
		"multiple ip sets": {
			addrPatterns: "10.0.0.[1-5]:10000,10.0.0.[6-10]:10001,192.168.0.[1-3]:10000",
			expAddrs:     "10.0.0.1:10000,10.0.0.2:10000,10.0.0.3:10000,10.0.0.4:10000,10.0.0.5:10000,192.168.0.1:10000,192.168.0.2:10000,192.168.0.3:10000,10.0.0.6:10001,10.0.0.7:10001,10.0.0.8:10001,10.0.0.9:10001,10.0.0.10:10001",
		},
	} {
		t.Run(name, func(t *testing.T) {
			outAddrs, err := flattenHostAddrs(tc.addrPatterns)
			CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expAddrs, strings.Join(outAddrs, ",")); diff != "" {
				t.Fatalf("unexpected output (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestCheckConns(t *testing.T) {
	for name, tc := range map[string]struct {
		results ResultMap
		states  string
		expErr  error
	}{
		"no connections": {
			results: ResultMap{},
		},
		"single successful connection": {
			results: ResultMap{"abc:10000": ClientResult{"abc:10000", nil, nil}},
			states:  "abc:10000: connected\n",
		},
		"single failed connection": {
			results: ResultMap{"abc4:10000": ClientResult{"abc4:10000", nil, MockErr}},
			states:  "abc4:10000: unknown failure\n",
		},
		"multiple successful connections": {
			results: ResultMap{
				"foo.bar:10000": ClientResult{"foo.bar:10000", nil, nil},
				"foo.baz:10001": ClientResult{"foo.baz:10001", nil, nil},
			},
			states: "foo.bar:10000,foo.baz:10001: connected\n",
		},
		"multiple failed connections": {
			results: ResultMap{"abc4:10000": ClientResult{"abc4:10000", nil, MockErr}, "abc5:10001": ClientResult{"abc5:10001", nil, MockErr}},
			states:  "abc4:10000,abc5:10001: unknown failure\n",
		},
		"multiple failed connections with hostlist compress": {
			results: ResultMap{"abc4:10000": ClientResult{"abc4:10000", nil, MockErr}, "abc5:10000": ClientResult{"abc5:10000", nil, MockErr}},
			states:  "abc[4-5]:10000: unknown failure\n",
		},
		"failed and successful connections": {
			results: ResultMap{"abc4:10000": ClientResult{"abc4:10000", nil, MockErr}, "abc5:10001": ClientResult{"abc5:10001", nil, nil}},
			states:  "abc5:10001: connected\nabc4:10000: unknown failure\n",
		},
		"multiple connections with hostlist compress": {
			results: ResultMap{
				"bar4:10001": ClientResult{"bar4:10001", nil, nil},
				"bar5:10001": ClientResult{"bar5:10001", nil, nil},
				"bar3:10001": ClientResult{"bar3:10001", nil, nil},
				"bar6:10001": ClientResult{"bar6:10001", nil, nil},
				"bar2:10001": ClientResult{"bar2:10001", nil, errors.New("foobaz")},
				"bar7:10001": ClientResult{"bar7:10001", nil, errors.New("foobar")},
				"bar8:10001": ClientResult{"bar8:10001", nil, errors.New("foobar")},
				"bar9:10000": ClientResult{"bar9:10000", nil, errors.New("foobar")},
			},
			states: "           bar[3-6]:10001: connected\nbar9:10000,bar[7-8]:10001: foobar\n" +
				"               bar2:10001: foobaz\n",
		},
		"multiple connections with IP address compress": {
			results: ResultMap{
				"10.0.0.4:10001": ClientResult{"10.0.0.4:10001", nil, nil},
				"10.0.0.5:10001": ClientResult{"10.0.0.5:10001", nil, nil},
				"10.0.0.3:10001": ClientResult{"10.0.0.3:10001", nil, nil},
				"10.0.0.6:10001": ClientResult{"10.0.0.6:10001", nil, nil},
				"10.0.0.2:10001": ClientResult{"10.0.0.2:10001", nil, errors.New("foobaz")},
				"10.0.0.7:10001": ClientResult{"10.0.0.7:10001", nil, errors.New("foobar")},
				"10.0.0.8:10001": ClientResult{"10.0.0.8:10001", nil, errors.New("foobar")},
				"10.0.0.9:10000": ClientResult{"10.0.0.9:10000", nil, errors.New("foobar")},
			},
			states: "               10.0.0.[3-6]:10001: connected\n10.0.0.9:10000,10.0.0.[7-8]:10001: foobar\n" +
				"                   10.0.0.2:10001: foobaz\n",
		},
	} {
		t.Run(name, func(t *testing.T) {
			states, err := checkConns(tc.results)
			CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.states, states.String()); diff != "" {
				t.Fatalf("unexpected states (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestCheckSprint(t *testing.T) {
	for name, tt := range map[string]struct {
		m   string
		out string
	}{
		"nvme scan summary without health": {
			fmt.Sprint(MockScanResp(MockCtrlrs, nil, nil, MockServers, true)),
			"1.2.3.4:10000\n\tSummary:\n\t\tSCM: 0.00B total capacity over 0 modules (unprepared)\n\t\tNVMe: 97.66TB total capacity over 1 controller\n1.2.3.5:10001\n\tSummary:\n\t\tSCM: 0.00B total capacity over 0 modules (unprepared)\n\t\tNVMe: 97.66TB total capacity over 1 controller\n",
		},
		"nvme scan without health": {
			fmt.Sprint(MockScanResp(MockCtrlrs, nil, nil, MockServers, false)),
			"1.2.3.4:10000\n\tSCM Modules:\n\t\tnone\n\tNVMe controllers and namespaces:\n\t\tPCI Addr:0000:81:00.0 Serial:123ABC Model:ABC Fwrev:1.0.0 Socket:0\n\t\t\tNamespace: id:12345 capacity:97.66TB\n\tSummary:\n\t\tSCM: 0.00B total capacity over 0 modules (unprepared)\n\t\tNVMe: 97.66TB total capacity over 1 controller\n1.2.3.5:10001\n\tSCM Modules:\n\t\tnone\n\tNVMe controllers and namespaces:\n\t\tPCI Addr:0000:81:00.0 Serial:123ABC Model:ABC Fwrev:1.0.0 Socket:0\n\t\t\tNamespace: id:12345 capacity:97.66TB\n\tSummary:\n\t\tSCM: 0.00B total capacity over 0 modules (unprepared)\n\t\tNVMe: 97.66TB total capacity over 1 controller\n",
		},
		"nvme scan with health": {
			fmt.Sprint(MockScanResp(MockCtrlrs, nil, nil, MockServers, false).StringHealthStats()),
			"1.2.3.4:10000\n\tNVMe controllers and namespaces detail with health statistics:\n\t\tPCI Addr:0000:81:00.0 Serial:123ABC Model:ABC Fwrev:1.0.0 Socket:0\n\t\t\tNamespace: id:12345 capacity:97.66TB\n\t\tHealth Stats:\n\t\t\tTemperature:300K(27C)\n\t\t\tController Busy Time:0s\n\t\t\tPower Cycles:99\n\t\t\tPower On Duration:9999h0m0s\n\t\t\tUnsafe Shutdowns:1\n\t\t\tMedia Errors:0\n\t\t\tError Log Entries:0\n\t\t\tCritical Warnings:\n\t\t\t\tTemperature: OK\n\t\t\t\tAvailable Spare: OK\n\t\t\t\tDevice Reliability: OK\n\t\t\t\tRead Only: OK\n\t\t\t\tVolatile Memory Backup: OK\n1.2.3.5:10001\n\tNVMe controllers and namespaces detail with health statistics:\n\t\tPCI Addr:0000:81:00.0 Serial:123ABC Model:ABC Fwrev:1.0.0 Socket:0\n\t\t\tNamespace: id:12345 capacity:97.66TB\n\t\tHealth Stats:\n\t\t\tTemperature:300K(27C)\n\t\t\tController Busy Time:0s\n\t\t\tPower Cycles:99\n\t\t\tPower On Duration:9999h0m0s\n\t\t\tUnsafe Shutdowns:1\n\t\t\tMedia Errors:0\n\t\t\tError Log Entries:0\n\t\t\tCritical Warnings:\n\t\t\t\tTemperature: OK\n\t\t\t\tAvailable Spare: OK\n\t\t\t\tDevice Reliability: OK\n\t\t\t\tRead Only: OK\n\t\t\t\tVolatile Memory Backup: OK\n",
		},
		"scm scan summary with pmem namespaces": {
			fmt.Sprint(MockScanResp(nil, MockScmModules, MockScmNamespaces, MockServers, true)),
			"1.2.3.4:10000\n\tSummary:\n\t\tSCM: 2.90TB total capacity over 1 namespace\n\t\tNVMe: 0.00B total capacity over 0 controllers\n1.2.3.5:10001\n\tSummary:\n\t\tSCM: 2.90TB total capacity over 1 namespace\n\t\tNVMe: 0.00B total capacity over 0 controllers\n",
		},
		"scm scan with pmem namespaces": {
			fmt.Sprint(MockScanResp(nil, MockScmModules, MockScmNamespaces, MockServers, false)),
			"1.2.3.4:10000\n\tSCM Namespaces:\n\t\tDevice:pmem1 Socket:1 Capacity:2.90TB\n\tNVMe controllers and namespaces:\n\t\tnone\n\tSummary:\n\t\tSCM: 2.90TB total capacity over 1 namespace\n\t\tNVMe: 0.00B total capacity over 0 controllers\n1.2.3.5:10001\n\tSCM Namespaces:\n\t\tDevice:pmem1 Socket:1 Capacity:2.90TB\n\tNVMe controllers and namespaces:\n\t\tnone\n\tSummary:\n\t\tSCM: 2.90TB total capacity over 1 namespace\n\t\tNVMe: 0.00B total capacity over 0 controllers\n",
		},
		"scm scan summary without pmem namespaces": {
			fmt.Sprint(MockScanResp(nil, MockScmModules, nil, MockServers, true)),
			"1.2.3.4:10000\n\tSummary:\n\t\tSCM: 12.06KB total capacity over 1 module (unprepared)\n\t\tNVMe: 0.00B total capacity over 0 controllers\n1.2.3.5:10001\n\tSummary:\n\t\tSCM: 12.06KB total capacity over 1 module (unprepared)\n\t\tNVMe: 0.00B total capacity over 0 controllers\n",
		},
		"scm scan without pmem namespaces": {
			fmt.Sprint(MockScanResp(nil, MockScmModules, nil, MockServers, false)),
			"1.2.3.4:10000\n\tSCM Modules:\n\t\tPhysicalID:12345 Capacity:12.06KB Location:(socket:4 memctrlr:3 chan:1 pos:2)\n\tNVMe controllers and namespaces:\n\t\tnone\n\tSummary:\n\t\tSCM: 12.06KB total capacity over 1 module (unprepared)\n\t\tNVMe: 0.00B total capacity over 0 controllers\n1.2.3.5:10001\n\tSCM Modules:\n\t\tPhysicalID:12345 Capacity:12.06KB Location:(socket:4 memctrlr:3 chan:1 pos:2)\n\tNVMe controllers and namespaces:\n\t\tnone\n\tSummary:\n\t\tSCM: 12.06KB total capacity over 1 module (unprepared)\n\t\tNVMe: 0.00B total capacity over 0 controllers\n",
		},
		"scm mount scan": {
			NewClientScmMount(MockMounts, MockServers).String(),
			"1.2.3.4:10000:\n\tmntpoint:\"/mnt/daos\" \n\n1.2.3.5:10001:\n\tmntpoint:\"/mnt/daos\" \n\n",
		},
		"generic cmd results": {
			ResultMap{"1.2.3.4:10000": ClientResult{"1.2.3.4:10000", nil, MockErr}, "1.2.3.5:10001": ClientResult{"1.2.3.5:10001", nil, MockErr}}.String(),
			"1.2.3.4:10000:\n\terror: unknown failure\n1.2.3.5:10001:\n\terror: unknown failure\n",
		},
		"nvme operation results": {
			NewClientNvmeResults(
				[]*ctlpb.NvmeControllerResult{
					{
						Pciaddr: "0000:81:00.0",
						State: &ctlpb.ResponseState{
							Status: ctlpb.ResponseStatus_CTL_ERR_APP,
							Error:  "example application error",
						},
					},
				}, MockServers).String(),
			"1.2.3.4:10000:\n\tPCI Addr:0000:81:00.0 Status:CTL_ERR_APP Error:example application error\n\n1.2.3.5:10001:\n\tPCI Addr:0000:81:00.0 Status:CTL_ERR_APP Error:example application error\n\n",
		},
		"scm mountpoint operation results": {
			NewClientScmMountResults(
				[]*ctlpb.ScmMountResult{
					{
						Mntpoint: "/mnt/daos",
						State: &ctlpb.ResponseState{
							Status: ctlpb.ResponseStatus_CTL_ERR_APP,
							Error:  "example application error",
						},
					},
				}, MockServers).String(),
			"1.2.3.4:10000:\n\tMntpoint:/mnt/daos Status:CTL_ERR_APP Error:example application error\n\n1.2.3.5:10001:\n\tMntpoint:/mnt/daos Status:CTL_ERR_APP Error:example application error\n\n",
		},
	} {
		t.Run(name, func(t *testing.T) {
			if diff := cmp.Diff(tt.out, tt.m); diff != "" {
				t.Fatalf("unexpected output (-want, +got):\n%s\n", diff)
			}
		})
	}
}
