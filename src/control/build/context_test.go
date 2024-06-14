//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package build

import (
	"context"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"
	"google.golang.org/grpc/metadata"

	"github.com/daos-stack/daos/src/control/common/test"
)

func TestBuild_FromContext(t *testing.T) {
	for name, tc := range map[string]struct {
		ctx        context.Context
		expVerComp *VersionedComponent
		expErr     error
	}{
		"nil context": {
			expErr: errors.New("nil context"),
		},
		"no metadata in context": {
			ctx:    test.Context(t),
			expErr: ErrNoCtxMetadata,
		},
		"no component in context": {
			ctx: metadata.NewIncomingContext(test.Context(t), metadata.Pairs(
				DaosVersionHeader, "2.3.108",
			)),
			expErr: ErrNoCtxMetadata,
		},
		"no version in context": {
			ctx: metadata.NewIncomingContext(test.Context(t), metadata.Pairs(
				DaosComponentHeader, ComponentAgent.String(),
			)),
			expErr: ErrNoCtxMetadata,
		},
		"good version": {
			ctx: metadata.NewIncomingContext(test.Context(t), metadata.Pairs(
				DaosComponentHeader, ComponentAgent.String(),
				DaosVersionHeader, "2.3.108",
			)),
			expVerComp: &VersionedComponent{
				Component: ComponentAgent,
				Version:   MustNewVersion("2.3.108"),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotComp, gotErr := FromContext(tc.ctx)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expVerComp, gotComp); diff != "" {
				t.Fatalf("unexpected versioned component (-want, +got):\n%s", diff)
			}
		})
	}
}

func TestBuild_ToContext(t *testing.T) {
	for name, tc := range map[string]struct {
		parent    context.Context
		comp      Component
		verString string
		expMD     metadata.MD
		expErr    error
	}{
		"nil parent": {
			expErr: errors.New("nil context"),
		},
		"empty component": {
			parent:    test.Context(t),
			verString: "1.2.3",
			expErr:    errors.New("ComponentAny"),
		},
		"invalid version": {
			parent:    test.Context(t),
			comp:      ComponentAgent,
			verString: "x.y.z",
			expErr:    errors.New("invalid major version"),
		},
		"good component version": {
			parent:    test.Context(t),
			comp:      ComponentAgent,
			verString: "2.3.108",
			expMD: metadata.Pairs(
				DaosComponentHeader, ComponentAgent.String(),
				DaosVersionHeader, "2.3.108",
			),
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotCtx, gotErr := ToContext(tc.parent, tc.comp, tc.verString)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			gotMD, ok := metadata.FromOutgoingContext(gotCtx)
			if !ok {
				t.Fatal("metadata.FromOutgoingContext failed")
			}
			if diff := cmp.Diff(tc.expMD, gotMD); diff != "" {
				t.Fatalf("unexpected metadata (-want, +got):\n%s", diff)
			}
		})
	}
}
