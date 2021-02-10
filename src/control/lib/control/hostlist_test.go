//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
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

func TestControl_getRequestHosts(t *testing.T) {
	defaultCfg := DefaultConfig()

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
