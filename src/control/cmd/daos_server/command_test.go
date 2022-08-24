//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"encoding/json"
	"fmt"
	"strings"
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
	name   string
	cmd    string
	expCmd string
	expErr error
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

func runCmd(t *testing.T, log *logging.LeveledLogger, cmdLine string, tc *string) error {
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

	_, err := p.ParseArgs(strings.Split(cmdLine, " "))
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
			err := runCmd(t, log, st.cmd, &gotCmd)
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
	multPCIAddrsSpaceSep := "0000:80:00.0 0000:81:00.0"
	multPCIAddrsCommaSep := "0000:80:00.0,0000:81:00.0"

	runCmdTests(t, []cmdTest{
		{
			"Prepare drives with all opts; space separated PCI addresses",
			fmt.Sprintf("nvme prepare --pci-block-list %s --hugepages 8192 --target-user bob --disable-vfio "+
				"-l /tmp/foo "+multPCIAddrsSpaceSep, multPCIAddrsSpaceSep),
			"",
			errors.New("unexpected commandline arguments"),
		},
		{
			"Prepare drives with all opts; comma separated PCI addresses",
			fmt.Sprintf("nvme prepare --pci-block-list %s --hugepages 8192 --target-user bob --disable-vfio "+
				"-l /tmp/foo "+multPCIAddrsCommaSep, multPCIAddrsCommaSep),
			printCommand(t, (&prepareNVMeCmd{
				PCIBlockList: multPCIAddrsCommaSep,
				NrHugepages:  8192,
				TargetUser:   "bob",
				DisableVFIO:  true,
			}).WithPCIAllowList(multPCIAddrsCommaSep)),
			nil,
		},
		{
			"Prepare drives; bad opt",
			fmt.Sprintf("nvme prepare --pcx-block-list %s --hugepages 8192 --target-user bob --disable-vfio "+
				"-l /tmp/foo "+multPCIAddrsCommaSep, multPCIAddrsCommaSep),
			"",
			errors.New("unknown"),
		},
		{
			"Reset drives with all opts",
			fmt.Sprintf("nvme reset --pci-block-list %s --target-user bob --disable-vfio -l /tmp/foo "+
				multPCIAddrsCommaSep, multPCIAddrsCommaSep),
			printCommand(t, (&resetNVMeCmd{
				PCIBlockList: multPCIAddrsCommaSep,
				TargetUser:   "bob",
				DisableVFIO:  true,
			}).WithPCIAllowList(multPCIAddrsCommaSep)),
			nil,
		},
		{
			"Reset drives; bad opt",
			fmt.Sprintf("nvme reset --pci-block-list %s --target-user bob --disble-vfio -l /tmp/foo "+
				multPCIAddrsCommaSep, multPCIAddrsCommaSep),
			"",
			errors.New("unknown"),
		},
		{
			"Prepare namespaces with all opts",
			"scm prepare -S 2 -f",
			printCommand(t, &prepareSCMCmd{
				NrNamespacesPerSocket: 2,
				Force:                 true,
			}),
			nil,
		},
		{
			"Prepare namespaces; bad opt",
			"scm prepare -X",
			"",
			errors.New("unknown"),
		},
		{
			"Reset namespaces with all opts",
			"scm reset -f",
			printCommand(t, &resetSCMCmd{
				Force: true,
			}),
			nil,
		},
		{
			"Reset namespaces; bad opt",
			"scm reset -S",
			"",
			errors.New("unknown"),
		},
	})
}
