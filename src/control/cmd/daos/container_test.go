//
// (C) Copyright 2023 Intel Corporation.
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/daos/api"
	"github.com/daos-stack/daos/src/control/lib/ui"
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
	for name, tc := range map[string]struct {
		contPath string
		contType string
		poolID   string
		contID   string
		expErr   error
	}{
		"no path, no pool, no cont": {
			expErr: errors.New("no container label or UUID"),
		},
		"bad DUNS path (POSIX)": {
			contType: "POSIX",
			contPath: "bad-path",
			expErr:   errors.New("DER_NONEXIST"),
		},
		"valid DUNS path (POSIX)": {
			contType: "POSIX",
			contPath: "valid-path",
			poolID:   test.MockUUID(1),
			contID:   test.MockUUID(2),
		},
		"pool UUID + cont UUID": {
			poolID: test.MockUUID(1),
			contID: test.MockUUID(2),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			cmd := &existingContainerCmd{
				Path: tc.contPath,
			}
			cmd.SetLog(log)

			if tc.contPath != "" && tc.contPath != "bad-path" {
				path := filepath.Join(t.TempDir(), tc.contPath)
				if tc.contType == "POSIX" {
					if err := os.MkdirAll(path, 0755); err != nil {
						t.Fatal(err)
					}
				}
				poolUUID, err := uuid.Parse(tc.poolID)
				if err != nil {
					t.Fatal(err)
				}
				contUUID, err := uuid.Parse(tc.contID)
				if err != nil {
					t.Fatal(err)
				}
				if err := _writeDunsPath(path, tc.contType, poolUUID, contUUID); err != nil {
					t.Fatal(err)
				}
				cmd.Path = path
			} else {
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

var (
	defaultContInfo *daos.ContainerInfo = &daos.ContainerInfo{
		PoolUUID:       defaultPoolInfo.UUID,
		ContainerUUID:  test.MockPoolUUID(2),
		ContainerLabel: "test-container",
	}

	contOpenErr error
)

func ContainerOpen(ctx context.Context, req api.ContainerOpenReq) (*api.ContainerOpenResp, error) {
	return &api.ContainerOpenResp{
		Connection: api.MockContainerHandle(),
		Info:       defaultContInfo,
	}, contOpenErr
}

func TestDaos_containerSetAttrCmd(t *testing.T) {
	baseArgs := test.JoinArgs(nil, "container", "set-attr", defaultPoolInfo.Label, defaultContInfo.ContainerLabel)
	keysOnlyArg := "key1,key2"
	keyValArg := "key1:val1,key2:val2"

	for name, tc := range map[string]struct {
		args    []string
		expErr  error
		expArgs containerSetAttrCmd
		setup   func(t *testing.T)
	}{
		"invalid flag": {
			args:   test.JoinArgs(baseArgs, "--bad", keyValArg),
			expErr: errors.New("unknown flag"),
		},
		"open fails": {
			args:   test.JoinArgs(baseArgs, keyValArg),
			expErr: errors.New("whoops"),
			setup: func(t *testing.T) {
				prevErr := contOpenErr
				t.Cleanup(func() {
					contOpenErr = prevErr
				})
				contOpenErr = errors.New("whoops")
			},
		},
		"missing required arguments": {
			args:   baseArgs,
			expErr: errors.New("attribute name and value are required"),
		},
		"malformed required arguments": {
			args:   test.JoinArgs(baseArgs, keysOnlyArg),
			expErr: errors.New("invalid property"),
		},
		"success": {
			args: test.JoinArgs(baseArgs, keyValArg),
			expArgs: containerSetAttrCmd{
				Args: struct {
					Attrs ui.SetPropertiesFlag `positional-arg-name:"key:val[,key:val...]"`
				}{
					Attrs: ui.SetPropertiesFlag{
						ParsedProps: map[string]string{
							"key1": "val1",
							"key2": "val2",
						},
					},
				},
			},
		},
		"success (one key, deprecated flag)": {
			args: test.JoinArgs(baseArgs, "--attr", "one", "--value", "uno"),
			expArgs: containerSetAttrCmd{
				Args: struct {
					Attrs ui.SetPropertiesFlag `positional-arg-name:"key:val[,key:val...]"`
				}{
					Attrs: ui.SetPropertiesFlag{
						ParsedProps: map[string]string{
							"one": "uno",
						},
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			t.Cleanup(api.ResetTestStubs)
			if tc.setup != nil {
				tc.setup(t)
			}

			runCmdTest(t, tc.args, tc.expArgs, tc.expErr, "Container.SetAttribute")
		})
	}
}

func TestDaos_containerGetAttrCmd(t *testing.T) {
	baseArgs := test.JoinArgs(nil, "container", "get-attr", defaultPoolInfo.Label, defaultContInfo.ContainerLabel)
	keysOnlyArg := "key1,key2"

	for name, tc := range map[string]struct {
		args    []string
		expErr  error
		expArgs containerGetAttrCmd
		setup   func(t *testing.T)
	}{
		"invalid flag": {
			args:   test.JoinArgs(baseArgs, "--bad"),
			expErr: errors.New("unknown flag"),
		},
		"missing container ID": {
			args:   baseArgs[:len(baseArgs)-1],
			expErr: errors.New("no container label or UUID supplied"),
		},
		"connect fails": {
			args:   baseArgs,
			expErr: errors.New("whoops"),
			setup: func(t *testing.T) {
				prevErr := contOpenErr
				t.Cleanup(func() {
					contOpenErr = prevErr
				})
				contOpenErr = errors.New("whoops")
			},
		},
		"malformed arguments": {
			args:   test.JoinArgs(baseArgs, strings.ReplaceAll(keysOnlyArg, ",", ":")),
			expErr: errors.New("key cannot contain"),
		},
		"unknown key(s)": {
			args:   test.JoinArgs(baseArgs, keysOnlyArg),
			expErr: daos.Nonexistent,
		},
		"success (one key)": {
			args: test.JoinArgs(baseArgs, "one"),
			expArgs: containerGetAttrCmd{
				Args: struct {
					Attrs ui.GetPropertiesFlag `positional-arg-name:"key[,key...]"`
				}{
					Attrs: ui.GetPropertiesFlag{
						ParsedProps: map[string]struct{}{
							"one": {},
						},
					},
				},
			},
		},
		"success (one key, deprecated flag)": {
			args: test.JoinArgs(baseArgs, "--attr", "one"),
			expArgs: containerGetAttrCmd{
				Args: struct {
					Attrs ui.GetPropertiesFlag `positional-arg-name:"key[,key...]"`
				}{
					Attrs: ui.GetPropertiesFlag{
						ParsedProps: map[string]struct{}{
							"one": {},
						},
					},
				},
			},
		},
		"success (all keys)": {
			args:    baseArgs,
			expArgs: containerGetAttrCmd{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			t.Cleanup(api.ResetTestStubs)
			if tc.setup != nil {
				tc.setup(t)
			}

			runCmdTest(t, tc.args, tc.expArgs, tc.expErr, "Container.GetAttribute")
		})
	}
}

func TestDaos_containerDelAttrCmd(t *testing.T) {
	baseArgs := test.JoinArgs(nil, "container", "del-attr", defaultPoolInfo.Label, defaultContInfo.ContainerLabel)
	keysOnlyArg := "key1,key2"

	for name, tc := range map[string]struct {
		args    []string
		expErr  error
		expArgs containerDelAttrCmd
		setup   func(t *testing.T)
	}{
		"invalid flag": {
			args:   test.JoinArgs(baseArgs, "--bad"),
			expErr: errors.New("unknown flag"),
		},
		"missing required arguments": {
			args:   baseArgs,
			expErr: errors.New("attribute name is required"),
		},
		"connect fails": {
			args:   test.JoinArgs(baseArgs, keysOnlyArg),
			expErr: errors.New("whoops"),
			setup: func(t *testing.T) {
				prevErr := contOpenErr
				t.Cleanup(func() {
					contOpenErr = prevErr
				})
				contOpenErr = errors.New("whoops")
			},
		},
		"malformed arguments": {
			args:   test.JoinArgs(baseArgs, strings.ReplaceAll(keysOnlyArg, ",", ":")),
			expErr: errors.New("key cannot contain"),
		},
		"success (one key)": {
			args: test.JoinArgs(baseArgs, "one"),
			expArgs: containerDelAttrCmd{
				Args: struct {
					Attrs ui.GetPropertiesFlag `positional-arg-name:"key[,key...]"`
				}{
					Attrs: ui.GetPropertiesFlag{
						ParsedProps: map[string]struct{}{
							"one": {},
						},
					},
				},
			},
		},
		"success (deprecated flag)": {
			args: test.JoinArgs(baseArgs, "--attr", "one"),
			expArgs: containerDelAttrCmd{
				Args: struct {
					Attrs ui.GetPropertiesFlag `positional-arg-name:"key[,key...]"`
				}{
					Attrs: ui.GetPropertiesFlag{
						ParsedProps: map[string]struct{}{
							"one": {},
						},
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			t.Cleanup(api.ResetTestStubs)
			if tc.setup != nil {
				tc.setup(t)
			}

			runCmdTest(t, tc.args, tc.expArgs, tc.expErr, "Container.DeleteAttribute")
		})
	}
}

func TestDaos_containerListAttrCmd(t *testing.T) {
	baseArgs := test.JoinArgs(nil, "container", "list-attr", defaultPoolInfo.Label, defaultContInfo.ContainerLabel)

	for name, tc := range map[string]struct {
		args    []string
		expErr  error
		expArgs containerListAttrsCmd
		setup   func(t *testing.T)
	}{
		"invalid flag": {
			args:   test.JoinArgs(baseArgs, "--bad"),
			expErr: errors.New("unknown flag"),
		},
		"missing container ID": {
			args:   baseArgs[:len(baseArgs)-1],
			expErr: errors.New("no container label or UUID supplied"),
		},
		"connect fails": {
			args:   baseArgs,
			expErr: errors.New("whoops"),
			setup: func(t *testing.T) {
				prevErr := contOpenErr
				t.Cleanup(func() {
					contOpenErr = prevErr
				})
				contOpenErr = errors.New("whoops")
			},
		},
		"success": {
			args:    baseArgs,
			expArgs: containerListAttrsCmd{},
		},
		"success (verbose, short)": {
			args: test.JoinArgs(baseArgs, "-V"),
			expArgs: containerListAttrsCmd{
				Verbose: true,
			},
		},
		"success (verbose, long)": {
			args: test.JoinArgs(baseArgs, "--verbose"),
			expArgs: containerListAttrsCmd{
				Verbose: true,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			t.Cleanup(api.ResetTestStubs)
			if tc.setup != nil {
				tc.setup(t)
			}
			runCmdTest(t, tc.args, tc.expArgs, tc.expErr, "Container.ListAttributes")
		})
	}
}
