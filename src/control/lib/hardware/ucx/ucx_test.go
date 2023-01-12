//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package ucx

import (
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestUCX_Provider_getProviderSet(t *testing.T) {
	for name, tc := range map[string]struct {
		in     string
		expSet *hardware.FabricProviderSet
	}{
		"dc": {
			in: "dc_mlx5",
			expSet: hardware.NewFabricProviderSet(
				&hardware.FabricProvider{
					Name: "ucx+dc_x",
				},
				&hardware.FabricProvider{
					Name: "ucx+dc",
				},
				&hardware.FabricProvider{
					Name:     "ucx+all",
					Priority: catchallPriority,
				},
			),
		},
		"tcp": {
			in: "tcp",
			expSet: hardware.NewFabricProviderSet(
				&hardware.FabricProvider{
					Name:     "ucx+tcp",
					Priority: tcpPriority,
				},
				&hardware.FabricProvider{
					Name:     "ucx+all",
					Priority: catchallPriority,
				},
			),
		},
		"add generic rc": {
			in: "rc_verbs",
			expSet: hardware.NewFabricProviderSet(
				&hardware.FabricProvider{
					Name: "ucx+rc_v",
				},
				&hardware.FabricProvider{
					Name: "ucx+rc",
				},
				&hardware.FabricProvider{
					Name:     "ucx+all",
					Priority: catchallPriority,
				},
			),
		},
		"add generic ud": {
			in: "ud_mlx5",
			expSet: hardware.NewFabricProviderSet(
				&hardware.FabricProvider{
					Name: "ucx+ud_x",
				},
				&hardware.FabricProvider{
					Name: "ucx+ud",
				},
				&hardware.FabricProvider{
					Name:     "ucx+all",
					Priority: catchallPriority,
				},
			),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			p := NewProvider(log)

			set := p.getProviderSet(tc.in)

			if diff := cmp.Diff(tc.expSet, set, cmp.AllowUnexported(hardware.FabricProviderSet{})); diff != "" {
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
