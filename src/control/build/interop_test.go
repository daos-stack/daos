//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package build_test

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
)

func testComponent(t *testing.T, comp string, ver string) *build.VersionedComponent {
	c, err := build.NewVersionedComponent(build.Component(comp), ver)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	return c
}

func TestBuild_NewVersionedComponent(t *testing.T) {
	for name, tc := range map[string]struct {
		component string
		version   string
		expVC     *build.VersionedComponent
		expErr    error
	}{
		"bad component": {
			component: "no way josé",
			version:   "1.0.0",
			expErr:    errors.New("invalid component"),
		},
		"bad version": {
			component: "server",
			version:   "",
			expErr:    errors.New("invalid version"),
		},
		"any component": {
			component: "",
			version:   "1.0.0",
			expVC:     testComponent(t, "", "1.0.0"),
		},
		"valid component/version": {
			component: "server",
			version:   "1.2.3",
			expVC:     testComponent(t, "server", "1.2.3"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			vc, err := build.NewVersionedComponent(build.Component(tc.component), tc.version)
			common.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expVC, vc); diff != "" {
				t.Fatalf("unexpected versioned component (-want +got):\n%s", diff)
			}
		})
	}
}

func TestBuild_CheckCompatibility(t *testing.T) {
	for name, tc := range map[string]struct {
		a          *build.VersionedComponent
		b          *build.VersionedComponent
		customRule *build.InteropRule
		expErr     error
	}{
		// --- Product-specific version/component interop checks ---
		"server/server not patch compatible": {
			a:      testComponent(t, "server", "1.0.0"),
			b:      testComponent(t, "server", "1.1.0"),
			expErr: errors.New("incompatible components"),
		},
		"server/server patch compatible": {
			a: testComponent(t, "server", "1.0.0"),
			b: testComponent(t, "server", "1.0.1"),
		},
		"2.0 not backwards compatible": {
			a:      testComponent(t, "", "2.0.0"),
			b:      testComponent(t, "", "1.2.0"),
			expErr: errors.New("incompatible components"),
		},
		// --- Unit testing ---
		"nil a": {
			a:      nil,
			b:      testComponent(t, "server", "2.0.0"),
			expErr: errors.Errorf("nil components"),
		},
		"nil b": {
			a:      testComponent(t, "server", "2.0.0"),
			b:      nil,
			expErr: errors.Errorf("nil components"),
		},
		"general: client/server major delta too large": {
			a:      testComponent(t, "admin", "1.0.0"),
			b:      testComponent(t, "server", "2.0.0"),
			expErr: errors.New("incompatible components"),
		},
		"general: server/agent minor delta too large": {
			a:      testComponent(t, "server", "1.0.0"),
			b:      testComponent(t, "agent", "1.1.0"),
			expErr: errors.New("incompatible components"),
		},
		"custom: specific version not backwards compatible": {
			a: testComponent(t, "server", "1.1.1"),
			b: testComponent(t, "agent", "1.1.0"),
			customRule: &build.InteropRule{
				Check: func(self, other *build.VersionedComponent) bool {
					v := build.MustNewVersion("1.1.1")
					return self.Version.Equals(v) &&
						!other.Version.LessThan(self.Version)
				},
			},
			expErr: errors.New("incompatible components"),
		},
		"custom: except specific component versions from standard interop rules": {
			a: testComponent(t, "server", "1.1.1"),
			b: testComponent(t, "agent", "2.0.0"),
			// Shows an example of a custom rule that can be used to override
			// the default interop rules.
			customRule: &build.InteropRule{
				Self:          build.ComponentServer,
				Other:         build.ComponentAgent,
				StopOnSuccess: true,
				Check: func(self, other *build.VersionedComponent) bool {
					v1_1_1 := build.MustNewVersion("1.1.1")
					v2_0_0 := build.MustNewVersion("2.0.0")
					return self.Version.Equals(v1_1_1) && other.Version.Equals(v2_0_0)
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			var gotErr error
			if tc.customRule != nil {
				gotErr = build.CheckCompatibility(tc.a, tc.b, *tc.customRule)
			} else {
				gotErr = build.CheckCompatibility(tc.a, tc.b)
			}
			common.CmpErr(t, tc.expErr, gotErr)
		})
	}
}
