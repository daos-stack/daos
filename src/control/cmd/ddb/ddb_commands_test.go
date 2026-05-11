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
		// TODO(follow-up PR): Add help tests for the remaining commands.
		// Use runHelpCmd(t, "<cmd>", "Usage:\n  <cmd>") following the same pattern.
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
		// skipCmdLine skips the command-line sub-test with a message. Use when
		// a flag is shared between the CLI layer and the grumble command: go-flags
		// consumes it before grumble can see it, making a clean command-line test
		// impossible for that particular flag.
		skipCmdLine string
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

		// --- open command ---
		// Note: the -w/--write_mode and -p/--db_path flags of the grumble 'open'
		// command share names with CLI-level flags that are consumed by go-flags
		// before reaching grumble in command-line mode. The command-line test for
		// those flags would silently test wrong values. They are correctly exercised
		// in command-file mode; see TestRun for CLI-level flag coverage.
		"open default": {
			args: []string{"open", "/path/to/vos-0"},
			setup: func() {
				ddb_run_open_Fn = func(path, dbPath string, writeMode bool) error {
					fmt.Println("open called")
					if err := isArgEqual("/path/to/vos-0", path, "path"); err != nil {
						return err
					}
					if err := isArgEqual("", dbPath, "db_path"); err != nil {
						return err
					}
					if err := isArgEqual(false, writeMode, "write_mode"); err != nil {
						return err
					}
					return nil
				}
			},
			expStdout: []string{"open called"},
		},
		"open write mode": {
			args:        []string{"open", "-w", "/path/to/vos-0"},
			skipCmdLine: "-w is consumed by the CLI write_mode flag before reaching grumble",
			setup: func() {
				ddb_run_open_Fn = func(path, dbPath string, writeMode bool) error {
					fmt.Println("open called")
					if err := isArgEqual(true, writeMode, "write_mode"); err != nil {
						return err
					}
					return nil
				}
			},
			expStdout: []string{"open called"},
		},
		"open with db path": {
			args:        []string{"open", "-p", "/sysdb", "/path/to/vos-0"},
			skipCmdLine: "-p is consumed by the CLI db_path flag before reaching grumble",
			setup: func() {
				ddb_run_open_Fn = func(path, dbPath string, writeMode bool) error {
					fmt.Println("open called")
					if err := isArgEqual("/path/to/vos-0", path, "path"); err != nil {
						return err
					}
					if err := isArgEqual("/sysdb", dbPath, "db_path"); err != nil {
						return err
					}
					return nil
				}
			},
			expStdout: []string{"open called"},
		},

		// --- feature command ---
		// feature --show: verifies the show flag is forwarded to the C layer.
		"feature show": {
			args: []string{"feature", "--show"},
			setup: func() {
				ddb_run_feature_Fn = func(path, dbPath, enable, disable string, show bool) error {
					fmt.Println("feature called")
					if err := isArgEqual(true, show, "show"); err != nil {
						return err
					}
					return nil
				}
			},
			expStdout: []string{"feature called"},
		},
		// feature --enable: verifies that the enable string reaches ddb_feature_string2flags.
		"feature enable": {
			args: []string{"feature", "--enable=myflag"},
			setup: func() {
				var capturedFlag string
				ddb_feature_string2flags_Fn = func(s string) (uint64, uint64, error) {
					capturedFlag = s
					return 0, 0, nil
				}
				ddb_run_feature_Fn = func(path, dbPath, enable, disable string, show bool) error {
					fmt.Println("feature called")
					if err := isArgEqual("myflag", capturedFlag, "enable flag string"); err != nil {
						return err
					}
					return nil
				}
			},
			expStdout: []string{"feature called"},
		},
		// feature --disable: verifies that the disable string reaches ddb_feature_string2flags.
		"feature disable": {
			args: []string{"feature", "--disable=otherflag"},
			setup: func() {
				var capturedFlag string
				ddb_feature_string2flags_Fn = func(s string) (uint64, uint64, error) {
					capturedFlag = s
					return 0, 0, nil
				}
				ddb_run_feature_Fn = func(path, dbPath, enable, disable string, show bool) error {
					fmt.Println("feature called")
					if err := isArgEqual("otherflag", capturedFlag, "disable flag string"); err != nil {
						return err
					}
					return nil
				}
			},
			expStdout: []string{"feature called"},
		},

		// --- dtx_aggr command ---
		// The Run handler in ddb_commands.go enforces that exactly one of --cmt_time or
		// --cmt_date is provided. These tests exercise that Go-layer validation.
		"dtx_aggr both cmt_time and cmt_date": {
			args:   []string{"dtx_aggr", "--cmt_time=0", "--cmt_date=2024-01-01"},
			expErr: ddbTestErr("mutually exclusive"),
		},
		"dtx_aggr neither cmt_time nor cmt_date": {
			args:   []string{"dtx_aggr"},
			expErr: ddbTestErr("has to be defined"),
		},
		"dtx_aggr cmt_time": {
			args: []string{"dtx_aggr", "--cmt_time=1000"},
			setup: func() {
				ddb_run_dtx_aggr_Fn = func(path string, cmtTime uint64, cmtDate string) error {
					fmt.Println("dtx_aggr called")
					if err := isArgEqual(uint64(1000), cmtTime, "cmtTime"); err != nil {
						return err
					}
					if err := isArgEqual("", cmtDate, "cmtDate"); err != nil {
						return err
					}
					return nil
				}
			},
			expStdout: []string{"dtx_aggr called"},
		},
		"dtx_aggr cmt_date": {
			args: []string{"dtx_aggr", "--cmt_date=2024-01-01"},
			setup: func() {
				ddb_run_dtx_aggr_Fn = func(path string, cmtTime uint64, cmtDate string) error {
					fmt.Println("dtx_aggr called")
					if err := isArgEqual("2024-01-01", cmtDate, "cmtDate"); err != nil {
						return err
					}
					return nil
				}
			},
			expStdout: []string{"dtx_aggr called"},
		},
		"dtx_aggr with path": {
			args: []string{"dtx_aggr", "--cmt_time=0", "[0]"},
			setup: func() {
				ddb_run_dtx_aggr_Fn = func(path string, cmtTime uint64, cmtDate string) error {
					fmt.Println("dtx_aggr called")
					if err := isArgEqual("[0]", path, "path"); err != nil {
						return err
					}
					if err := isArgEqual(uint64(0), cmtTime, "cmtTime"); err != nil {
						return err
					}
					return nil
				}
			},
			expStdout: []string{"dtx_aggr called"},
		},

		// TODO(follow-up PR): Add TestCmds cases for the remaining commands.
		// Each new test case follows the same pattern as the cases above: set the
		// corresponding ddb_run_<cmd>_Fn hook in setup() to verify argument passing,
		// then add the case to this table.
		// Commands still to be covered: close, superblock_dump, value_dump, rm,
		// value_load, ilog_dump, ilog_commit, ilog_clear, dtx_dump, dtx_cmt_clear,
		// smd_sync, vea_dump, vea_update, dtx_act_commit, dtx_act_abort, rm_pool,
		// dtx_act_discard_invalid, dev_list, dev_replace, dtx_stat, prov_mem.
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
				if tc.skipCmdLine != "" {
					t.Skipf("skipping command-line mode: %s", tc.skipCmdLine)
				}
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
