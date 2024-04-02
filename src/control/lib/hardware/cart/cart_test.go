//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package cart

import (
	"context"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestCart_Provider_GetFabricInterfaces_Integrated(t *testing.T) {
	for name, tc := range map[string]struct {
		in     string
		expErr error
	}{
		"all": {},
		"all tcp": {
			in: "tcp",
		},
		"ofi+tcp": {
			in: "ofi+tcp",
		},
		"ucx+tcp": {
			in: "ucx+tcp",
		},
		"all verbs": {
			in: "verbs",
		},
		"ofi+verbs": {
			in: "ofi+verbs",
		},
		"ucx+rc": {
			in: "ucx+rc_v",
		},
		"garbage": {
			in:     "blahblahblah",
			expErr: daos.MercuryError,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			p := NewProvider(log)

			result, err := p.GetFabricInterfaces(test.Context(t), tc.in)

			test.CmpErr(t, tc.expErr, err)

			if err == nil {
				t.Logf("Results:\n%+v", result)
			}
		})
	}
}

func TestCart_Provider_GetFabricInterfaces(t *testing.T) {
	for name, tc := range map[string]struct {
		nilProvider     bool
		getContext      func(*testing.T) context.Context
		getProtocolInfo getProtocolFn
		wantProv        string
		expResult       *hardware.FabricInterfaceSet
		expErr          error
	}{
		"nil": {
			nilProvider: true,
			expErr:      errors.New("nil"),
		},
		"context canceled": {
			getContext: func(t *testing.T) context.Context {
				ctx, cancel := context.WithCancel(test.Context(t))
				cancel()
				return ctx
			},
			getProtocolInfo: func(log logging.Logger, provider string) ([]*crtFabricDevice, error) {
				time.Sleep(1 * time.Second) // a delay to ensure canceled context is noticed first
				return nil, errors.New("should not get here")
			},
			expErr: errors.New("context canceled"),
		},
		"getProtocolInfo fails": {
			getProtocolInfo: func(_ logging.Logger, _ string) ([]*crtFabricDevice, error) {
				return nil, errors.New("mock failure")
			},
			expErr: errors.New("fetching fabric interfaces: mock failure"),
		},
		"getProtocolInfo fails with specific provider": {
			getProtocolInfo: func(_ logging.Logger, provider string) ([]*crtFabricDevice, error) {
				if provider != "ofi+verbs" {
					return nil, errors.Errorf("FAIL: wrong provider %q passed in", provider)
				}
				return nil, errors.New("mock failure")
			},
			wantProv: "ofi+verbs",
			expErr:   errors.New("fetching fabric interfaces for provider \"ofi+verbs\": mock failure"),
		},
		"no fabric interfaces": {
			getProtocolInfo: func(_ logging.Logger, _ string) ([]*crtFabricDevice, error) {
				return []*crtFabricDevice{}, nil
			},
			expResult: hardware.NewFabricInterfaceSet(),
		},
		"success": {
			getProtocolInfo: func(_ logging.Logger, _ string) ([]*crtFabricDevice, error) {
				return []*crtFabricDevice{
					{
						Class:    classLibFabric,
						Protocol: "verbs",
						Device:   "test0",
					},
					{
						Class:    classUCX,
						Protocol: "rc_verbs",
						Device:   "test0:1",
					},
					{
						Class:    classUCX,
						Protocol: "ud_verbs",
						Device:   "test0:1",
					},
					{
						Class:    classLibFabric,
						Protocol: "tcp",
						Device:   "test1",
					},
					{
						Class:    classUCX,
						Protocol: "tcp",
						Device:   "test1",
					},
					{
						Class:    classNA,
						Protocol: "shm",
						Device:   "shm",
					},
				}, nil
			},
			expResult: hardware.NewFabricInterfaceSet(
				&hardware.FabricInterface{
					Name:   "test0",
					OSName: "test0",
					Providers: hardware.NewFabricProviderSet(
						&hardware.FabricProvider{
							Name: "ofi+verbs",
						},
					),
				},
				&hardware.FabricInterface{
					Name:   "test0:1",
					OSName: "test0",
					Providers: hardware.NewFabricProviderSet(
						&hardware.FabricProvider{
							Name: "ucx+rc_v",
						},
						&hardware.FabricProvider{
							Name: "ucx+ud_v",
						},
						&hardware.FabricProvider{
							Name: "ucx+rc",
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
				&hardware.FabricInterface{
					Name:   "test1",
					OSName: "test1",
					Providers: hardware.NewFabricProviderSet(
						&hardware.FabricProvider{
							Name:     "ofi+tcp",
							Priority: 1,
						},
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
				&hardware.FabricInterface{
					Name:   "shm",
					OSName: "shm",
					Providers: hardware.NewFabricProviderSet(
						&hardware.FabricProvider{
							Name:     "na+shm",
							Priority: 2,
						},
					),
				},
			),
		},
		"success with provider": {
			getProtocolInfo: func(_ logging.Logger, provider string) ([]*crtFabricDevice, error) {
				if provider != "ofi+tcp" {
					return nil, errors.Errorf("FAIL: wrong provider %q passed in", provider)
				}
				return []*crtFabricDevice{
					{
						Class:    classLibFabric,
						Protocol: "tcp",
						Device:   "test0",
					},
					{
						Class:    classLibFabric,
						Protocol: "tcp",
						Device:   "test1",
					},
				}, nil
			},
			wantProv: "ofi+tcp",
			expResult: hardware.NewFabricInterfaceSet(
				&hardware.FabricInterface{
					Name:   "test0",
					OSName: "test0",
					Providers: hardware.NewFabricProviderSet(
						&hardware.FabricProvider{
							Name: "ofi+tcp",
						},
					),
				},
				&hardware.FabricInterface{
					Name:   "test1",
					OSName: "test1",
					Providers: hardware.NewFabricProviderSet(
						&hardware.FabricProvider{
							Name: "ofi+tcp",
						},
					),
				},
			),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			var p *Provider
			if !tc.nilProvider {
				p = NewProvider(log)
				p.getProtocolInfo = tc.getProtocolInfo
			}

			if tc.getContext == nil {
				tc.getContext = test.Context
			}
			result, err := p.GetFabricInterfaces(tc.getContext(t), tc.wantProv)

			test.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResult, result, cmp.AllowUnexported(
				hardware.FabricInterfaceSet{},
				hardware.FabricProviderSet{},
			)); diff != "" {
				t.Fatalf("want-, got+:\n%s", diff)
			}
		})
	}
}
