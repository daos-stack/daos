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

package control

import (
	"fmt"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
)

type testTgtChooser struct {
	hostList []string
	toMS     bool
}

func (ttc *testTgtChooser) SetHostList(hl []string) {
	ttc.hostList = hl
}

func (ttc *testTgtChooser) getHostList() []string {
	return ttc.hostList
}

func (ttc *testTgtChooser) isMSRequest() bool {
	return ttc.toMS
}

func mockHostList(hosts ...string) []string {
	return hosts
}

func TestControl_ParseHostList(t *testing.T) {
	defaultCfg := DefaultConfig()

	for name, tc := range map[string]struct {
		in     []string
		expOut []string
		expErr error
	}{
		"host with too many :": {
			in:     mockHostList("foo::10"),
			expErr: errors.New("invalid host"),
		},
		"host with non-numeric port": {
			in:     mockHostList("foo:bar"),
			expErr: errors.New("invalid host"),
		},
		"host with just a port": {
			in:     mockHostList(":42"),
			expErr: errors.New("invalid host"),
		},
		"should append missing port": {
			in:     mockHostList("foo"),
			expOut: mockHostList(fmt.Sprintf("foo:%d", defaultCfg.ControlPort)),
		},
		"should append missing port (multiple)": {
			in: mockHostList("foo", "bar:4242", "baz"),
			expOut: mockHostList(
				"bar:4242",
				fmt.Sprintf("baz:%d", defaultCfg.ControlPort),
				fmt.Sprintf("foo:%d", defaultCfg.ControlPort),
			),
		},
		"should append missing port (ranges)": {
			in: mockHostList("foo-[1-4]", "bar[2-4]", "baz[8-9]:4242"),
			expOut: mockHostList(
				fmt.Sprintf("bar2:%d", defaultCfg.ControlPort),
				fmt.Sprintf("bar3:%d", defaultCfg.ControlPort),
				fmt.Sprintf("bar4:%d", defaultCfg.ControlPort),
				"baz8:4242",
				"baz9:4242",
				fmt.Sprintf("foo-1:%d", defaultCfg.ControlPort),
				fmt.Sprintf("foo-2:%d", defaultCfg.ControlPort),
				fmt.Sprintf("foo-3:%d", defaultCfg.ControlPort),
				fmt.Sprintf("foo-4:%d", defaultCfg.ControlPort),
			),
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotOut, gotErr := ParseHostList(tc.in, defaultCfg.ControlPort)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
			if diff := cmp.Diff(tc.expOut, gotOut); diff != "" {
				t.Fatalf("unexpected output (-want, +got):\n%s\n", diff)
			}
		})
	}

}

func TestControl_getRequestHosts(t *testing.T) {
	defaultCfg := DefaultConfig()
	defaultCfg.HostList = mockHostList("a", "b", "c")
	for i, host := range defaultCfg.HostList {
		defaultCfg.HostList[i] = fmt.Sprintf("%s:%d", host, defaultCfg.ControlPort)
	}

	for name, tc := range map[string]struct {
		cfg    *Config
		req    targetChooser
		expOut []string
		expErr error
	}{
		"bad hostname in config": {
			cfg: &Config{
				ControlPort: 42,
				HostList:    mockHostList("::"),
			},
			req:    &testTgtChooser{},
			expErr: errors.New("invalid host"),
		},
		"bad hostname in request": {
			cfg: defaultCfg,
			req: &testTgtChooser{
				hostList: mockHostList("::"),
			},
			expErr: errors.New("invalid host"),
		},
		"invalid config control port": {
			cfg: &Config{
				HostList: mockHostList("foo"),
			},
			req:    &testTgtChooser{},
			expErr: FaultConfigBadControlPort,
		},
		"empty config hostlist": {
			cfg:    &Config{},
			req:    &testTgtChooser{},
			expErr: FaultConfigEmptyHostList,
		},
		"default config; empty req list": {
			cfg:    defaultCfg,
			req:    &testTgtChooser{},
			expOut: mockHostList(defaultCfg.HostList...),
		},
		"default config; MS request; empty req list": {
			cfg: defaultCfg,
			req: &testTgtChooser{
				toMS: true,
			},
			expOut: mockHostList(defaultCfg.HostList[0]),
		},
		"default config; MS request; use req list": {
			cfg: defaultCfg,
			req: &testTgtChooser{
				hostList: defaultCfg.HostList[1:],
				toMS:     true,
			},
			expOut: mockHostList(defaultCfg.HostList[1]),
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotOut, gotErr := getRequestHosts(tc.cfg, tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
			if diff := cmp.Diff(tc.expOut, gotOut); diff != "" {
				t.Fatalf("unexpected output (-want, +got):\n%s\n", diff)
			}
		})
	}
}
