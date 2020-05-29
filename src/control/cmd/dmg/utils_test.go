//
// (C) Copyright 2018-2020 Intel Corporation.
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
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	. "github.com/daos-stack/daos/src/control/client"
	. "github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/daos-stack/daos/src/control/system"
)

func TestFlattenAddrs(t *testing.T) {
	for name, tc := range map[string]struct {
		addrPatterns string
		expAddrs     string
		expErrMsg    string
	}{
		"single addr": {
			addrPatterns: "abc:10000",
			expAddrs:     "abc:10000",
		},
		"multiple nodesets": {
			addrPatterns: "abc[1-5]:10000,abc[6-10]:10001,def[1-3]:10000",
			expAddrs:     "abc10:10001,abc1:10000,abc2:10000,abc3:10000,abc4:10000,abc5:10000,abc6:10001,abc7:10001,abc8:10001,abc9:10001,def1:10000,def2:10000,def3:10000",
		},
		"multiple nodeset ranges": {
			addrPatterns: "abc[1-5,7-10],def[1-3,5,7-9]:10000",
			expAddrs:     "abc10:9999,abc1:9999,abc2:9999,abc3:9999,abc4:9999,abc5:9999,abc7:9999,abc8:9999,abc9:9999,def1:10000,def2:10000,def3:10000,def5:10000,def7:10000,def8:10000,def9:10000",
		},
		"multiple ip sets": {
			addrPatterns: "10.0.0.[1-5]:10000,10.0.0.[6-10]:10001,192.168.0.[1-3]:10000",
			expAddrs:     "10.0.0.10:10001,10.0.0.1:10000,10.0.0.2:10000,10.0.0.3:10000,10.0.0.4:10000,10.0.0.5:10000,10.0.0.6:10001,10.0.0.7:10001,10.0.0.8:10001,10.0.0.9:10001,192.168.0.1:10000,192.168.0.2:10000,192.168.0.3:10000",
		},
		"missing port": {
			addrPatterns: "localhost:10001,abc-[1-3]",
			expAddrs:     "abc-1:9999,abc-2:9999,abc-3:9999,localhost:10001",
		},
		"too many colons":     {"bad:addr:here", "", "address bad:addr:here: too many colons in address"},
		"no host":             {"valid:10001,:100", "", "invalid hostname \":100\""},
		"bad host number":     {"1001", "", "invalid hostname \"1001\""},
		"bad port alphabetic": {"foo:bar", "", "invalid port \"bar\""},
		"bad port empty":      {"foo:", "", "invalid port \"\""},
	} {
		t.Run(name, func(t *testing.T) {
			outAddrs, err := flattenHostAddrs(tc.addrPatterns, 9999)
			if err != nil {
				ExpectError(t, err, tc.expErrMsg, name)
				return
			}

			if diff := cmp.Diff(strings.Split(tc.expAddrs, ","), outAddrs); diff != "" {
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

// FIXME: Disable these until output formats stabilize. Or possibly remove them.
/*func TestCheckSprint(t *testing.T) {
	for name, tt := range map[string]struct {
		m   string
		out string
	}{
		"nvme scan with health": {
			fmt.Sprint(MockScanResp(MockCtrlrs, nil, nil, MockServers).StringHealthStats()),
			"1.2.3.4:10000\n\tNVMe controllers and namespaces detail with health statistics:\n\t\t" +
				"PCI:0000:81:00.0 Model:ABC FW:1.0.0 Socket:0 Capacity:97.66TB\n\t\t" +
				"Health Stats:\n\t\t\tTemperature:300K(27C)\n\t\t\tController Busy Time:0s\n\t\t\t" +
				"Power Cycles:99\n\t\t\tPower On Duration:9999h0m0s\n\t\t\tUnsafe Shutdowns:1\n\t\t\t" +
				"Media Errors:0\n\t\t\tError Log Entries:0\n\t\t\tCritical Warnings:\n\t\t\t\t" +
				"Temperature: OK\n\t\t\t\tAvailable Spare: OK\n\t\t\t\tDevice Reliability: OK\n\t\t\t\t" +
				"Read Only: OK\n\t\t\t\tVolatile Memory Backup: OK\n" +
				"1.2.3.5:10001\n\tNVMe controllers and namespaces detail with health statistics:\n\t\t" +
				"PCI:0000:81:00.0 Model:ABC FW:1.0.0 Socket:0 Capacity:97.66TB\n\t\t" +
				"Health Stats:\n\t\t\tTemperature:300K(27C)\n\t\t\tController Busy Time:0s\n\t\t\t" +
				"Power Cycles:99\n\t\t\tPower On Duration:9999h0m0s\n\t\t\tUnsafe Shutdowns:1\n\t\t\t" +
				"Media Errors:0\n\t\t\tError Log Entries:0\n\t\t\tCritical Warnings:\n\t\t\t\t" +
				"Temperature: OK\n\t\t\t\tAvailable Spare: OK\n\t\t\t\tDevice Reliability: OK\n\t\t\t\t" +
				"Read Only: OK\n\t\t\t\tVolatile Memory Backup: OK\n",
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
}*/

func mockHostGroups(t *testing.T) hostlist.HostGroups {
	groups := make(hostlist.HostGroups)

	for k, v := range map[string]string{
		"host1": "13GB (3 devices)/200TB (4 devices)",
		"host2": "13GB (3 devices)/200TB (4 devices)",
		"host3": "13GB (3 devices)/400TB (4 devices)",
		"host4": "13GB (3 devices)/400TB (4 devices)",
		"host5": "10GB (2 devices)/200TB (1 devices)",
	} {
		if err := groups.AddHost(v, k); err != nil {
			t.Fatal("couldn't add host group")
		}
	}

	return groups
}

func TestFormatHostGroups(t *testing.T) {
	for name, tt := range map[string]struct {
		g   hostlist.HostGroups
		out string
	}{
		"formatted results": {
			g:   mockHostGroups(t),
			out: "-----\nhost5\n-----\n10GB (2 devices)/200TB (1 devices)---------\nhost[1-2]\n---------\n13GB (3 devices)/200TB (4 devices)---------\nhost[3-4]\n---------\n13GB (3 devices)/400TB (4 devices)",
		},
	} {
		t.Run(name, func(t *testing.T) {
			buf := &bytes.Buffer{}
			if diff := cmp.Diff(tt.out, formatHostGroups(buf, tt.g)); diff != "" {
				t.Fatalf("unexpected output (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func mockRankGroups(t *testing.T) system.RankGroups {
	groups := make(system.RankGroups)

	rs1, err := system.NewRankSet("0-9,11-19")
	if err != nil {
		t.Fatal(err)
	}
	groups["foo/OK"] = rs1

	rs2, err := system.NewRankSet("10,20-299")
	if err != nil {
		t.Fatal(err)
	}
	groups["bar/BAD"] = rs2

	return groups
}

func TestTabulateRankGroups(t *testing.T) {
	mockColumnTitles := []string{"Ranks", "Action", "Result"}

	for name, tt := range map[string]struct {
		g         system.RankGroups
		cTitles   []string
		out       string
		expErrMsg string
	}{
		"formatted results": {
			g:       mockRankGroups(t),
			cTitles: mockColumnTitles,
			out: `
Ranks       Action Result 
-----       ------ ------ 
[10,20-299] bar    BAD    
[0-9,11-19] foo    OK     
`,
		},
		"column number mismatch": {
			g:         mockRankGroups(t),
			cTitles:   []string{"Ranks", "SCM", "NVME", "???"},
			expErrMsg: "unexpected summary format",
		},
		"too few columns": {
			g:         mockRankGroups(t),
			cTitles:   []string{"Ranks"},
			expErrMsg: "insufficient number of column titles",
		},
	} {
		t.Run(name, func(t *testing.T) {
			table, err := tabulateRankGroups(tt.g, tt.cTitles...)
			ExpectError(t, err, tt.expErrMsg, name)
			if diff := cmp.Diff(strings.TrimLeft(tt.out, "\n"), table); diff != "" {
				t.Fatalf("unexpected output (-want, +got):\n%s\n", diff)
			}
		})
	}
}
