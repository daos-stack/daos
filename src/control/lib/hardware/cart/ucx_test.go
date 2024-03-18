//
// (C) Copyright 2022-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package cart

import (
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/hardware"
)

func TestCart_getOSNameFromUCXDevice(t *testing.T) {
	for name, tc := range map[string]struct {
		in        string
		expResult string
	}{
		"empty": {},
		"no port": {
			in:        "dev0_1",
			expResult: "dev0_1",
		},
		"port": {
			in:        "dev0_1:1",
			expResult: "dev0_1",
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.AssertEqual(t, tc.expResult, getOSNameFromUCXDevice(tc.in), "")
		})
	}
}

func TestCart_getProviderSetFromUCXTransport(t *testing.T) {
	for name, tc := range map[string]struct {
		in     string
		expSet *hardware.FabricProviderSet
	}{
		"empty": {
			expSet: hardware.NewFabricProviderSet(),
		},
		"custom": {
			in: "custom",
			expSet: hardware.NewFabricProviderSet(
				&hardware.FabricProvider{
					Name: "ucx+custom",
				},
				&hardware.FabricProvider{
					Name:     "ucx+all",
					Priority: ucxCatchallPriority,
				},
			),
		},
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
					Priority: ucxCatchallPriority,
				},
			),
		},
		"tcp": {
			in: "tcp",
			expSet: hardware.NewFabricProviderSet(
				&hardware.FabricProvider{
					Name:     "ucx+tcp",
					Priority: ucxTCPPriority,
				},
				&hardware.FabricProvider{
					Name:     "ucx+all",
					Priority: ucxCatchallPriority,
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
					Priority: ucxCatchallPriority,
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
					Priority: ucxCatchallPriority,
				},
			),
		},
	} {
		t.Run(name, func(t *testing.T) {
			set := getProviderSetFromUCXTransport(tc.in)

			if diff := cmp.Diff(tc.expSet, set, cmp.AllowUnexported(hardware.FabricProviderSet{})); diff != "" {
				t.Fatalf("(-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestCart_ucxTransportToDAOSProvider(t *testing.T) {
	for name, tc := range map[string]struct {
		in  string
		exp string
	}{
		"custom": {
			in:  "custom",
			exp: "ucx+custom",
		},
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
			test.AssertEqual(t, tc.exp, ucxTransportToDAOSProvider(tc.in), "")
		})
	}
}
