//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package api

import (
	"syscall"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/daos"
)

func TestAPI_ResolvePathInfo(t *testing.T) {
	for name, tc := range map[string]struct {
		setup   func(t *testing.T)
		path    string
		expInfo *PathInfo
		expErr  error
	}{
		"empty path": {
			path:   "",
			expErr: errors.Wrap(daos.InvalidInput, "empty path"),
		},
		"resolve fails": {
			setup: func(t *testing.T) {
				duns_resolve_path_RC = _Ctype_int(syscall.ENODATA)
			},
			path:   "/some/path",
			expErr: errors.New("failed to resolve path"),
		},
		"resolve succeeds": {
			setup: func(t *testing.T) {
				duns_resolve_path_PoolID = "test-pool-uuid"
				duns_resolve_path_ContainerID = "test-cont-uuid"
				duns_resolve_path_Layout = daos.ContainerLayoutPOSIX
			},
			path: "/mnt/daos/mycontainer",
			expInfo: &PathInfo{
				PoolID:      "test-pool-uuid",
				ContainerID: "test-cont-uuid",
				Layout:      daos.ContainerLayoutPOSIX,
			},
		},
		"resolve succeeds with relative path": {
			setup: func(t *testing.T) {
				duns_resolve_path_PoolID = "pool-label"
				duns_resolve_path_ContainerID = "cont-label"
				duns_resolve_path_Layout = daos.ContainerLayoutHDF5
				duns_resolve_path_RelPath = "subdir/file.h5"
			},
			path: "/mnt/daos/mycontainer/subdir/file.h5",
			expInfo: &PathInfo{
				PoolID:      "pool-label",
				ContainerID: "cont-label",
				Layout:      daos.ContainerLayoutHDF5,
				RelPath:     "subdir/file.h5",
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			ResetTestStubs()
			if tc.setup != nil {
				tc.setup(t)
			}

			info, err := ResolvePathInfo(tc.path)

			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expInfo, info); diff != "" {
				t.Fatalf("unexpected info (-want +got):\n%s", diff)
			}
		})
	}
}

func TestAPI_ContainerLinkAtPath(t *testing.T) {
	for name, tc := range map[string]struct {
		setup       func(t *testing.T)
		poolID      string
		containerID string
		path        string
		checkParams func(t *testing.T)
		expErr      error
	}{
		"empty container ID": {
			poolID:      "test-pool",
			containerID: "",
			path:        "/mnt/daos/link",
			expErr:      errors.Wrap(daos.InvalidInput, "empty container ID"),
		},
		"empty path": {
			poolID:      "test-pool",
			containerID: "test-cont",
			path:        "",
			expErr:      errors.Wrap(daos.InvalidInput, "empty path"),
		},
		"link fails": {
			setup: func(t *testing.T) {
				duns_link_cont_RC = _Ctype_int(syscall.EEXIST)
			},
			poolID:      "test-pool",
			containerID: "test-cont",
			path:        "/mnt/daos/link",
			expErr:      errors.New("failed to link container"),
		},
		"link succeeds": {
			poolID:      "test-pool",
			containerID: "my-container",
			path:        "/mnt/daos/mylink",
			checkParams: func(t *testing.T) {
				test.CmpAny(t, "containerID", "my-container", duns_link_cont_SetContainerID)
				test.CmpAny(t, "path", "/mnt/daos/mylink", duns_link_cont_SetPath)
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			ResetTestStubs()
			if tc.setup != nil {
				tc.setup(t)
			}

			ctx := test.Context(t)
			err := ContainerLinkAtPath(ctx, "", tc.poolID, tc.containerID, tc.path)

			test.CmpErr(t, tc.expErr, err)
			if tc.checkParams != nil {
				tc.checkParams(t)
			}
		})
	}
}

func TestAPI_PoolHandle_LinkContainerAtPath(t *testing.T) {
	for name, tc := range map[string]struct {
		setup       func(t *testing.T)
		ph          *PoolHandle
		containerID string
		path        string
		checkParams func(t *testing.T)
		expErr      error
	}{
		"nil pool handle": {
			ph:          nil,
			containerID: "test-cont",
			path:        "/mnt/daos/link",
			expErr:      ErrInvalidPoolHandle,
		},
		"invalid pool handle": {
			ph:          &PoolHandle{},
			containerID: "test-cont",
			path:        "/mnt/daos/link",
			expErr:      ErrInvalidPoolHandle,
		},
		"link succeeds": {
			ph:          defaultPoolHandle(),
			containerID: "my-container",
			path:        "/mnt/daos/mylink",
			checkParams: func(t *testing.T) {
				test.CmpAny(t, "containerID", "my-container", duns_link_cont_SetContainerID)
				test.CmpAny(t, "path", "/mnt/daos/mylink", duns_link_cont_SetPath)
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			ResetTestStubs()
			if tc.setup != nil {
				tc.setup(t)
			}

			ctx := test.Context(t)
			err := tc.ph.LinkContainerAtPath(ctx, tc.containerID, tc.path)

			test.CmpErr(t, tc.expErr, err)
			if tc.checkParams != nil {
				tc.checkParams(t)
			}
		})
	}
}

func TestAPI_ContainerDestroyAtPath(t *testing.T) {
	for name, tc := range map[string]struct {
		setup       func(t *testing.T)
		poolID      string
		path        string
		checkParams func(t *testing.T)
		expErr      error
	}{
		"empty path": {
			poolID: "test-pool",
			path:   "",
			expErr: errors.Wrap(daos.InvalidInput, "empty path"),
		},
		"destroy fails": {
			setup: func(t *testing.T) {
				duns_destroy_path_RC = _Ctype_int(syscall.ENOENT)
			},
			poolID: "test-pool",
			path:   "/mnt/daos/link",
			expErr: errors.New("failed to destroy path"),
		},
		"destroy succeeds": {
			poolID: "test-pool",
			path:   "/mnt/daos/mylink",
			checkParams: func(t *testing.T) {
				test.CmpAny(t, "path", "/mnt/daos/mylink", duns_destroy_path_SetPath)
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			ResetTestStubs()
			if tc.setup != nil {
				tc.setup(t)
			}

			ctx := test.Context(t)
			err := ContainerDestroyAtPath(ctx, "", tc.poolID, tc.path)

			test.CmpErr(t, tc.expErr, err)
			if tc.checkParams != nil {
				tc.checkParams(t)
			}
		})
	}
}

func TestAPI_PoolHandle_DestroyContainerAtPath(t *testing.T) {
	for name, tc := range map[string]struct {
		setup       func(t *testing.T)
		ph          *PoolHandle
		path        string
		checkParams func(t *testing.T)
		expErr      error
	}{
		"nil pool handle": {
			ph:     nil,
			path:   "/mnt/daos/link",
			expErr: ErrInvalidPoolHandle,
		},
		"invalid pool handle": {
			ph:     &PoolHandle{},
			path:   "/mnt/daos/link",
			expErr: ErrInvalidPoolHandle,
		},
		"destroy succeeds": {
			ph:   defaultPoolHandle(),
			path: "/mnt/daos/mylink",
			checkParams: func(t *testing.T) {
				test.CmpAny(t, "path", "/mnt/daos/mylink", duns_destroy_path_SetPath)
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			ResetTestStubs()
			if tc.setup != nil {
				tc.setup(t)
			}

			ctx := test.Context(t)
			err := tc.ph.DestroyContainerAtPath(ctx, tc.path)

			test.CmpErr(t, tc.expErr, err)
			if tc.checkParams != nil {
				tc.checkParams(t)
			}
		})
	}
}
