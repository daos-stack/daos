//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"encoding/json"
	"fmt"
	"testing"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/google/go-cmp/cmp"
	"github.com/jessevdk/go-flags"
	"github.com/pkg/errors"
)

type daosServerTestErr string

func (dste daosServerTestErr) Error() string {
	return string(dste)
}

const (
	errMissingFlag = daosServerTestErr("required flag")
)

type cmdTest struct {
	name    string
	cmdArgs []string
	expCmd  string
	expErr  error
}

// printCommand generates a stable string representation of the
// supplied command object. It only includes exported fields in
// the output.
func printCommand(t *testing.T, input interface{}) string {
	buf, err := json.Marshal(input)
	if err != nil {
		t.Fatalf("unable to print %+v: %s", input, err)
	}
	return fmt.Sprintf("%T-%s", input, string(buf))
}

func runCmd(t *testing.T, log *logging.LeveledLogger, cmdLineArgs []string, tc *string) error {
	t.Helper()

	var opts mainOpts
	p := flags.NewParser(&opts, flags.HelpFlag|flags.PassDoubleDash)
	p.SubcommandsOptional = false
	p.CommandHandler = func(cmd flags.Commander, cmdArgs []string) error {
		if len(cmdArgs) > 0 {
			// don't support positional arguments, extra cmdArgs are unexpected
			return errors.Errorf("unexpected commandline arguments: %v", cmdArgs)
		}
		cmdRepr := printCommand(t, cmd)
		log.Debug(cmdRepr)
		*tc = cmdRepr
		return nil
	}

	_, err := p.ParseArgs(cmdLineArgs)
	return err
}

func runCmdTests(t *testing.T, cmdTests []cmdTest) {
	t.Helper()

	for _, st := range cmdTests {
		t.Run(st.name, func(t *testing.T) {
			t.Helper()
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			var gotCmd string
			err := runCmd(t, log, st.cmdArgs, &gotCmd)
			if err != st.expErr {
				if st.expErr == nil {
					t.Fatalf("expected nil error, got %+v", err)
				}

				if err == nil {
					t.Fatalf("expected err '%v', got nil", st.expErr)
				}

				testExpectedError(t, st.expErr, err)
				return
			}
			if diff := cmp.Diff(st.expCmd, gotCmd); diff != "" {
				t.Fatalf("unexpected function calls (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestBadCommand(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	var opts mainOpts
	err := parseOpts([]string{"foo"}, &opts, log)
	testExpectedError(t, fmt.Errorf("Unknown command `foo'"), err)
}

func TestNoCommand(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	var opts mainOpts
	err := parseOpts([]string{}, &opts, log)
	testExpectedError(t, fmt.Errorf("Please specify one command"), err)
}

func TestDaosServer_NVMe_Commands(t *testing.T) {
	multiPCIAddrs := "0000:80:00.0 0000:81:00.0"

	runCmdTests(t, []cmdTest{
		{
			"Prepare drives with all opts",
			[]string{
				"nvme", "prepare-drives", "--pci-block-list", multiPCIAddrs,
				"--hugepages", "8192", "--target-user", "bob", "--disable-vfio",
				"-l", "/tmp/foo", multiPCIAddrs,
			},
			printCommand(t, (&prepareDrivesCmd{
				PCIBlockList: multiPCIAddrs,
				NrHugepages:  8192,
				TargetUser:   "bob",
				DisableVFIO:  true,
			}).WithPCIAllowList(multiPCIAddrs)),
			nil,
		},
		{
			"Prepare drives; bad opt",
			[]string{
				"nvme", "prepare-drives", "--pxi-block-list", multiPCIAddrs,
				"--target-user", "bob", "--disable-vfio", "-l", "/tmp/foo",
				multiPCIAddrs,
			},
			"",
			errors.New("unknown"),
		},
		{
			"Release drives with all opts",
			[]string{
				"nvme", "release-drives", "--pci-block-list", multiPCIAddrs,
				"--target-user", "bob", "--disable-vfio", "-l", "/tmp/foo",
				multiPCIAddrs,
			},
			printCommand(t, (&releaseDrivesCmd{
				PCIBlockList: multiPCIAddrs,
				TargetUser:   "bob",
				DisableVFIO:  true,
			}).WithPCIAllowList(multiPCIAddrs)),
			nil,
		},
		{
			"Release drives; bad opt",
			[]string{
				"nvme", "release-drives", "--pci-block-list", multiPCIAddrs,
				"--target-user", "bob", "--disble-vfio", "-l", "/tmp/foo",
				multiPCIAddrs,
			},
			"",
			errors.New("unknown"),
		},
		{
			"Create namespaces with all opts",
			[]string{
				"pmem", "create-namespaces", "-S", "2", "-f",
			},
			printCommand(t, &createNamespacesCmd{
				NrNamespacesPerSocket: 2,
				Force:                 true,
			}),
			nil,
		},
		{
			"Create namespaces; bad opt",
			[]string{
				"pmem", "remove-namespaces", "-X",
			},
			"",
			errors.New("unknown"),
		},
		{
			"Remove namespaces with all opts",
			[]string{
				"pmem", "remove-namespaces", "-f",
			},
			printCommand(t, &removeNamespacesCmd{
				Force: true,
			}),
			nil,
		},
		{
			"Remove namespaces; bad opt",
			[]string{
				"pmem", "remove-namespaces", "-S",
			},
			"",
			errors.New("unknown"),
		},
	})
}
