//
// (C) Copyright 2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"strings"
	"syscall"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/daos"
	daosAPI "github.com/daos-stack/daos/src/control/lib/daos/client"
	apiMocks "github.com/daos-stack/daos/src/control/lib/daos/client/mocks"
	"github.com/daos-stack/daos/src/control/lib/dfs"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestDaos_existingContainerCmd_parseContPathArgs(t *testing.T) {
	for name, tc := range map[string]struct {
		inArgs   []string
		contPath string
		poolArg  string
		contArg  string
		expArgs  []string
		expErr   error
	}{
		"no args": {},
		"pool UUID only": {
			poolArg: test.MockUUID(1),
		},
		"pool label only": {
			poolArg: "pool1",
		},
		"cont UUID only": {
			contArg: test.MockUUID(2),
		},
		"cont label only": {
			contArg: "cont1",
		},
		"pool UUID + cont UUID": {
			poolArg: test.MockUUID(1),
			contArg: test.MockUUID(2),
		},
		"pool label + cont label": {
			poolArg: "pool1",
			contArg: "cont1",
		},
		"bad pool label + good cont label": {
			poolArg: "pool1$bad",
			contArg: "cont1",
			expErr:  errors.New("invalid label"),
		},
		"good pool label + bad cont label": {
			poolArg: "pool1",
			contArg: "cont1#bad",
			expErr:  errors.New("invalid label"),
		},
		"path + 1 arg": {
			contPath: "/path/to/cont",
			poolArg:  "foo:bar,baz:qux",
			expArgs:  []string{"foo:bar,baz:qux"},
		},
		"path + 2 args": {
			contPath: "/path/to/cont",
			poolArg:  "foo:bar,baz:qux",
			contArg:  "quux:quuz",
			expArgs:  []string{"quux:quuz", "foo:bar,baz:qux"},
		},
		"inArgs + path + 1 arg": {
			contPath: "/path/to/cont",
			inArgs:   []string{"foo:bar,baz:qux"},
			poolArg:  "cow:dog,quack:blam",
			expArgs:  []string{"foo:bar,baz:qux", "cow:dog,quack:blam"},
		},
	} {
		t.Run(name, func(t *testing.T) {
			cmd := &existingContainerCmd{
				Path: tc.contPath,
			}
			if poolUUID, err := uuid.Parse(tc.poolArg); err == nil {
				cmd.poolBaseCmd.Args.Pool.UUID = poolUUID
			} else if err := cmd.poolBaseCmd.Args.Pool.SetLabel(tc.poolArg); err != nil {
				cmd.poolBaseCmd.Args.Pool.unparsedArg = tc.poolArg
			}
			if contUUID, err := uuid.Parse(tc.contArg); err == nil {
				cmd.Args.Container.UUID = contUUID
			} else if err := cmd.Args.Container.SetLabel(tc.contArg); err != nil {
				cmd.Args.Container.unparsedArg = tc.contArg
			}

			got, err := cmd.parseContPathArgs(tc.inArgs)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}
			if diff := cmp.Diff(tc.expArgs, got); diff != "" {
				t.Fatalf("unexpected args (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestDaos_existingContainerCmd_resolveContainer(t *testing.T) {
	connPool := test.MockPoolUUID(1)
	connCont := test.MockPoolUUID(2)

	for name, tc := range map[string]struct {
		daosCtx  context.Context
		contType string
		contPath string
		poolID   string
		contID   string
		expErr   error
	}{
		"no path, no pool, no cont": {
			expErr: errors.New("no container label or UUID"),
		},
		"bad dfuse path; bad DUNS path (POSIX)": {
			daosCtx: func() context.Context {
				if ctx, err := daosAPI.MockApiClientContext(context.Background(), &daosAPI.MockApiClientConfig{
					ReturnCodeMap: map[string]int{
						"dfuse_ioctl":       int(syscall.ENOTTY),
						"duns_resolve_path": int(syscall.ENODATA),
					},
				}); err == nil {
					return ctx
				} else {
					t.Fatal(err)
					return nil
				}
			}(),
			contPath: "path",
			contType: "POSIX",
			expErr:   daos.BadPath,
		},
		"bad dfuse path; valid DUNS path (POSIX)": {
			daosCtx: func() context.Context {
				if ctx, err := daosAPI.MockApiClientContext(context.Background(), &daosAPI.MockApiClientConfig{
					ReturnCodeMap: map[string]int{
						"dfuse_ioctl": int(syscall.ENOTTY),
					},
					ConnectedPool: connPool,
					ConnectedCont: connCont,
				}); err == nil {
					return ctx
				} else {
					t.Fatal(err)
					return nil
				}
			}(),
			contPath: "path",
			contType: "POSIX",
			poolID:   connPool.String(),
			contID:   connCont.String(),
		},
		"valid DUNS path (POSIX)": {
			daosCtx: func() context.Context {
				if ctx, err := daosAPI.MockApiClientContext(context.Background(), &daosAPI.MockApiClientConfig{
					ConnectedPool: connPool,
					ConnectedCont: connCont,
				}); err == nil {
					return ctx
				} else {
					t.Fatal(err)
					return nil
				}
			}(),
			contPath: "path",
			contType: "POSIX",
			poolID:   connPool.String(),
			contID:   connCont.String(),
		},
		"pool UUID + cont UUID": {
			daosCtx: func() context.Context {
				if ctx, err := daosAPI.MockApiClientContext(context.Background(), &daosAPI.MockApiClientConfig{
					ReturnCodeMap: map[string]int{
						"dfuse_ioctl": int(syscall.ENOTTY),
					},
					ConnectedPool: connPool,
					ConnectedCont: connCont,
				}); err == nil {
					return ctx
				} else {
					t.Fatal(err)
					return nil
				}
			}(),
			poolID: connPool.String(),
			contID: connCont.String(),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			cmd := &existingContainerCmd{
				Path: tc.contPath,
			}
			if tc.daosCtx == nil {
				tc.daosCtx = func() context.Context {
					ctx, err := daosAPI.MockApiClientContext(context.Background(), nil)
					if err != nil {
						t.Fatal(err)
					}
					return ctx
				}()
			}
			cmd.daosCtx = tc.daosCtx
			cmd.SetLog(log)
			daosAPI.SetDebugLog(log)

			if cmd.Path == "" {
				if tc.poolID != "" {
					gotErr := cmd.poolBaseCmd.Args.Pool.UnmarshalFlag(tc.poolID)
					test.CmpErr(t, tc.expErr, gotErr)
					if tc.expErr != nil {
						return
					}
				}
				if tc.contID != "" {
					gotErr := cmd.Args.Container.UnmarshalFlag(tc.contID)
					test.CmpErr(t, tc.expErr, gotErr)
					if tc.expErr != nil {
						return
					}
				}
			}

			ap, deallocCmdArgs, err := allocCmdArgs(cmd.Logger)
			if err != nil {
				t.Fatal(err)
			}
			defer deallocCmdArgs()

			gotErr := cmd.resolveContainer(ap)
			test.CmpErr(t, tc.expErr, gotErr)
			test.AssertEqual(t, tc.poolID, cmd.poolBaseCmd.Args.Pool.String(), "PoolID")
			test.AssertEqual(t, tc.contID, cmd.Args.Container.String(), "ContainerID")
		})
	}
}

func makeArgs(base []string, in ...string) []string {
	return append(base, in...)
}

func objClass(t *testing.T, str string) daosAPI.ObjectClass {
	t.Helper()

	var cls daosAPI.ObjectClass
	if err := cls.FromString(str); err != nil {
		t.Fatal(err)
	}

	return cls
}

func mockApiCtx(t *testing.T, cfg *daosAPI.MockApiClientConfig) context.Context {
	t.Helper()

	ctx, err := daosAPI.MockApiClientContext(test.Context(t), cfg)
	if err != nil {
		t.Fatal(err)
	}

	return ctx
}

func TestDaos_ContainerCreateCmd(t *testing.T) {
	flagTestFini, err := flagTestInit()
	if err != nil {
		t.Fatal(err)
	}
	defer flagTestFini()

	tmpDir := t.TempDir()
	goodACL := test.CreateTestFile(t, tmpDir, "A::OWNER@:rw\nA::user1@:rw\nA:g:group1@:r\n")

	baseArgs := makeArgs(nil, "container", "create")
	poolUUID := test.MockPoolUUID(1)
	poolLabel := "poolLabel"
	contUUID := test.MockPoolUUID(2)
	contLabel := "contLabel"
	consMode := dfs.ConsistencyModeBalanced
	contHints := "dir:single,file:max"
	defOClass := objClass(t, "SX")
	dirOClass := objClass(t, "RP_2G1")
	fileOClass := objClass(t, "EC_2P1GX")
	testProps := make(daosAPI.ContainerPropertySet)
	if err := testProps.AddValue("rd_fac", "1"); err != nil {
		t.Fatal(err)
	}
	if err := testProps.AddValue("ec_cell_sz", "131072"); err != nil {
		t.Fatal(err)
	}

	for name, tc := range map[string]struct {
		args     []string
		ctx      context.Context
		mpcc     *apiMocks.PoolConnCfg
		expInfo  *daosAPI.ContainerInfo
		expProps daosAPI.ContainerPropertySet
		expErr   error
	}{
		"no arguments given": {
			args:   baseArgs,
			expErr: errors.New("no pool ID"),
		},
		"label supplied as arg and prop": {
			args: makeArgs(baseArgs, poolLabel, contLabel,
				"--properties", "label:"+contLabel,
			),
			expErr: errors.New("can't supply"),
		},
		"invalid label": {
			args:   makeArgs(baseArgs, poolLabel, strings.Repeat("x", daos.MaxLabelLength+1)),
			expErr: errors.New("invalid label"),
		},
		"bad ACL path": {
			args:   makeArgs(baseArgs, poolLabel, contLabel, "--acl-file", "/bad/path"),
			expErr: errors.New("no such file"),
		},
		"pool connect fails with -DER_NOPERM": {
			args: makeArgs(baseArgs, poolLabel, contLabel),
			mpcc: &apiMocks.PoolConnCfg{
				Connect: apiMocks.Err{
					Error: daos.NoPermission,
				},
			},
			expErr: daos.NoPermission,
		},
		"create fails with -DER_NOPERM": {
			args: makeArgs(baseArgs, poolLabel, contLabel),
			mpcc: &apiMocks.PoolConnCfg{
				CreateContainer: apiMocks.ContainerInfoResp{
					Err: apiMocks.Err{
						Error: daos.NoPermission,
					},
				},
			},
			expErr: daos.NoPermission,
		},
		"create succeeds but open fails": {
			args: makeArgs(baseArgs, poolLabel, contLabel),
			mpcc: &apiMocks.PoolConnCfg{
				ConnectedPool: poolUUID,
				ContConnCfg: &apiMocks.ContConnCfg{
					ConnectedPool:      poolUUID,
					ConnectedContainer: contUUID,
				},
				OpenContainer: apiMocks.OpenContainerResp{
					Err: apiMocks.Err{
						Error: daos.IOError,
					},
				},
			},
			expErr: daos.IOError,
		},
		"create succeeds but open fails due to lack of permissions": {
			args: makeArgs(baseArgs, poolLabel, contLabel),
			mpcc: &apiMocks.PoolConnCfg{
				ConnectedPool: poolUUID,
				ContConnCfg: &apiMocks.ContConnCfg{
					ConnectedPool:      poolUUID,
					ConnectedContainer: contUUID,
				},
				OpenContainer: apiMocks.OpenContainerResp{
					Err: apiMocks.Err{
						Error: daos.NoPermission,
					},
				},
			},
			expInfo: &daosAPI.ContainerInfo{
				PoolUUID: poolUUID,
				UUID:     contUUID,
				Label:    contLabel,
			},
		},
		"create succeeds but query fails": {
			args: makeArgs(baseArgs, poolLabel, contLabel),
			mpcc: &apiMocks.PoolConnCfg{
				ConnectedPool: poolUUID,
				ContConnCfg: &apiMocks.ContConnCfg{
					ConnectedPool:      poolUUID,
					ConnectedContainer: contUUID,
					Query: apiMocks.ContainerInfoResp{
						Err: apiMocks.Err{
							Error: errors.New("whoops"),
						},
					},
				},
			},
			expErr: errors.New("whoops"),
		},
		"pooLabel/contLabel (posix)": {
			args: makeArgs(baseArgs, poolLabel, contLabel,
				"--type", daosAPI.ContainerLayoutPOSIX.String(),
				"--oclass", defOClass.String(),
				"--dir-oclass", dirOClass.String(),
				"--file-oclass", fileOClass.String(),
				"--chunk-size", "4MB",
				"--mode", consMode.String(),
				"--hints", contHints,
				"--properties", testProps.String(),
				"--acl-file", goodACL,
			),
			expInfo: &daosAPI.ContainerInfo{
				PoolUUID: poolUUID,
				UUID:     contUUID,
				Label:    contLabel,
				Type:     daosAPI.ContainerLayoutPOSIX,
				POSIXAttributes: &daosAPI.POSIXAttributes{
					ObjectClass:     defOClass,
					DirObjectClass:  dirOClass,
					FileObjectClass: fileOClass,
					ChunkSize:       4000000,
					ConsistencyMode: uint32(consMode),
					Hints:           contHints,
				},
			},
			expProps: func() daosAPI.ContainerPropertySet {
				props := make(daosAPI.ContainerPropertySet)
				for key, prop := range testProps {
					if err := props.AddValue(key, prop.String()); err != nil {
						t.Fatal(err)
					}
				}
				if err := props.AddValue("label", contLabel); err != nil {
					t.Fatal(err)
				}

				return props
			}(),
		},
		"invalid pool path": {
			args: makeArgs(baseArgs, "--path", "/path", contLabel),
			ctx: mockApiCtx(t, &daosAPI.MockApiClientConfig{
				ReturnCodeMap: map[string]int{
					"dfuse_open": int(syscall.ENOENT),
				},
			}),
			expErr: syscall.ENOENT,
		},
		"valid pool path": {
			args: makeArgs(baseArgs, "--path", "/path", contLabel),
			ctx: mockApiCtx(t, &daosAPI.MockApiClientConfig{
				ConnectedPool: poolUUID,
			}),
			expInfo: &daosAPI.ContainerInfo{
				PoolUUID: poolUUID,
				UUID:     contUUID,
				Label:    contLabel,
			},
		},
		"valid pool path and pool label": {
			args:   makeArgs(baseArgs, "--path", "/path", poolLabel, contLabel),
			expErr: errors.New("pool ID or path"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			var opts cliOptions

			parent := tc.ctx
			if parent == nil {
				parent = test.Context(t)
			}
			if tc.mpcc == nil {
				tc.mpcc = &apiMocks.PoolConnCfg{
					ConnectedPool: poolUUID,
					ContConnCfg: &apiMocks.ContConnCfg{
						ConnectedPool:      poolUUID,
						ConnectedContainer: contUUID,
					},
				}
			}
			ctx := apiMocks.PoolConnCtx(parent, tc.mpcc)
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			gotErr := parseOpts(ctx, tc.args, &opts, log)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expInfo, tc.mpcc.CreateContainer.ContainerInfo); diff != "" {
				t.Fatalf("unexpected container info (-want,+got): %s", diff)
			}

			cmpOpts := []cmp.Option{
				cmp.Comparer(func(a, b daosAPI.ContainerPropertySet) bool {
					return a.String() == b.String()
				}),
			}
			if diff := cmp.Diff(tc.expProps, tc.mpcc.ContConnCfg.GetProperties.Properties, cmpOpts...); diff != "" {
				t.Fatalf("unexpected container props (-want,+got): %s", diff)
			}
		})
	}
}
