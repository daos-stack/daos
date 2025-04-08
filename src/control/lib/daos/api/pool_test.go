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
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
)

var (
	testPoolName = "test-pool"
)

func TestAPI_PoolConnect(t *testing.T) {
	defaultReq := PoolConnectReq{
		ID:      daos_default_PoolInfo.Label,
		SysName: build.DefaultSystemName,
		Flags:   daos.PoolConnectFlagReadWrite,
		Query:   true,
	}

	for name, tc := range map[string]struct {
		setup       func(t *testing.T)
		ctx         context.Context
		connReq     PoolConnectReq
		checkParams func(t *testing.T)
		expResp     *PoolConnectResp
		expErr      error
	}{
		"nil context": {
			connReq: defaultReq,
			expErr:  errNilCtx,
		},
		"no poolID in req": {
			ctx: test.Context(t),
			connReq: PoolConnectReq{
				SysName: defaultReq.SysName,
				Flags:   defaultReq.Flags,
				Query:   defaultReq.Query,
			},
			expErr: errors.Wrap(daos.InvalidInput, "no pool ID provided"),
		},
		"context already has a connection for a pool": {
			ctx: func() context.Context {
				otherPoolHdl := &PoolHandle{
					connHandle: connHandle{
						UUID:  test.MockPoolUUID(99),
						Label: "not-the-pool-you're-looking-for",
					},
				}
				return otherPoolHdl.toCtx(test.Context(t))
			}(),
			connReq: defaultReq,
			checkParams: func(t *testing.T) {
				test.CmpAny(t, "pool connect count", 0, daos_pool_connect_Count)
			},
			expErr: ErrContextHandleConflict,
		},
		"daos_pool_connect() fails": {
			setup: func(t *testing.T) {
				daos_pool_connect_RC = -_Ctype_int(daos.IOError)
			},
			ctx:     test.Context(t),
			connReq: defaultReq,
			expErr:  errors.Wrap(daos.IOError, "failed to connect to pool"),
		},
		"daos_pool_connect() succeeds": {
			ctx:     test.Context(t),
			connReq: defaultReq,
			checkParams: func(t *testing.T) {
				test.CmpAny(t, "poolID", defaultReq.ID, daos_pool_connect_SetPoolID)
				test.CmpAny(t, "sysName", defaultReq.SysName, daos_pool_connect_SetSys)
				test.CmpAny(t, "flags", defaultReq.Flags, daos_pool_connect_SetFlags)
				test.CmpAny(t, "query", daos.DefaultPoolQueryMask, daos_pool_connect_QueryMask)
			},
			expResp: &PoolConnectResp{
				Connection: &PoolHandle{
					connHandle: connHandle{
						Label:      daos_default_PoolInfo.Label,
						UUID:       daos_default_PoolInfo.UUID,
						daosHandle: *defaultPoolHdl(),
					},
				},
				Info: defaultPoolInfo(),
			},
		},
		"Connect with UUID and query enabled": {
			ctx: test.Context(t),
			connReq: PoolConnectReq{
				ID:      daos_default_PoolInfo.UUID.String(),
				SysName: defaultReq.SysName,
				Flags:   defaultReq.Flags,
				Query:   true,
			},
			expResp: &PoolConnectResp{
				Connection: &PoolHandle{
					connHandle: connHandle{
						Label:      daos_default_PoolInfo.Label,
						UUID:       daos_default_PoolInfo.UUID,
						daosHandle: *defaultPoolHdl(),
					},
				},
				Info: defaultPoolInfo(),
			},
		},
		"Connect with UUID and query enabled -- query fails": {
			setup: func(t *testing.T) {
				daos_pool_query_RC = -_Ctype_int(daos.IOError)
			},
			ctx: test.Context(t),
			connReq: PoolConnectReq{
				ID:      daos_default_PoolInfo.UUID.String(),
				SysName: defaultReq.SysName,
				Flags:   defaultReq.Flags,
				Query:   true,
			},
			checkParams: func(t *testing.T) {
				test.CmpAny(t, "pool disconnect count", 1, daos_pool_disconnect_Count)
			},
			expErr: daos.IOError,
		},
		"Connect with UUID and query disabled": {
			ctx: test.Context(t),
			connReq: PoolConnectReq{
				ID:      daos_default_PoolInfo.UUID.String(),
				SysName: defaultReq.SysName,
				Flags:   defaultReq.Flags,
				Query:   false,
			},
			expResp: &PoolConnectResp{
				Connection: &PoolHandle{
					connHandle: connHandle{
						Label:      MissingPoolLabel,
						UUID:       daos_default_PoolInfo.UUID,
						daosHandle: *defaultPoolHdl(),
					},
				},
				Info: func() *daos.PoolInfo {
					out := defaultPoolInfo()
					out.Label = MissingPoolLabel
					return out
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

			gotResp, gotErr := PoolConnect(test.MustLogContext(t, tc.ctx), tc.connReq)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			cmpOpts := cmp.Options{
				cmp.Comparer(func(a, b *PoolHandle) bool {
					return a != nil && b != nil && a.String() == b.String()
				}),
				// These fields aren't populated in the PoolConnect() query.
				cmpopts.IgnoreFields(daos.PoolInfo{},
					"EnabledRanks", "DisabledRanks", "DeadRanks", "ServiceReplicas",
				),
			}
			test.CmpAny(t, "PoolConnectResp", tc.expResp, gotResp, cmpOpts...)
		})
	}
}

func TestAPI_getPoolConn(t *testing.T) {
	for name, tc := range map[string]struct {
		setup       func(t *testing.T)
		ctx         context.Context
		sysName     string
		poolID      string
		flags       daos.PoolConnectFlag
		checkParams func(t *testing.T)
		expHdl      *PoolHandle
		expErr      error
	}{
		"pool handle in context with non-empty ID": {
			ctx:    defaultPoolHandle().toCtx(test.Context(t)),
			poolID: testPoolName,
			expErr: errors.New("PoolHandle found in context with non-empty poolID"),
		},
		"pool handle in context": {
			ctx:    defaultPoolHandle().toCtx(test.Context(t)),
			expHdl: defaultPoolHandle(),
		},
		"pool handle not in context, no poolID": {
			ctx:    test.Context(t),
			expErr: errors.Wrap(daos.InvalidInput, "no pool ID provided"),
		},
		"pool not in context; pool connect fails": {
			ctx: test.Context(t),
			setup: func(t *testing.T) {
				daos_pool_connect_RC = -_Ctype_int(daos.IOError)
			},
			poolID: testPoolName,
			expErr: errors.Wrap(daos.IOError, "failed to connect to pool"),
		},
		"pool handle from Connect() with non-default sys name": {
			ctx:     test.Context(t),
			poolID:  daos_default_PoolInfo.Label,
			sysName: "non-default",
			checkParams: func(t *testing.T) {
				test.CmpAny(t, "poolID", daos_default_PoolInfo.Label, daos_pool_connect_SetPoolID)
				test.CmpAny(t, "sysName", "non-default", daos_pool_connect_SetSys)
				test.CmpAny(t, "flags", daos.PoolConnectFlagReadOnly, daos_pool_connect_SetFlags)
				test.CmpAny(t, "query", daos.PoolQueryMask(0), daos_pool_connect_QueryMask)
			},
			expHdl: defaultPoolHandle(),
		},
		"pool handle from Connect()": {
			ctx:    test.Context(t),
			poolID: daos_default_PoolInfo.Label,
			checkParams: func(t *testing.T) {
				test.CmpAny(t, "poolID", daos_default_PoolInfo.Label, daos_pool_connect_SetPoolID)
				test.CmpAny(t, "sysName", "", daos_pool_connect_SetSys)
				test.CmpAny(t, "flags", daos.PoolConnectFlagReadOnly, daos_pool_connect_SetFlags)
				test.CmpAny(t, "query", daos.PoolQueryMask(0), daos_pool_connect_QueryMask)
			},
			expHdl: defaultPoolHandle(),
		},
	} {
		t.Run(name, func(t *testing.T) {
			t.Cleanup(ResetTestStubs)
			if tc.setup != nil {
				tc.setup(t)
			}

			ctx := tc.ctx
			if ctx == nil {
				ctx = test.Context(t)
			}

			if tc.checkParams != nil {
				defer tc.checkParams(t)
			}

			ph, cleanup, gotErr := getPoolConn(test.MustLogContext(t, ctx), tc.sysName, tc.poolID, tc.flags)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
			t.Cleanup(cleanup)

			cmpOpts := cmp.Options{
				cmp.Comparer(func(a, b *PoolHandle) bool {
					return a != nil && b != nil && a.String() == b.String()
				}),
			}
			test.CmpAny(t, "PoolHandle", tc.expHdl, ph, cmpOpts...)
		})
	}
}

func TestAPI_PoolQuery(t *testing.T) {
	for name, tc := range map[string]struct {
		setup       func(t *testing.T)
		ctx         context.Context
		poolID      string
		queryMask   daos.PoolQueryMask
		checkParams func(t *testing.T)
		expResp     *daos.PoolInfo
		expErr      error
	}{
		"nil context": {
			expErr: errNilCtx,
		},
		"daos_pool_query() fails": {
			setup: func(t *testing.T) {
				daos_pool_query_RC = -_Ctype_int(daos.IOError)
			},
			ctx:    test.Context(t),
			poolID: daos_default_PoolInfo.Label,
			expErr: errors.Wrap(daos.IOError, "failed to query pool"),
		},
		"daos_pool_query() fails on enabled ranks": {
			setup: func(t *testing.T) {
				daos_pool_query_RC = -_Ctype_int(daos.IOError)
			},
			ctx:       test.Context(t),
			poolID:    daos_default_PoolInfo.Label,
			queryMask: daos.MustNewPoolQueryMask(daos.PoolQueryOptionEnabledEngines, daos.PoolQueryOptionDisabledEngines),
			expErr:    errors.Wrap(daos.IOError, "failed to query pool"),
		},
		"unspecified query mask": {
			ctx: defaultPoolHandle().toCtx(test.Context(t)),
			expResp: func() *daos.PoolInfo {
				out := defaultPoolInfo()
				out.QueryMask = daos.DefaultPoolQueryMask
				out.EnabledRanks = nil
				return out
			}(),
		},
		"default query mask": {
			ctx:       defaultPoolHandle().toCtx(test.Context(t)),
			queryMask: daos.DefaultPoolQueryMask,
			expResp: func() *daos.PoolInfo {
				out := defaultPoolInfo()
				out.QueryMask = daos.DefaultPoolQueryMask
				out.EnabledRanks = nil
				return out
			}(),
		},
		"health-only query mask": {
			ctx:       defaultPoolHandle().toCtx(test.Context(t)),
			queryMask: daos.HealthOnlyPoolQueryMask,
			expResp: func() *daos.PoolInfo {
				out := defaultPoolInfo()
				out.QueryMask = daos.HealthOnlyPoolQueryMask
				out.EnabledRanks = nil
				out.TierStats = nil
				return out
			}(),
		},
		"enabled ranks": {
			ctx:       defaultPoolHandle().toCtx(test.Context(t)),
			queryMask: daos.MustNewPoolQueryMask(daos.PoolQueryOptionEnabledEngines),
			expResp: func() *daos.PoolInfo {
				out := defaultPoolInfo()
				out.QueryMask = daos.MustNewPoolQueryMask(daos.PoolQueryOptionEnabledEngines)
				out.DisabledRanks = nil
				out.TierStats = nil
				return out
			}(),
		},
		"enabled & disabled ranks": {
			ctx:       defaultPoolHandle().toCtx(test.Context(t)),
			queryMask: daos.MustNewPoolQueryMask(daos.PoolQueryOptionEnabledEngines, daos.PoolQueryOptionDisabledEngines),
			expResp: func() *daos.PoolInfo {
				out := defaultPoolInfo()
				out.QueryMask = daos.MustNewPoolQueryMask(daos.PoolQueryOptionEnabledEngines, daos.PoolQueryOptionDisabledEngines)
				out.TierStats = nil
				return out
			}(),
		},
		"space-only": {
			ctx:       defaultPoolHandle().toCtx(test.Context(t)),
			queryMask: daos.MustNewPoolQueryMask(daos.PoolQueryOptionSpace),
			expResp: func() *daos.PoolInfo {
				out := defaultPoolInfo()
				out.QueryMask = daos.MustNewPoolQueryMask(daos.PoolQueryOptionSpace)
				out.EnabledRanks = nil
				out.DisabledRanks = nil
				return out
			}(),
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

			gotResp, err := PoolQuery(test.MustLogContext(t, tc.ctx), "", tc.poolID, tc.queryMask)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			cmpOpts := cmp.Options{
				cmp.Comparer(func(a, b ranklist.RankSet) bool {
					return a.String() == b.String()
				}),
			}
			test.CmpAny(t, "PoolQuery() PoolInfo", tc.expResp, gotResp, cmpOpts...)
		})
	}
}

func TestAPI_PoolQueryTargets(t *testing.T) {
	allTgtCt := daos_default_PoolInfo.TotalTargets / daos_default_PoolInfo.TotalEngines

	for name, tc := range map[string]struct {
		setup       func(t *testing.T)
		ctx         context.Context
		poolID      string
		rank        ranklist.Rank
		targets     *ranklist.RankSet
		checkParams func(t *testing.T)
		expResp     []*daos.PoolQueryTargetInfo
		expErr      error
	}{
		"nil context": {
			expErr: errNilCtx,
		},
		"daos_pool_query() fails": {
			setup: func(t *testing.T) {
				daos_pool_query_RC = -_Ctype_int(daos.IOError)
			},
			ctx:    test.Context(t),
			poolID: daos_default_PoolInfo.Label,
			expErr: errors.Wrap(daos.IOError, "failed to query pool"),
		},
		"daos_pool_query_target() fails": {
			setup: func(t *testing.T) {
				daos_pool_query_target_RC = -_Ctype_int(daos.IOError)
			},
			ctx:    test.Context(t),
			poolID: daos_default_PoolInfo.Label,
			expErr: daos.IOError,
		},
		"pool query returns zero targets": {
			setup: func(t *testing.T) {
				daos_pool_query_PoolInfo = defaultPoolInfo()
				daos_pool_query_PoolInfo.TotalTargets = 0
			},
			ctx:    test.Context(t),
			poolID: daos_default_PoolInfo.Label,
			expErr: errors.New("failed to derive target count"),
		},
		"pool query returns zero engines": {
			setup: func(t *testing.T) {
				daos_pool_query_PoolInfo = defaultPoolInfo()
				daos_pool_query_PoolInfo.TotalEngines = 0
			},
			ctx:    test.Context(t),
			poolID: daos_default_PoolInfo.Label,
			expErr: errors.New("failed to derive target count"),
		},
		"nil target set gets all": {
			ctx:     test.Context(t),
			poolID:  daos_default_PoolInfo.Label,
			rank:    1,
			targets: nil,
			checkParams: func(t *testing.T) {
				test.CmpAny(t, "rank", _Ctype_uint32_t(1), daos_pool_query_target_SetRank)
				test.CmpAny(t, "last target", _Ctype_uint32_t(allTgtCt-1), daos_pool_query_target_SetTgt)
			},
			expResp: func() []*daos.PoolQueryTargetInfo {
				infos := make([]*daos.PoolQueryTargetInfo, allTgtCt)
				for i := range infos {
					infos[i] = &daos_default_PoolQueryTargetInfo
				}
				return infos
			}(),
		},
		"empty target set gets all": {
			ctx:     test.Context(t),
			poolID:  daos_default_PoolInfo.Label,
			rank:    1,
			targets: ranklist.NewRankSet(),
			checkParams: func(t *testing.T) {
				test.CmpAny(t, "rank", _Ctype_uint32_t(1), daos_pool_query_target_SetRank)
				test.CmpAny(t, "last target", _Ctype_uint32_t(allTgtCt-1), daos_pool_query_target_SetTgt)
			},
			expResp: func() []*daos.PoolQueryTargetInfo {
				infos := make([]*daos.PoolQueryTargetInfo, allTgtCt)
				for i := range infos {
					infos[i] = &daos_default_PoolQueryTargetInfo
				}
				return infos
			}(),
		},
		"specified target should not query pool for target list": {
			setup: func(t *testing.T) {
				daos_pool_query_RC = -_Ctype_int(daos.IOError) // fail if the pool is queried
			},
			ctx:     test.Context(t),
			poolID:  daos_default_PoolInfo.Label,
			rank:    1,
			targets: ranklist.MustCreateRankSet("1"),
			checkParams: func(t *testing.T) {
				test.CmpAny(t, "rank", _Ctype_uint32_t(1), daos_pool_query_target_SetRank)
				test.CmpAny(t, "last target", _Ctype_uint32_t(1), daos_pool_query_target_SetTgt)
			},
			expResp: func() []*daos.PoolQueryTargetInfo {
				infos := make([]*daos.PoolQueryTargetInfo, 1)
				for i := range infos {
					infos[i] = &daos_default_PoolQueryTargetInfo
				}
				return infos
			}(),
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

			gotResp, err := PoolQueryTargets(test.MustLogContext(t, tc.ctx), "", tc.poolID, tc.rank, tc.targets)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			cmpOpts := cmp.Options{
				cmp.Comparer(func(a, b ranklist.RankSet) bool {
					return a.String() == b.String()
				}),
			}
			test.CmpAny(t, "PoolQueryTargets() response", tc.expResp, gotResp, cmpOpts...)
		})
	}
}

func TestAPI_PoolListAttributes(t *testing.T) {
	for name, tc := range map[string]struct {
		setup    func(t *testing.T)
		ctx      context.Context
		poolID   string
		expNames []string
		expErr   error
	}{
		"nil context": {
			expErr: errNilCtx,
		},
		"daos_pool_list_attr() fails (get buf size)": {
			setup: func(t *testing.T) {
				daos_pool_list_attr_RC = -_Ctype_int(daos.IOError)
			},
			ctx:    defaultPoolHandle().toCtx(test.Context(t)),
			expErr: errors.Wrap(daos.IOError, "failed to list pool attributes"),
		},
		"daos_pool_list_attr() fails (fetch names)": {
			setup: func(t *testing.T) {
				daos_pool_list_attr_RCList = []_Ctype_int{
					0,
					-_Ctype_int(daos.IOError),
				}
			},
			ctx:    defaultPoolHandle().toCtx(test.Context(t)),
			expErr: errors.Wrap(daos.IOError, "failed to list pool attributes"),
		},
		"no attributes set": {
			setup: func(t *testing.T) {
				daos_pool_list_attr_AttrList = nil
			},
			ctx: defaultPoolHandle().toCtx(test.Context(t)),
		},
		"success": {
			ctx: defaultPoolHandle().toCtx(test.Context(t)),
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

			gotNames, err := PoolListAttributes(test.MustLogContext(t, tc.ctx), "", tc.poolID)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			test.CmpAny(t, "PoolListAttributes()", tc.expNames, gotNames)
		})
	}
}

func TestAPI_PoolGetAttributes(t *testing.T) {
	for name, tc := range map[string]struct {
		setup       func(t *testing.T)
		ctx         context.Context
		poolID      string
		attrNames   []string
		checkParams func(t *testing.T)
		expAttrs    daos.AttributeList
		expErr      error
	}{
		"nil context": {
			expErr: errNilCtx,
		},
		"daos_pool_list_attr() fails": {
			setup: func(t *testing.T) {
				daos_pool_list_attr_RC = -_Ctype_int(daos.IOError)
			},
			ctx:    defaultPoolHandle().toCtx(test.Context(t)),
			expErr: errors.Wrap(daos.IOError, "failed to list pool attributes"),
		},
		"daos_pool_get_attr() fails (sizes)": {
			setup: func(t *testing.T) {
				daos_pool_get_attr_RC = -_Ctype_int(daos.IOError)
			},
			ctx:    defaultPoolHandle().toCtx(test.Context(t)),
			expErr: errors.Wrap(daos.IOError, "failed to get pool attribute sizes"),
		},
		"daos_pool_get_attr() fails (values)": {
			setup: func(t *testing.T) {
				daos_pool_get_attr_RCList = []_Ctype_int{
					0,
					-_Ctype_int(daos.IOError),
				}
			},
			ctx:    defaultPoolHandle().toCtx(test.Context(t)),
			expErr: errors.Wrap(daos.IOError, "failed to get pool attribute values"),
		},
		"empty requested attribute name": {
			ctx:       defaultPoolHandle().toCtx(test.Context(t)),
			attrNames: test.JoinArgs(nil, "a", ""),
			expErr:    errors.Errorf("empty pool attribute name at index 1"),
		},
		"no attributes set; attributes requested": {
			setup: func(t *testing.T) {
				daos_pool_get_attr_AttrList = nil
			},
			ctx:       defaultPoolHandle().toCtx(test.Context(t)),
			attrNames: test.JoinArgs(nil, "foo"),
			checkParams: func(t *testing.T) {
				test.CmpAny(t, "req attr names", map[string]struct{}{"foo": {}}, daos_pool_get_attr_ReqNames)
			},
			expErr: errors.Wrap(daos.Nonexistent, "failed to get pool attribute sizes"),
		},
		"unknown attribute requested": {
			ctx:       defaultPoolHandle().toCtx(test.Context(t)),
			attrNames: test.JoinArgs(nil, "foo"),
			checkParams: func(t *testing.T) {
				test.CmpAny(t, "req attr names", map[string]struct{}{"foo": {}}, daos_pool_get_attr_ReqNames)
			},
			expErr: errors.Wrap(daos.Nonexistent, "failed to get pool attribute sizes"),
		},
		"no attributes set; no attributes requested": {
			setup: func(t *testing.T) {
				daos_pool_list_attr_AttrList = nil
			},
			ctx: defaultPoolHandle().toCtx(test.Context(t)),
		},
		"success; all attributes": {
			ctx:      defaultPoolHandle().toCtx(test.Context(t)),
			expAttrs: daos_default_AttrList,
		},
		"success; requested attributes": {
			ctx:       defaultPoolHandle().toCtx(test.Context(t)),
			attrNames: test.JoinArgs(nil, daos_default_AttrList[0].Name, daos_default_AttrList[2].Name),
			checkParams: func(t *testing.T) {
				reqNames := test.JoinArgs(nil, daos_default_AttrList[0].Name, daos_default_AttrList[2].Name)
				sort.Strings(reqNames)
				gotNames := daos_test_get_mappedNames(daos_pool_get_attr_ReqNames)
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

			gotAttrs, err := PoolGetAttributes(test.MustLogContext(t, tc.ctx), "", tc.poolID, tc.attrNames...)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			test.CmpAny(t, "PoolGetAttributes() daos.AttributeList", tc.expAttrs, gotAttrs)
		})
	}
}

func TestAPI_PoolSetAttributes(t *testing.T) {
	for name, tc := range map[string]struct {
		setup  func(t *testing.T)
		ctx    context.Context
		poolID string
		toSet  daos.AttributeList
		expErr error
	}{
		"nil context": {
			expErr: errNilCtx,
		},
		"no attributes to set": {
			ctx:    defaultPoolHandle().toCtx(test.Context(t)),
			expErr: errors.Wrap(daos.InvalidInput, "no pool attributes provided"),
		},
		"nil toSet attribute": {
			ctx:    defaultPoolHandle().toCtx(test.Context(t)),
			toSet:  append(daos_default_AttrList, nil),
			expErr: errors.Wrap(daos.InvalidInput, "nil pool attribute at index 3"),
		},
		"toSet attribute with empty name": {
			ctx:    defaultPoolHandle().toCtx(test.Context(t)),
			toSet:  append(daos_default_AttrList, &daos.Attribute{Name: ""}),
			expErr: errors.Wrap(daos.InvalidInput, "empty pool attribute name at index 3"),
		},
		"toSet attribute with empty value": {
			ctx:    defaultPoolHandle().toCtx(test.Context(t)),
			toSet:  append(daos_default_AttrList, &daos.Attribute{Name: "empty"}),
			expErr: errors.Wrap(daos.InvalidInput, "empty pool attribute value at index 3"),
		},
		"daos_pool_set_attr() fails": {
			setup: func(t *testing.T) {
				daos_pool_set_attr_RC = -_Ctype_int(daos.IOError)
			},
			ctx:    defaultPoolHandle().toCtx(test.Context(t)),
			toSet:  daos_default_AttrList,
			expErr: errors.Wrap(daos.IOError, "failed to set pool attributes"),
		},
		"success": {
			ctx:   defaultPoolHandle().toCtx(test.Context(t)),
			toSet: daos_default_AttrList,
		},
	} {
		t.Run(name, func(t *testing.T) {
			t.Cleanup(ResetTestStubs)
			if tc.setup != nil {
				tc.setup(t)
			}

			err := PoolSetAttributes(test.MustLogContext(t, tc.ctx), "", tc.poolID, tc.toSet...)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			test.CmpAny(t, "PoolSetAttributes() daos.AttributeList", tc.toSet, daos_pool_set_attr_AttrList)
		})
	}
}

func TestAPI_PoolDeleteAttributes(t *testing.T) {
	for name, tc := range map[string]struct {
		setup    func(t *testing.T)
		ctx      context.Context
		poolID   string
		toDelete []string
		expErr   error
	}{
		"nil context": {
			expErr: errNilCtx,
		},
		"no attributes to delete": {
			ctx:    defaultPoolHandle().toCtx(test.Context(t)),
			expErr: errors.Wrap(daos.InvalidInput, "no pool attribute names provided"),
		},
		"empty name in toDelete list": {
			ctx:      defaultPoolHandle().toCtx(test.Context(t)),
			toDelete: test.JoinArgs(nil, "foo", "", "bar"),
			expErr:   errors.Wrap(daos.InvalidInput, "empty pool attribute name at index 1"),
		},
		"daos_pool_del_attr() fails": {
			setup: func(t *testing.T) {
				daos_pool_del_attr_RC = -_Ctype_int(daos.IOError)
			},
			ctx:      defaultPoolHandle().toCtx(test.Context(t)),
			toDelete: test.JoinArgs(nil, daos_default_AttrList[0].Name),
			expErr:   errors.Wrap(daos.IOError, "failed to delete pool attributes"),
		},
		"success": {
			ctx:      defaultPoolHandle().toCtx(test.Context(t)),
			toDelete: test.JoinArgs(nil, daos_default_AttrList[0].Name),
		},
	} {
		t.Run(name, func(t *testing.T) {
			t.Cleanup(ResetTestStubs)
			if tc.setup != nil {
				tc.setup(t)
			}

			err := PoolDeleteAttributes(test.MustLogContext(t, tc.ctx), "", tc.poolID, tc.toDelete...)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			test.CmpAny(t, "PoolDeleteAttributes() AttrNames", tc.toDelete, daos_pool_del_attr_AttrNames)
		})
	}
}

func TestAPI_PoolHandleMethods(t *testing.T) {
	thType := reflect.TypeOf(defaultPoolHandle())
	for i := 0; i < thType.NumMethod(); i++ {
		method := thType.Method(i)
		methArgs := make([]reflect.Value, 0)
		var expResults int

		switch method.Name {
		case "Disconnect":
			expResults = 1
		case "Query":
			methArgs = append(methArgs, reflect.ValueOf(daos.DefaultPoolQueryMask))
			expResults = 2
		case "QueryTargets":
			methArgs = append(methArgs, reflect.ValueOf(ranklist.Rank(1)), reflect.ValueOf((*ranklist.RankSet)(nil)))
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
		case "ListContainers":
			methArgs = append(methArgs, reflect.ValueOf(true))
			expResults = 2
		case "DestroyContainer":
			methArgs = append(methArgs, reflect.ValueOf("foo"), reflect.ValueOf(true))
			expResults = 1
		case "QueryContainer":
			methArgs = append(methArgs, reflect.ValueOf("foo"))
			expResults = 2
		case "OpenContainer":
			methArgs = append(methArgs, reflect.ValueOf(ContainerOpenReq{ID: "foo"}))
			expResults = 2
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
			th     *PoolHandle
			expErr error
		}{
			fmt.Sprintf("%s: nil handle", method.Name): {
				th:     nil,
				expErr: ErrInvalidPoolHandle,
			},
			fmt.Sprintf("%s: success", method.Name): {
				th: defaultPoolHandle(),
			},
		} {
			t.Run(name, func(t *testing.T) {
				thArg := reflect.ValueOf(tc.th)
				if tc.th == nil {
					thArg = reflect.New(thType).Elem()
				}
				ctxArg := reflect.ValueOf(test.Context(t))
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

func TestAPI_GetPoolList(t *testing.T) {
	defaultReq := GetPoolListReq{
		SysName: "non-default",
		Query:   true,
	}
	defaultPoolInfoResp := []*daos.PoolInfo{
		{
			State:           daos.PoolServiceStateReady,
			UUID:            daos_default_PoolInfo.UUID,
			Label:           daos_default_PoolInfo.Label,
			ServiceReplicas: daos_default_PoolInfo.ServiceReplicas,
			ServiceLeader:   daos_default_PoolInfo.ServiceLeader,
		},
	}

	for name, tc := range map[string]struct {
		setup       func(t *testing.T)
		ctx         context.Context
		req         GetPoolListReq
		checkParams func(t *testing.T)
		expPools    []*daos.PoolInfo
		expErr      error
	}{
		"nil context": {
			expErr: errNilCtx,
		},
		"daos_mgmt_list_pools fails (sizes)": {
			setup: func(t *testing.T) {
				daos_mgmt_list_pools_RC = -_Ctype_int(daos.IOError)
			},
			ctx:    test.Context(t),
			expErr: errors.Wrap(daos.IOError, "failed to list pools"),
		},
		"daos_mgmt_list_pools fetch fails (not retryable)": {
			setup: func(t *testing.T) {
				daos_mgmt_list_pools_RCList = []_Ctype_int{
					0,
					-_Ctype_int(daos.NoMemory),
				}
			},
			ctx:    test.Context(t),
			expErr: errors.Wrap(daos.NoMemory, "failed to list pools"),
		},
		"daos_pool_connect fails": {
			setup: func(t *testing.T) {
				daos_pool_connect_RC = -_Ctype_int(daos.IOError)
			},
			ctx:      test.Context(t),
			req:      defaultReq,
			expPools: []*daos.PoolInfo{},
		},
		"daos_mgmt_list_pools fetch fails (retryable)": {
			setup: func(t *testing.T) {
				daos_mgmt_list_pools_RCList = []_Ctype_int{
					0,
					-_Ctype_int(daos.StructTooSmall),
					0,
					0,
				}
			},
			ctx:      test.Context(t),
			expPools: defaultPoolInfoResp,
		},
		"default system name supplied": {
			ctx: test.Context(t),
			req: GetPoolListReq{},
			checkParams: func(t *testing.T) {
				test.CmpAny(t, "sysName", build.DefaultSystemName, daos_mgmt_list_pools_SetSys)
			},
			expPools: defaultPoolInfoResp,
		},
		"success (no pools)": {
			setup: func(t *testing.T) {
				daos_mgmt_list_pools_RetPools = nil
			},
			ctx: test.Context(t),
			req: defaultReq,
		},
		"success (no query)": {
			ctx: test.Context(t),
			req: GetPoolListReq{
				SysName: defaultReq.SysName,
			},
			checkParams: func(t *testing.T) {
				test.CmpAny(t, "sysName", defaultReq.SysName, daos_mgmt_list_pools_SetSys)
			},
			expPools: defaultPoolInfoResp,
		},
		"success (query)": {
			ctx: test.Context(t),
			req: defaultReq,
			checkParams: func(t *testing.T) {
				test.CmpAny(t, "sysName", defaultReq.SysName, daos_mgmt_list_pools_SetSys)
			},
			expPools: func() []*daos.PoolInfo {
				pi := copyPoolInfo(&daos_default_PoolInfo)
				pi.EnabledRanks = nil
				pi.DisabledRanks = nil

				return []*daos.PoolInfo{pi}
			}(),
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

			gotPools, err := GetPoolList(test.MustLogContext(t, tc.ctx), tc.req)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			test.CmpAny(t, "GetPoolList() PoolList", tc.expPools, gotPools)
		})
	}
}
