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

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestLibfabric_Provider_GetFabricInterfaces_Integrated(t *testing.T) {
	cleanup, err := Load()
	if err != nil {
		t.Skipf("libfabric not installed (%s)", err.Error())
	}
	defer cleanup()

	// Can't mock the underlying libfabric calls, but we can make sure it doesn't crash or
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

	t.Logf("\n%+v\n", result)
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
				Name:      "fi0_domain",
				OSName:    "fi0_domain",
				Providers: common.NewStringSet("provider_x"),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer common.ShowBufferOnFailure(t, buf)

			p := NewProvider(log)

			result, err := p.infoToFabricInterface(tc.in)

			common.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResult, result); diff != "" {
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
			expOut: "provider_x",
		},
		"badly formed": {
			in:     " ;ofi_rxm",
			expErr: errors.New("malformed"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			out, err := libFabricProviderListToExt(tc.in)

			common.CmpErr(t, tc.expErr, err)
			common.AssertEqual(t, tc.expOut, out, "")
		})
	}
}
