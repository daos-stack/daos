//
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package api

import (
	"context"
	"fmt"
	"reflect"
	"sort"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
)

var (
	testContName = "test-container"
)

func TestAPI_ContainerOpen(t *testing.T) {
	defaultReq := ContainerOpenReq{
		ID:      daos_default_ContainerInfo.ContainerLabel,
		Flags:   daos.ContainerOpenFlagReadWrite,
		Query:   true,
		SysName: build.DefaultSystemName,
		PoolID:  daos_default_PoolInfo.Label,
	}

	for name, tc := range map[string]struct {
		setup       func(t *testing.T)
		ctx         context.Context
		openReq     ContainerOpenReq
		checkParams func(t *testing.T)
		expResp     *ContainerOpenResp
		expErr      error
	}{
		"nil context": {
			openReq: defaultReq,
			expErr:  errNilCtx,
		},
		"no contID in req": {
			ctx: test.Context(t),
			openReq: ContainerOpenReq{
				SysName: defaultReq.SysName,
				Flags:   defaultReq.Flags,
				Query:   defaultReq.Query,
				PoolID:  defaultReq.PoolID,
			},
			expErr: errors.Wrap(daos.InvalidInput, "no container ID provided"),
		},
		"context already has a connection for a container": {
			ctx: func() context.Context {
				otherContHdl := &ContainerHandle{
					connHandle: connHandle{
						daosHandle: defaultContHdl(),
						UUID:       test.MockPoolUUID(99),
						Label:      "not-the-container-you're-looking-for",
					},
					PoolHandle: defaultPoolHandle(),
				}
				return otherContHdl.toCtx(test.Context(t))
			}(),
			expErr: ErrContextHandleConflict,
		},
		"daos_cont_open() fails": {
			setup: func(t *testing.T) {
				daos_cont_open_RC = _Ctype_int(daos.IOError)
			},
			ctx:     test.Context(t),
			openReq: defaultReq,
			expErr:  errors.New("failed to open container"),
		},
		"daos_cont_open() succeeds (no pool in context)": {
			ctx:     test.Context(t),
			openReq: defaultReq,
			checkParams: func(t *testing.T) {
				test.CmpAny(t, "pool connect count", 1, daos_pool_connect_Count)
				test.CmpAny(t, "contID", defaultReq.ID, daos_cont_open_SetContainerID)
				test.CmpAny(t, "flags", defaultReq.Flags, daos_cont_open_SetFlags)
			},
			expResp: &ContainerOpenResp{
				Connection: &ContainerHandle{
					connHandle: connHandle{
						Label:      daos_default_ContainerInfo.ContainerLabel,
						UUID:       daos_default_ContainerInfo.ContainerUUID,
						daosHandle: defaultContHdl(),
					},
					PoolHandle: defaultPoolHandle(),
				},
				Info: defaultContainerInfo(),
			},
		},
		"daos_cont_open() succeeds (pool in context)": {
			ctx: defaultPoolHandle().toCtx(test.Context(t)),
			openReq: ContainerOpenReq{
				ID:    defaultReq.ID,
				Flags: defaultReq.Flags,
				Query: defaultReq.Query,
			},
			checkParams: func(t *testing.T) {
				test.CmpAny(t, "pool connect count", 0, daos_pool_connect_Count)
				test.CmpAny(t, "contID", defaultReq.ID, daos_cont_open_SetContainerID)
				test.CmpAny(t, "flags", defaultReq.Flags, daos_cont_open_SetFlags)
			},
			expResp: &ContainerOpenResp{
				Connection: &ContainerHandle{
					connHandle: connHandle{
						Label:      daos_default_ContainerInfo.ContainerLabel,
						UUID:       daos_default_ContainerInfo.ContainerUUID,
						daosHandle: defaultContHdl(),
					},
					PoolHandle: defaultPoolHandle(),
				},
				Info: defaultContainerInfo(),
			},
		},
		"Open with UUID and query enabled": {
			ctx: test.Context(t),
			openReq: ContainerOpenReq{
				ID:     daos_default_ContainerInfo.ContainerUUID.String(),
				Flags:  defaultReq.Flags,
				Query:  true,
				PoolID: defaultReq.PoolID,
			},
			expResp: &ContainerOpenResp{
				Connection: &ContainerHandle{
					connHandle: connHandle{
						Label:      daos_default_ContainerInfo.ContainerLabel,
						UUID:       daos_default_ContainerInfo.ContainerUUID,
						daosHandle: defaultContHdl(),
					},
					PoolHandle: defaultPoolHandle(),
				},
				Info: defaultContainerInfo(),
			},
		},
		"Open with UUID and query enabled -- query fails": {
			setup: func(t *testing.T) {
				daos_cont_query_RC = _Ctype_int(daos.IOError)
			},
			ctx: test.Context(t),
			openReq: ContainerOpenReq{
				ID:     daos_default_ContainerInfo.ContainerUUID.String(),
				Flags:  defaultReq.Flags,
				Query:  true,
				PoolID: defaultReq.PoolID,
			},
			checkParams: func(t *testing.T) {
				// Make sure we don't leak handles when handling errors.
				test.CmpAny(t, "container close count", 1, daos_cont_close_Count)
				test.CmpAny(t, "pool disconnect count", 1, daos_pool_disconnect_Count)
			},
			expErr: daos.IOError,
		},
		"Open with UUID and query disabled": {
			ctx: test.Context(t),
			openReq: ContainerOpenReq{
				ID:     daos_default_ContainerInfo.ContainerUUID.String(),
				Flags:  defaultReq.Flags,
				Query:  false,
				PoolID: defaultReq.PoolID,
			},
			expResp: &ContainerOpenResp{
				Connection: &ContainerHandle{
					connHandle: connHandle{
						UUID:       daos_default_ContainerInfo.ContainerUUID,
						daosHandle: defaultContHdl(),
					},
					PoolHandle: defaultPoolHandle(),
				},
				Info: func() *daos.ContainerInfo {
					ci := defaultContainerInfo()
					ci.ContainerLabel = ""
					ci.Type = 0
					ci.Health = ""
					ci.POSIXAttributes = nil
					return ci
				}(),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			t.Cleanup(ResetTestStubs)
			if tc.setup != nil {
				tc.setup(t)
			}
			if tc.checkParams != nil {
				defer tc.checkParams(t)
			}

			gotResp, gotErr := ContainerOpen(test.MustLogContext(t, tc.ctx), tc.openReq)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			cmpOpts := cmp.Options{
				cmp.Comparer(func(a, b *ContainerHandle) bool {
					return a != nil && b != nil && a.String() == b.String()
				}),
			}
			test.CmpAny(t, "ContainerOpenResp", tc.expResp, gotResp, cmpOpts...)
		})
	}
}

func TestAPI_ContainerDestroy(t *testing.T) {
	for name, tc := range map[string]struct {
		setup       func(t *testing.T)
		ctx         context.Context
		poolID      string
		contID      string
		checkParams func(t *testing.T)
		expErr      error
	}{
		"nil context": {
			expErr: errNilCtx,
		},
		"pool_connect with read/write flag fails, but succeeds with readonly": {
			setup: func(t *testing.T) {
				daos_pool_connect_RCList = []_Ctype_int{
					_Ctype_int(daos.NoPermission),
					0,
				}
			},
			ctx:    test.Context(t),
			poolID: testPoolName,
			contID: testContName,
		},
		"daos_cont_destroy fails": {
			setup: func(t *testing.T) {
				daos_cont_destroy_RC = _Ctype_int(daos.IOError)
			},
			ctx:    test.Context(t),
			poolID: testPoolName,
			contID: testContName,
			checkParams: func(t *testing.T) {
				test.CmpAny(t, "pool disconnect count", 1, daos_pool_disconnect_Count)
			},
			expErr: errors.New("failed to destroy container"),
		},
		"daos_cont_destroy succeeds": {
			ctx:    test.Context(t),
			poolID: testPoolName,
			contID: testContName,
			checkParams: func(t *testing.T) {
				test.CmpAny(t, "pool disconnect count", 1, daos_pool_disconnect_Count)
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			t.Cleanup(ResetTestStubs)
			if tc.setup != nil {
				tc.setup(t)
			}

			if tc.checkParams != nil {
				defer tc.checkParams(t)
			}

			gotErr := ContainerDestroy(test.MustLogContext(t, tc.ctx), "", tc.poolID, tc.contID, false)
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}

func TestAPI_getContConn(t *testing.T) {
	otherContHdl := &ContainerHandle{
		connHandle: connHandle{
			daosHandle: defaultContHdl(),
			UUID:       test.MockPoolUUID(99),
			Label:      "not-the-container-you're-looking-for",
		},
		PoolHandle: defaultPoolHandle(),
	}

	for name, tc := range map[string]struct {
		setup       func(t *testing.T)
		ctx         context.Context
		sysName     string
		poolID      string
		contID      string
		flags       daos.ContainerOpenFlag
		checkParams func(t *testing.T)
		expHdl      *ContainerHandle
		expErr      error
	}{
		"nil context": {
			expErr: errNilCtx,
		},
		"pool handle not in context, no poolID": {
			ctx:    test.Context(t),
			contID: testContName,
			expErr: errors.Wrap(daos.InvalidInput, "no pool ID provided"),
		},
		"pool not in context; pool connect fails": {
			ctx: test.Context(t),
			setup: func(t *testing.T) {
				daos_pool_connect_RC = _Ctype_int(daos.IOError)
			},
			poolID: testPoolName,
			contID: testContName,
			expErr: errors.Wrap(daos.IOError, "failed to connect to pool"),
		},
		"pool handle in context with non-empty poolID": {
			ctx:    defaultPoolHandle().toCtx(test.Context(t)),
			poolID: testPoolName,
			expErr: errors.New("PoolHandle found in context with non-empty poolID"),
		},
		"container handle not in context, no contID": {
			ctx:    defaultPoolHandle().toCtx(test.Context(t)),
			expErr: errors.Wrap(daos.InvalidInput, "no container ID provided"),
		},
		"container handle in context with non-empty contID": {
			ctx:    defaultContainerHandle().toCtx(test.Context(t)),
			contID: testContName,
			expErr: errors.New("non-empty contID"),
		},
		"context already has a connection for a container": {
			ctx: otherContHdl.toCtx(test.Context(t)),
			checkParams: func(t *testing.T) {
				test.CmpAny(t, "pool connect count", 0, daos_pool_connect_Count)
				test.CmpAny(t, "cont open count", 0, daos_cont_open_Count)
			},
			expHdl: otherContHdl,
		},
		"pool handle from context; container open fails": {
			ctx: defaultPoolHandle().toCtx(test.Context(t)),
			setup: func(t *testing.T) {
				daos_cont_open_RC = _Ctype_int(daos.IOError)
			},
			contID: testContName,
			checkParams: func(t *testing.T) {
				// Pool was already connected, so it shouldn't be disconnected in this
				// error handler.
				test.CmpAny(t, "pool disconnect count", 0, daos_pool_disconnect_Count)
			},
			expErr: errors.New("failed to open container"),
		},
		"pool handle from Connect(); container open fails": {
			setup: func(t *testing.T) {
				daos_cont_open_RC = _Ctype_int(daos.IOError)
			},
			ctx:    test.Context(t),
			poolID: testPoolName,
			contID: testContName,
			checkParams: func(t *testing.T) {
				// Pool needed to be connected, so it should be disconnected in this
				// error handler.
				test.CmpAny(t, "pool disconnect count", 1, daos_pool_disconnect_Count)
			},
			expErr: errors.New("failed to open container"),
		},
		"pool handle from context; container handle from Open()": {
			ctx:    defaultPoolHandle().toCtx(test.Context(t)),
			contID: testContName,
			checkParams: func(t *testing.T) {
				test.CmpAny(t, "pool connect count", 0, daos_pool_connect_Count)

				test.CmpAny(t, "cont open count", 1, daos_cont_open_Count)
				test.CmpAny(t, "contID", testContName, daos_cont_open_SetContainerID)
				test.CmpAny(t, "open flags", daos.ContainerOpenFlagReadOnly, daos_cont_open_SetFlags)
			},
			expHdl: defaultContainerHandle(),
		},
		"pool handle from Connect() with non-default sys name; container handle from Open()": {
			ctx:     test.Context(t),
			sysName: "non-default",
			poolID:  testPoolName,
			contID:  testContName,
			checkParams: func(t *testing.T) {
				test.CmpAny(t, "pool connect count", 1, daos_pool_connect_Count)
				test.CmpAny(t, "poolID", testPoolName, daos_pool_connect_SetPoolID)
				test.CmpAny(t, "sysName", "non-default", daos_pool_connect_SetSys)
				test.CmpAny(t, "connect flags", daos.PoolConnectFlagReadOnly, daos_pool_connect_SetFlags)
				test.CmpAny(t, "pool query", daos.PoolQueryMask(0), daos_pool_connect_QueryMask)

				test.CmpAny(t, "cont open count", 1, daos_cont_open_Count)
				test.CmpAny(t, "contID", testContName, daos_cont_open_SetContainerID)
				test.CmpAny(t, "open flags", daos.ContainerOpenFlagReadOnly, daos_cont_open_SetFlags)
			},
			expHdl: defaultContainerHandle(),
		},
		"pool handle from Connect(); container handle from Open()": {
			ctx:    test.Context(t),
			poolID: testPoolName,
			contID: testContName,
			checkParams: func(t *testing.T) {
				test.CmpAny(t, "pool connect count", 1, daos_pool_connect_Count)
				test.CmpAny(t, "poolID", testPoolName, daos_pool_connect_SetPoolID)
				test.CmpAny(t, "sysName", "", daos_pool_connect_SetSys)
				test.CmpAny(t, "connect flags", daos.PoolConnectFlagReadOnly, daos_pool_connect_SetFlags)
				test.CmpAny(t, "pool query", daos.PoolQueryMask(0), daos_pool_connect_QueryMask)

				test.CmpAny(t, "cont open count", 1, daos_cont_open_Count)
				test.CmpAny(t, "contID", testContName, daos_cont_open_SetContainerID)
				test.CmpAny(t, "open flags", daos.ContainerOpenFlagReadOnly, daos_cont_open_SetFlags)
			},
			expHdl: defaultContainerHandle(),
		},
	} {
		t.Run(name, func(t *testing.T) {
			t.Cleanup(ResetTestStubs)
			if tc.setup != nil {
				tc.setup(t)
			}

			if tc.checkParams != nil {
				defer tc.checkParams(t)
			}

			ph, cleanup, gotErr := getContConn(test.MustLogContext(t, tc.ctx), tc.sysName, tc.poolID, tc.contID, tc.flags)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
			t.Cleanup(cleanup)

			cmpOpts := cmp.Options{
				cmp.Comparer(func(a, b *ContainerHandle) bool {
					return a != nil && b != nil && a.String() == b.String()
				}),
			}
			test.CmpAny(t, "ContainerHandle", tc.expHdl, ph, cmpOpts...)
		})
	}
}

func TestAPI_containerQueryDFSAttrs(t *testing.T) {
	for name, tc := range map[string]struct {
		setup    func(t *testing.T)
		hdl      *ContainerHandle
		expAttrs *daos.POSIXAttributes
		expErr   error
	}{
		"nil handle": {
			expErr: ErrInvalidContainerHandle,
		},
		"dfs_mount fails": {
			setup: func(t *testing.T) {
				dfs_mount_RC = 22
			},
			hdl:    defaultContainerHandle(),
			expErr: errors.New("failed to mount container"),
		},
		"dfs_query fails": {
			setup: func(t *testing.T) {
				dfs_query_RC = 22
			},
			hdl:    defaultContainerHandle(),
			expErr: errors.New("failed to query container"),
		},
		"dfs_umount fails": {
			setup: func(t *testing.T) {
				dfs_umount_RC = 22
			},
			hdl:    defaultContainerHandle(),
			expErr: errors.New("failed to unmount container"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			t.Cleanup(ResetTestStubs)
			if tc.setup != nil {
				tc.setup(t)
			}

			gotAttrs, gotErr := containerQueryDFSAttrs(tc.hdl)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			test.CmpAny(t, "DFS attributes", tc.expAttrs, gotAttrs)
		})
	}
}

func TestAPI_ContainerQuery(t *testing.T) {
	for name, tc := range map[string]struct {
		setup       func(t *testing.T)
		ctx         context.Context
		poolID      string
		contID      string
		checkParams func(t *testing.T)
		expResp     *daos.ContainerInfo
		expErr      error
	}{
		"nil context": {
			expErr: errNilCtx,
		},
		"daos_cont_query() fails": {
			setup: func(t *testing.T) {
				daos_cont_query_RC = _Ctype_int(daos.IOError)
			},
			ctx:    test.Context(t),
			poolID: daos_default_PoolInfo.Label,
			contID: daos_default_ContainerInfo.ContainerLabel,
			expErr: errors.Wrap(daos.IOError, "failed to query container"),
		},
		"DFS query fails on POSIX container": {
			setup: func(t *testing.T) {
				dfs_query_RC = 22
			},
			ctx:    defaultContainerHandle().toCtx(test.Context(t)),
			expErr: errors.New("failed to query container"),
		},
		"success": {
			ctx:     defaultContainerHandle().toCtx(test.Context(t)),
			expResp: defaultContainerInfo(),
		},
	} {
		t.Run(name, func(t *testing.T) {
			t.Cleanup(ResetTestStubs)
			if tc.setup != nil {
				tc.setup(t)
			}

			if tc.checkParams != nil {
				defer tc.checkParams(t)
			}

			gotResp, err := ContainerQuery(test.MustLogContext(t, tc.ctx), "", tc.poolID, tc.contID)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			cmpOpts := cmp.Options{
				cmp.Comparer(func(a, b ranklist.RankSet) bool {
					return a.String() == b.String()
				}),
			}
			test.CmpAny(t, "ContainerQuery() ContainerInfo", tc.expResp, gotResp, cmpOpts...)
		})
	}
}

func TestAPI_ContainerListAttributes(t *testing.T) {
	for name, tc := range map[string]struct {
		setup    func(t *testing.T)
		ctx      context.Context
		poolID   string
		contID   string
		expNames []string
		expErr   error
	}{
		"nil context": {
			expErr: errNilCtx,
		},
		"daos_cont_list_attr() fails (get buf size)": {
			setup: func(t *testing.T) {
				daos_cont_list_attr_RC = _Ctype_int(daos.IOError)
			},
			ctx:    defaultContainerHandle().toCtx(test.Context(t)),
			expErr: errors.Wrap(daos.IOError, "failed to list container attributes"),
		},
		"daos_cont_list_attr() fails (fetch names)": {
			setup: func(t *testing.T) {
				daos_cont_list_attr_RCList = []_Ctype_int{
					0,
					_Ctype_int(daos.IOError),
				}
			},
			ctx:    defaultContainerHandle().toCtx(test.Context(t)),
			expErr: errors.Wrap(daos.IOError, "failed to list container attributes"),
		},
		"no attributes set": {
			setup: func(t *testing.T) {
				daos_cont_list_attr_AttrList = nil
			},
			ctx: defaultContainerHandle().toCtx(test.Context(t)),
		},
		"success": {
			ctx: defaultContainerHandle().toCtx(test.Context(t)),
			expNames: []string{
				daos_default_AttrList[0].Name,
				daos_default_AttrList[1].Name,
				daos_default_AttrList[2].Name,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			t.Cleanup(ResetTestStubs)
			if tc.setup != nil {
				tc.setup(t)
			}

			gotNames, err := ContainerListAttributes(test.MustLogContext(t, tc.ctx), "", tc.poolID, tc.contID)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			test.CmpAny(t, "ContainerListAttributes()", tc.expNames, gotNames)
		})
	}
}

func TestAPI_ContainerGetAttributes(t *testing.T) {
	for name, tc := range map[string]struct {
		setup       func(t *testing.T)
		ctx         context.Context
		poolID      string
		contID      string
		attrNames   []string
		checkParams func(t *testing.T)
		expAttrs    daos.AttributeList
		expErr      error
	}{
		"nil context": {
			expErr: errNilCtx,
		},
		"daos_cont_list_attr() fails": {
			setup: func(t *testing.T) {
				daos_cont_list_attr_RC = _Ctype_int(daos.IOError)
			},
			ctx:    defaultContainerHandle().toCtx(test.Context(t)),
			expErr: errors.Wrap(daos.IOError, "failed to list container attributes"),
		},
		"daos_cont_get_attr() fails (sizes)": {
			setup: func(t *testing.T) {
				daos_cont_get_attr_RC = _Ctype_int(daos.IOError)
			},
			ctx:    defaultContainerHandle().toCtx(test.Context(t)),
			expErr: errors.Wrap(daos.IOError, "failed to get container attribute sizes"),
		},
		"daos_cont_get_attr() fails (values)": {
			setup: func(t *testing.T) {
				daos_cont_get_attr_RCList = []_Ctype_int{
					0,
					_Ctype_int(daos.IOError),
				}
			},
			ctx:    defaultContainerHandle().toCtx(test.Context(t)),
			expErr: errors.Wrap(daos.IOError, "failed to get container attribute values"),
		},
		"empty requested attribute name": {
			ctx:       defaultContainerHandle().toCtx(test.Context(t)),
			attrNames: test.JoinArgs(nil, "a", ""),
			expErr:    errors.Errorf("empty container attribute name at index 1"),
		},
		"no attributes set; attributes requested": {
			setup: func(t *testing.T) {
				daos_cont_get_attr_AttrList = nil
			},
			ctx:       defaultContainerHandle().toCtx(test.Context(t)),
			attrNames: test.JoinArgs(nil, "foo"),
			checkParams: func(t *testing.T) {
				test.CmpAny(t, "req attr names", map[string]struct{}{"foo": {}}, daos_cont_get_attr_ReqNames)
			},
			expErr: errors.Wrap(daos.Nonexistent, "failed to get container attribute sizes"),
		},
		"unknown attribute requested": {
			ctx:       defaultContainerHandle().toCtx(test.Context(t)),
			attrNames: test.JoinArgs(nil, "foo"),
			checkParams: func(t *testing.T) {
				test.CmpAny(t, "req attr names", map[string]struct{}{"foo": {}}, daos_cont_get_attr_ReqNames)
			},
			expErr: errors.Wrap(daos.Nonexistent, "failed to get container attribute sizes"),
		},
		"no attributes set; no attributes requested": {
			setup: func(t *testing.T) {
				daos_cont_list_attr_AttrList = nil
			},
			ctx: defaultContainerHandle().toCtx(test.Context(t)),
		},
		"success; all attributes": {
			ctx:      defaultContainerHandle().toCtx(test.Context(t)),
			expAttrs: daos_default_AttrList,
		},
		"success; requested attributes": {
			ctx:       defaultContainerHandle().toCtx(test.Context(t)),
			attrNames: test.JoinArgs(nil, daos_default_AttrList[0].Name, daos_default_AttrList[2].Name),
			checkParams: func(t *testing.T) {
				reqNames := test.JoinArgs(nil, daos_default_AttrList[0].Name, daos_default_AttrList[2].Name)
				sort.Strings(reqNames)
				gotNames := daos_test_get_mappedNames(daos_cont_get_attr_ReqNames)
				sort.Strings(gotNames)
				test.CmpAny(t, "req attr names", reqNames, gotNames)
			},
			expAttrs: daos.AttributeList{
				daos_default_AttrList[0],
				daos_default_AttrList[2],
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			t.Cleanup(ResetTestStubs)
			if tc.setup != nil {
				tc.setup(t)
			}

			if tc.checkParams != nil {
				defer tc.checkParams(t)
			}

			gotAttrs, err := ContainerGetAttributes(test.MustLogContext(t, tc.ctx), "", tc.poolID, tc.contID, tc.attrNames...)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			test.CmpAny(t, "ContainerGetAttributes() daos.AttributeList", tc.expAttrs, gotAttrs)
		})
	}
}

func TestAPI_ContainerSetAttributes(t *testing.T) {
	for name, tc := range map[string]struct {
		setup  func(t *testing.T)
		ctx    context.Context
		poolID string
		contID string
		toSet  daos.AttributeList
		expErr error
	}{
		"nil context": {
			expErr: errNilCtx,
		},
		"no attributes to set": {
			ctx:    defaultContainerHandle().toCtx(test.Context(t)),
			expErr: errors.Wrap(daos.InvalidInput, "no container attributes provided"),
		},
		"nil toSet attribute": {
			ctx:    defaultContainerHandle().toCtx(test.Context(t)),
			toSet:  append(daos_default_AttrList, nil),
			expErr: errors.Wrap(daos.InvalidInput, "nil container attribute at index 3"),
		},
		"toSet attribute with empty name": {
			ctx:    defaultContainerHandle().toCtx(test.Context(t)),
			toSet:  append(daos_default_AttrList, &daos.Attribute{Name: ""}),
			expErr: errors.Wrap(daos.InvalidInput, "empty container attribute name at index 3"),
		},
		"toSet attribute with empty value": {
			ctx:    defaultContainerHandle().toCtx(test.Context(t)),
			toSet:  append(daos_default_AttrList, &daos.Attribute{Name: "empty"}),
			expErr: errors.Wrap(daos.InvalidInput, "empty container attribute value at index 3"),
		},
		"daos_cont_set_attr() fails": {
			setup: func(t *testing.T) {
				daos_cont_set_attr_RC = _Ctype_int(daos.IOError)
			},
			ctx:    defaultContainerHandle().toCtx(test.Context(t)),
			toSet:  daos_default_AttrList,
			expErr: errors.Wrap(daos.IOError, "failed to set container attributes"),
		},
		"success": {
			ctx:   defaultContainerHandle().toCtx(test.Context(t)),
			toSet: daos_default_AttrList,
		},
	} {
		t.Run(name, func(t *testing.T) {
			t.Cleanup(ResetTestStubs)
			if tc.setup != nil {
				tc.setup(t)
			}

			err := ContainerSetAttributes(test.MustLogContext(t, tc.ctx), "", tc.poolID, tc.contID, tc.toSet...)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			test.CmpAny(t, "ContainerSetAttributes() daos.AttributeList", tc.toSet, daos_cont_set_attr_AttrList)
		})
	}
}

func TestAPI_ContainerDeleteAttributes(t *testing.T) {
	for name, tc := range map[string]struct {
		setup    func(t *testing.T)
		ctx      context.Context
		poolID   string
		contID   string
		toDelete []string
		expErr   error
	}{
		"nil context": {
			expErr: errNilCtx,
		},
		"no attributes to delete": {
			ctx:    defaultContainerHandle().toCtx(test.Context(t)),
			expErr: errors.Wrap(daos.InvalidInput, "no container attribute names provided"),
		},
		"empty name in toDelete list": {
			ctx:      defaultContainerHandle().toCtx(test.Context(t)),
			toDelete: test.JoinArgs(nil, "foo", "", "bar"),
			expErr:   errors.Wrap(daos.InvalidInput, "empty container attribute name at index 1"),
		},
		"daos_cont_det_attr() fails": {
			setup: func(t *testing.T) {
				daos_cont_del_attr_RC = _Ctype_int(daos.IOError)
			},
			ctx:      defaultContainerHandle().toCtx(test.Context(t)),
			toDelete: test.JoinArgs(nil, daos_default_AttrList[0].Name),
			expErr:   errors.Wrap(daos.IOError, "failed to delete container attributes"),
		},
		"success": {
			ctx:      defaultContainerHandle().toCtx(test.Context(t)),
			toDelete: test.JoinArgs(nil, daos_default_AttrList[0].Name),
		},
	} {
		t.Run(name, func(t *testing.T) {
			t.Cleanup(ResetTestStubs)
			if tc.setup != nil {
				tc.setup(t)
			}

			err := ContainerDeleteAttributes(test.MustLogContext(t, tc.ctx), "", tc.poolID, tc.contID, tc.toDelete...)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			test.CmpAny(t, "ContainerDeleteAttributes() AttrNames", tc.toDelete, daos_cont_del_attr_AttrNames)
		})
	}
}

func TestAPI_ContainerHandleMethods(t *testing.T) {
	thType := reflect.TypeOf(defaultContainerHandle())
	for i := 0; i < thType.NumMethod(); i++ {
		method := thType.Method(i)
		methArgs := make([]reflect.Value, 0)
		ctx := test.Context(t)
		var expResults int

		switch method.Name {
		case "Close":
			expResults = 1
		case "Query":
			expResults = 2
		case "ListAttributes":
			expResults = 2
		case "GetAttributes":
			methArgs = append(methArgs, reflect.ValueOf(daos_default_AttrList[0].Name))
			expResults = 2
		case "SetAttributes":
			methArgs = append(methArgs, reflect.ValueOf(daos_default_AttrList[0]))
			expResults = 1
		case "DeleteAttributes":
			methArgs = append(methArgs, reflect.ValueOf(daos_default_AttrList[0].Name))
			expResults = 1
		case "GetProperties":
			methArgs = append(methArgs, reflect.ValueOf(daos.ContainerPropLabel.String()))
			expResults = 2
		case "SetProperties":
			propList, err := daos.AllocateContainerPropertyList(1)
			if err != nil {
				t.Fatal(err)
			}
			defer propList.Free()
			methArgs = append(methArgs, reflect.ValueOf(propList))
			expResults = 1
		case "FillHandle", "IsValid", "String", "UUID", "ID":
			// No tests for these. The main point of this suite is to ensure that the
			// convenience wrappers handle inputs as expected.
			continue
		default:
			// If you're here, you need to add a case to test your new method.
			t.Fatalf("unhandled method %q", method.Name)
		}

		// Not intended to be exhaustive; just verify that they accept the parameters
		// we expect and return something sensible for errors.
		for name, tc := range map[string]struct {
			setup  func(t *testing.T)
			th     *ContainerHandle
			expErr error
		}{
			fmt.Sprintf("%s: nil handle", method.Name): {
				th:     nil,
				expErr: ErrInvalidContainerHandle,
			},
			fmt.Sprintf("%s: success", method.Name): {
				th: defaultContainerHandle(),
			},
		} {
			t.Run(name, func(t *testing.T) {
				thArg := reflect.ValueOf(tc.th)
				if tc.th == nil {
					thArg = reflect.New(thType).Elem()
				}
				ctxArg := reflect.ValueOf(ctx)
				testArgs := append([]reflect.Value{thArg, ctxArg}, methArgs...)
				t.Logf("\nargs: %+v", testArgs)

				retVals := method.Func.Call(testArgs)
				if len(retVals) != expResults {
					t.Fatalf("expected %d return values, got %d", expResults, len(retVals))
				}

				if err, ok := retVals[len(retVals)-1].Interface().(error); ok {
					test.CmpErr(t, tc.expErr, err)
				} else {
					test.CmpErr(t, tc.expErr, nil)
				}
			})
		}
	}
}
