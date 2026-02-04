package main

import (
	"fmt"
	"os"
	"path"
	"strings"
	"testing"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
)

func runHelpCmd(t *testing.T, cmdStr string, helpSubStr string) {
	t.Helper()

	var ctx DdbContextStub
	var opts cliOptions

	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	// Create a temporary config file with the help command
	tmpCfgDir := t.TempDir()
	t.Cleanup(func() {
		test.RemoveContents(t, tmpCfgDir)
	})
	tmpCfgFile := path.Join(tmpCfgDir, "ddb-cmd_file.txt")
	if err := os.WriteFile(tmpCfgFile, []byte(fmt.Sprintf("%s --help", cmdStr)), 0644); err != nil {
		t.Fatalf("Failed to write temp config file: %v", err)
	}

	// Run the help command with a command file
	args := test.JoinArgs(nil, "--cmd_file="+tmpCfgFile)
	stdoutCmdFile, err := runCmdToStdout(log, &ctx, &opts, args)
	if err != nil {
		t.Fatalf("Error parsing %s help option: %v\n", cmdStr, err)
	}
	test.AssertTrue(t, strings.Contains(stdoutCmdFile, helpSubStr),
		"Expected help message not found in output: "+stdoutCmdFile)

	// Run the help command with a command line
	args = test.JoinArgs(nil, cmdStr, "--help")
	stdoutCmdLine, err := runCmdToStdout(log, &ctx, &opts, args)
	if err != nil {
		t.Fatalf("Error parsing %s help option: %v\n", cmdStr, err)
	}
	test.AssertTrue(t, strings.Contains(stdoutCmdLine, helpSubStr),
		"Expected help message not found in output: "+stdoutCmdLine)

	// Compare command line and command file outputs
	test.AssertEqual(t, stdoutCmdFile, stdoutCmdLine,
		fmt.Sprintf("Help output mismatch between command file and command line for '%s'", cmdStr))
}

func TestHelpCmds(t *testing.T) {
	for name, tc := range map[string]struct {
		cmdStr     string
		helpSubStr string
	}{
		"help for 'ls' command": {
			cmdStr:     "ls",
			helpSubStr: "Usage:\n  ls [flags] [path]\n",
		},
		"help for 'open' command": {
			cmdStr:     "open",
			helpSubStr: "Usage:\n  open [flags] path\n",
		},
	} {
		t.Run(name, func(t *testing.T) {
			runHelpCmd(t, tc.cmdStr, tc.helpSubStr)
		})
	}
}

func TestCmds(t *testing.T) {
	for name, tc := range map[string]struct {
		args      []string
		expCalls  func(ctx *DdbContextStub)
		expStdout []string
		expErr    error
	}{
		"ls invalid options": {
			args:   []string{"ls", "--bar"},
			expErr: ddbTestErr("invalid flag: --bar"),
		},
		"ls default": {
			args: []string{"ls"},
			expCalls: func(ctx *DdbContextStub) {
				ctx.ls = func(path string, recursive bool, details bool) error {
					fmt.Println("ls called")
					if err := isArgEqual("", path, "path"); err != nil {
						return err
					}
					if err := isArgEqual(false, recursive, "recursive"); err != nil {
						return err
					}
					if err := isArgEqual(false, details, "details"); err != nil {
						return err
					}
					return nil
				}
			},
			expStdout: []string{"ls called"},
		},
		"ls path": {
			args: []string{"ls", "/[0]"},
			expCalls: func(ctx *DdbContextStub) {
				ctx.ls = func(path string, recursive bool, details bool) error {
					fmt.Println("ls called")
					if err := isArgEqual("/[0]", path, "path"); err != nil {
						return err
					}
					if err := isArgEqual(false, recursive, "recursive"); err != nil {
						return err
					}
					if err := isArgEqual(false, details, "details"); err != nil {
						return err
					}
					return nil
				}
			},
			expStdout: []string{"ls called"},
		},
		"ls long recursive opt": {
			args: []string{"ls", "--recursive"},
			expCalls: func(ctx *DdbContextStub) {
				ctx.ls = func(path string, recursive bool, details bool) error {
					fmt.Println("ls called")
					if err := isArgEqual("", path, "path"); err != nil {
						return err
					}
					if err := isArgEqual(true, recursive, "recursive"); err != nil {
						return err
					}
					if err := isArgEqual(false, details, "details"); err != nil {
						return err
					}
					return nil
				}
			},
			expStdout: []string{"ls called"},
		},
		"ls short details opt": {
			args: []string{"ls", "-d"},
			expCalls: func(ctx *DdbContextStub) {
				ctx.ls = func(path string, recursive bool, details bool) error {
					fmt.Println("ls called")
					if err := isArgEqual("", path, "path"); err != nil {
						return err
					}
					if err := isArgEqual(false, recursive, "recursive"); err != nil {
						return err
					}
					if err := isArgEqual(true, details, "details"); err != nil {
						return err
					}
					return nil
				}
			},
			expStdout: []string{"ls called"},
		},
		"ls details long opt": {
			args: []string{"ls", "--details"},
			expCalls: func(ctx *DdbContextStub) {
				ctx.ls = func(path string, recursive bool, details bool) error {
					fmt.Println("ls called")
					if err := isArgEqual("", path, "path"); err != nil {
						return err
					}
					if err := isArgEqual(false, recursive, "recursive"); err != nil {
						return err
					}
					if err := isArgEqual(true, details, "details"); err != nil {
						return err
					}
					return nil
				}
			},
			expStdout: []string{"ls called"},
		},
	} {
		t.Run(name, func(t *testing.T) {
			var opts cliOptions
			var ctx DdbContextStub

			log, buf := logging.NewTestLogger(t.Name())
			t.Cleanup(func() {
				test.ShowBufferOnFailure(t, buf)
			})

			if tc.expCalls != nil {
				tc.expCalls(&ctx)
			}
			stdout, err := runCmdToStdout(log, &ctx, &opts, tc.args)
			if err != nil {
				test.CmpErr(t, tc.expErr, err)
				if tc.expErr != nil {
					return
				}
			}

			for _, msg := range tc.expStdout {
				test.AssertTrue(t, strings.Contains(stdout, msg),
					fmt.Sprintf("Expected message '%s' not found in standard output:%v\n", msg, stdout))
			}
		})
	}
}
