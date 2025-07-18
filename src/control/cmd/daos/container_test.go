//
// (C) Copyright 2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
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
