//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package libfabric

import (
	"context"
	"errors"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestLibfabric_Provider_GetFabricInterfaces_Integrated(t *testing.T) {
	cleanup, err := Load()
	if err != nil {
		t.Skipf("libfabric not installed (%s)", err.Error())
	}
	defer cleanup()

	for name, tc := range map[string]struct {
		provider string
	}{
		"all": {},
		"tcp": {
			provider: "ofi+tcp",
		},
		"not valid": {
			provider: "fake",
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			// Can't mock the underlying libfabric calls, but we can make sure it doesn't crash or
			// error on the normal happy path.

			p := NewProvider(log)

			ctx, cancel := context.WithTimeout(test.Context(t), 10*time.Second)
			defer cancel()

			result, err := p.GetFabricInterfaces(ctx, tc.provider)
			if err != nil {
				t.Fatal(err.Error())
			}

			t.Logf("\nwith %s:\n%+v\n", name, result)
		})
	}
}

type mockInfo struct {
	domainNameReturn     string
	fabricProviderReturn string
}

func (m *mockInfo) domainName() string {
	return m.domainNameReturn
}

func (m *mockInfo) fabricProvider() string {
	return m.fabricProviderReturn
}

func TestLibfabric_Provider_fiInfoToFabricInterfaceSet(t *testing.T) {
	testPriority := 5
	for name, tc := range map[string]struct {
		in        info
		expResult *hardware.FabricInterface
		expErr    error
	}{
		"nil": {
			expErr: errors.New("nil"),
		},
		"no domain": {
			in: &mockInfo{
				fabricProviderReturn: "provider_x",
			},
			expErr: errors.New("domain name"),
		},
		"no provider": {
			in: &mockInfo{
				domainNameReturn: "fi0_domain",
			},
			expErr: errors.New("provider"),
		},
		"success": {
			in: &mockInfo{
				domainNameReturn:     "fi0_domain",
				fabricProviderReturn: "provider_x",
			},
			expResult: &hardware.FabricInterface{
				Name:   "fi0_domain",
				OSName: "fi0_domain",
				Providers: hardware.NewFabricProviderSet(
					&hardware.FabricProvider{
						Name:     "ofi+provider_x",
						Priority: testPriority,
					},
				),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			p := NewProvider(log)

			result, err := p.infoToFabricInterface(tc.in, testPriority)

			test.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResult, result, cmp.AllowUnexported(hardware.FabricProviderSet{})); diff != "" {
				t.Errorf("(-want, +got)\n%s\n", diff)
			}
		})
	}
}

func TestLibfabric_libFabricProviderListToExt(t *testing.T) {
	for name, tc := range map[string]struct {
		in     string
		expOut string
		expErr error
	}{
		"empty": {
			expErr: errors.New("empty"),
		},
		"all whitespace": {
			in:     "\t\n ",
			expErr: errors.New("empty"),
		},
		"sockets": {
			in:     "sockets",
			expOut: "ofi+sockets",
		},
		"tcp": {
			in:     "tcp",
			expOut: "ofi+tcp",
		},
		"tcp with ofi_rxm": {
			in:     "tcp;ofi_rxm",
			expOut: "ofi+tcp;ofi_rxm",
		},
		"verbs": {
			in:     "verbs",
			expOut: "ofi+verbs",
		},
		"verbs with ofi_rxm": {
			in:     "verbs;ofi_rxm",
			expOut: "ofi+verbs;ofi_rxm",
		},
		"psm2": {
			in:     "psm2",
			expOut: "ofi+psm2",
		},
		"gni": {
			in:     "gni",
			expOut: "ofi+gni",
		},
		"cxi": {
			in:     "cxi",
			expOut: "ofi+cxi",
		},
		"unknown": {
			in:     "provider_x",
			expOut: "ofi+provider_x",
		},
		"badly formed": {
			in:     " ;ofi_rxm",
			expErr: errors.New("malformed"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			out, err := libFabricProviderListToExt(tc.in)

			test.CmpErr(t, tc.expErr, err)
			test.AssertEqual(t, tc.expOut, out, "")
		})
	}
}

func TestLibfabric_extProviderToLibFabric(t *testing.T) {
	for name, tc := range map[string]struct {
		in     string
		expOut string
	}{
		"empty": {},
		"no ofi prefix": {
			in:     "tcp",
			expOut: "tcp",
		},
		"ofi prefix": {
			in:     "ofi+verbs",
			expOut: "verbs",
		},
		"some other prefix": {
			in:     "ucx+tcp",
			expOut: "ucx+tcp",
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.AssertEqual(t, tc.expOut, extProviderToLibFabric(tc.in), "")
		})
	}
}
