//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

//go:build test_stubs

package main

import (
	"fmt"
	"os"
	"path"
	"path/filepath"
	"strings"
	"testing"

	"github.com/daos-stack/daos/src/control/common/test"
)

func runHelpCmd(t *testing.T, cmdStr string, helpSubStr string) {
	t.Helper()

	ctx := newTestContext(t)

	// Create a temporary config file with the help command
	tmpCfgDir := t.TempDir()
	tmpCfgFile := path.Join(tmpCfgDir, "ddb-cmd_file.txt")
	if err := os.WriteFile(tmpCfgFile, []byte(fmt.Sprintf("%s --help", cmdStr)), 0644); err != nil {
		t.Fatalf("failed to write temp config file: %v", err)
	}

	// Run the help command with a command file
	args := test.JoinArgs(nil, "--cmd_file="+tmpCfgFile)
	stdoutCmdFile, err := runMainFlow(ctx, args)
	if err != nil {
		t.Fatalf("unexpected error when running '%s --help' via command file: want nil, got %v", cmdStr, err)
	}
	test.AssertTrue(t, strings.Contains(stdoutCmdFile, helpSubStr),
		fmt.Sprintf("expected stdout to contain %q: got\n%s", helpSubStr, stdoutCmdFile))

	// Run the help command with a command line
	args = test.JoinArgs(nil, cmdStr, "--help")
	stdoutCmdLine, err := runMainFlow(ctx, args)
	if err != nil {
		t.Fatalf("unexpected error when running '%s --help' via command line: want nil, got %v", cmdStr, err)
	}
	test.AssertTrue(t, strings.Contains(stdoutCmdLine, helpSubStr),
		fmt.Sprintf("expected stdout to contain %q: got\n%s", helpSubStr, stdoutCmdLine))

	// Compare command line and command file outputs
	test.AssertEqual(t, stdoutCmdFile, stdoutCmdLine,
		fmt.Sprintf("unexpected help output mismatch between command file and command line for '%s'", cmdStr))
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
		setup     func()
		expStdout []string
		expErr    error
	}{
		"ls invalid options": {
			args:   []string{"ls", "--bar"},
			expErr: ddbTestErr("invalid flag: --bar"),
		},
		"ls default": {
			args: []string{"ls"},
			setup: func() {
				ddb_run_ls_Fn = func(path string, recursive bool, details bool) error {
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
			setup: func() {
				ddb_run_ls_Fn = func(path string, recursive bool, details bool) error {
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
			setup: func() {
				ddb_run_ls_Fn = func(path string, recursive bool, details bool) error {
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
			setup: func() {
				ddb_run_ls_Fn = func(path string, recursive bool, details bool) error {
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
			setup: func() {
				ddb_run_ls_Fn = func(path string, recursive bool, details bool) error {
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
			checkCmd := func(t *testing.T, stdout string, err error) {
				t.Helper()
				test.CmpErr(t, tc.expErr, err)
				if tc.expErr != nil {
					return
				}
				for _, msg := range tc.expStdout {
					test.AssertTrue(t, strings.Contains(stdout, msg),
						fmt.Sprintf("expected stdout to contain %q: got\n%s", msg, stdout))
				}
			}

			t.Run("command-line", func(t *testing.T) {
				ctx := newTestContext(t)
				if tc.setup != nil {
					tc.setup()
				}
				stdout, err := runMainFlow(ctx, tc.args)
				checkCmd(t, stdout, err)
			})

			t.Run("command-file", func(t *testing.T) {
				tmpDir := t.TempDir()
				cmdFile := filepath.Join(tmpDir, "cmds.txt")
				cmdLine := strings.Join(tc.args, " ")
				if err := os.WriteFile(cmdFile, []byte(cmdLine), 0644); err != nil {
					t.Fatalf("failed to write command file: %v", err)
				}
				ctx := newTestContext(t)
				if tc.setup != nil {
					tc.setup()
				}
				stdout, err := runMainFlow(ctx, []string{"--cmd_file=" + cmdFile})
				checkCmd(t, stdout, err)
			})
		})
	}
}

func TestManPage(t *testing.T) {
	for name, tc := range map[string]struct {
		args      []string
		expStdout []string
		expErr    error
	}{
		"manpage to stdout contains sections and commands": {
			args: []string{"manpage"},
			expStdout: []string{
				manArgsHeader,
				manCmdsHeader,
				manPathSection[:20],
				manMdOnSsdSection[:20],
				manLoggingSection[:20],
				// Known commands must appear in the listing
				".B ls\n",
				".B open\n",
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			ctx := newTestContext(t)
			stdout, err := runMainFlow(ctx, tc.args)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}
			for _, msg := range tc.expStdout {
				test.AssertTrue(t, strings.Contains(stdout, msg),
					fmt.Sprintf("expected stdout to contain %q: got\n%s", msg, stdout))
			}
		})
	}

	// Test --output flag: man page is written to a file, stdout is empty.
	t.Run("manpage to file", func(t *testing.T) {
		tmpDir := t.TempDir()
		outFile := filepath.Join(tmpDir, "ddb.groff")

		ctx := newTestContext(t)
		stdout, err := runMainFlow(ctx, []string{"manpage", "--output=" + outFile})
		if err != nil {
			t.Fatalf("unexpected error when running 'manpage --output': want nil, got %v", err)
		}
		test.AssertTrue(t, stdout == "",
			fmt.Sprintf("expected empty stdout when --output is set: got\n%s", stdout))

		content, err := os.ReadFile(outFile)
		if err != nil {
			t.Fatalf("failed to read output file: %v", err)
		}
		for _, section := range []string{manArgsHeader, manCmdsHeader, manLoggingSection[:20]} {
			test.AssertTrue(t, strings.Contains(string(content), section),
				fmt.Sprintf("expected file to contain %q: got\n%s", section, string(content)))
		}
	})
}
