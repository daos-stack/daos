//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package ucx

import (
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestUCX_Provider_getProviderSet(t *testing.T) {
	for name, tc := range map[string]struct {
		in     string
		expSet common.StringSet
	}{
		"dc": {
			in:     "dc_mlx5",
			expSet: common.NewStringSet("ucx+dc_x", "ucx+dc", "ucx+all"),
		},
		"tcp": {
			in:     "tcp",
			expSet: common.NewStringSet("ucx+tcp", "ucx+all"),
		},
		"add generic rc": {
			in:     "rc_verbs",
			expSet: common.NewStringSet("ucx+rc_v", "ucx+rc", "ucx+all"),
		},
		"add generic ud": {
			in:     "ud_mlx5",
			expSet: common.NewStringSet("ucx+ud_x", "ucx+ud", "ucx+all"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			p := NewProvider(log)

			set := p.getProviderSet(tc.in)

			if diff := cmp.Diff(tc.expSet, set); diff != "" {
				t.Fatalf("(-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestUCX_transportToDAOSProvider(t *testing.T) {
	for name, tc := range map[string]struct {
		in  string
		exp string
	}{
		"rc_verbs": {
			in:  "rc_verbs",
			exp: "ucx+rc_v",
		},
		"rc_mlx5": {
			in:  "rc_mlx5",
			exp: "ucx+rc_x",
		},
		"ud_verbs": {
			in:  "ud_verbs",
			exp: "ucx+ud_v",
		},
		"ud_mlx5": {
			in:  "ud_mlx5",
			exp: "ucx+ud_x",
		},
		"dc_mlx5": {
			in:  "dc_mlx5",
			exp: "ucx+dc_x",
		},
		"dc": {
			in:  "dc",
			exp: "ucx+dc",
		},
		"tcp": {
			in:  "tcp",
			exp: "ucx+tcp",
		},
		"rc": {
			in:  "rc",
			exp: "ucx+rc",
		},
		"ud": {
			in:  "ud",
			exp: "ucx+ud",
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.AssertEqual(t, tc.exp, transportToDAOSProvider(tc.in), "")
		})
	}
}

func TestUCX_getExternalName(t *testing.T) {
	for name, tc := range map[string]struct {
		devName   string
		devComp   string
		allDevs   []string
		expResult string
	}{
		"single IB device": {
			devName:   "d1",
			devComp:   compInfiniband,
			expResult: "d1",
		},
		"multiple IB devices": {
			devName:   "d1",
			devComp:   compInfiniband,
			allDevs:   []string{"d0"},
			expResult: "d1,d0",
		},
		"IB duplicates ignored": {
			devName:   "d1",
			devComp:   compInfiniband,
			allDevs:   []string{"d0", "d1", "d2"},
			expResult: "d1,d0,d2",
		},
		"single TCP device": {
			devName:   "d1",
			devComp:   compTCP,
			expResult: "d1",
		},
		"multiple TCP devices": {
			devName:   "d1",
			devComp:   compTCP,
			allDevs:   []string{"d0"},
			expResult: "d1",
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := getExternalName(tc.devComp, tc.devName, tc.allDevs)

			test.AssertEqual(t, tc.expResult, result, "")
		})
	}
}
