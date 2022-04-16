//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package ucx

import (
	"context"
	"fmt"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestUCX_Provider_GetFabricInterfaces_Integrated(t *testing.T) {
	cleanup, err := Load()
	if err != nil {
		t.Skipf("can't load lib (%s)", err.Error())
	}
	defer cleanup()

	// Can't mock the underlying UCX calls, but we can make sure it doesn't crash or
	// error on the normal happy path.

	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	p := NewProvider(log)

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	result, err := p.GetFabricInterfaces(ctx)

	if err != nil {
		t.Fatal(err.Error())
	}

	fmt.Printf("FabricInterfaceSet:\n%s\n", result)
}

func TestUCX_Provider_getProviderSet(t *testing.T) {
	for name, tc := range map[string]struct {
		in     string
		expSet common.StringSet
	}{
		"dc": {
			in:     "dc_mlx5",
			expSet: common.NewStringSet("ucx+dc_x", "ucx+dc"),
		},
		"tcp": {
			in:     "tcp",
			expSet: common.NewStringSet("ucx+tcp"),
		},
		"add generic rc": {
			in:     "rc_verbs",
			expSet: common.NewStringSet("ucx+rc_v", "ucx+rc"),
		},
		"add generic ud": {
			in:     "ud_mlx5",
			expSet: common.NewStringSet("ucx+ud_x", "ucx+ud"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

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
			common.AssertEqual(t, tc.exp, transportToDAOSProvider(tc.in), "")
		})
	}
}
