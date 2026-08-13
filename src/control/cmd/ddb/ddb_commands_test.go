//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

//go:build test_stubs

package main

import (
	"fmt"
	"math"
	"os"
	"path"
	"path/filepath"
	"strings"
	"testing"

	"github.com/daos-stack/daos/src/control/common/test"
)

func TestDdb_HelpCmds(t *testing.T) {
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
		"help for 'csum_dump' command": {
			cmdStr:     "csum_dump",
			helpSubStr: "Usage:\n  csum_dump [flags] path [dst]\n",
		},
		// TODO(follow-up PR): Add help tests for the remaining commands.
	} {
		t.Run(name, func(t *testing.T) {
			ctx := newTestContext(t)

			// Create a temporary config file with the help command
			tmpCfgDir := t.TempDir()
			tmpCfgFile := path.Join(tmpCfgDir, "ddb-cmd_file.txt")
			if err := os.WriteFile(tmpCfgFile, []byte(fmt.Sprintf("%s --help", tc.cmdStr)), 0644); err != nil {
				t.Fatalf("failed to write temp config file: %v", err)
			}

			// Run the help command with a command file
			args := test.JoinArgs(nil, "--cmd_file="+tmpCfgFile)
			stdoutCmdFile, err := captureStdout(func() error {
				return runDdb(ctx, args)
			})
			if err != nil {
				t.Fatalf("unexpected error when running '%s --help' via command file: want nil, got %v", tc.cmdStr, err)
			}
			test.AssertStringContains(t, stdoutCmdFile, tc.helpSubStr)

			// Run the help command with a command line
			args = test.JoinArgs(nil, tc.cmdStr, "--help")
			stdoutCmdLine, err := captureStdout(func() error {
				return runDdb(ctx, args)
			})
			if err != nil {
				t.Fatalf("unexpected error when running '%s --help' via command line: want nil, got %v", tc.cmdStr, err)
			}
			test.AssertStringContains(t, stdoutCmdLine, tc.helpSubStr)

			// Compare command line and command file outputs
			test.AssertEqual(t, stdoutCmdFile, stdoutCmdLine,
				fmt.Sprintf("unexpected help output mismatch between command file and command line for '%s'", tc.cmdStr))
		})
	}
}

func TestDdb_Cmds(t *testing.T) {
	// Helper factories for command stub functions — declared here to avoid
	// anonymous functions nested inside the test table.

	lsFnChecking := func(t *testing.T, wantPath string, wantRecursive, wantDetails bool) func(string, bool, bool) error {
		return func(path string, recursive, details bool) error {
			fmt.Println("ls called")
			test.CmpAny(t, "path", wantPath, path)
			test.CmpAny(t, "recursive", wantRecursive, recursive)
			test.CmpAny(t, "details", wantDetails, details)
			return nil
		}
	}

	openFnChecking := func(t *testing.T, wantPath, wantDbPath string, wantWriteMode bool) func(string, string, bool) error {
		return func(path, dbPath string, writeMode bool) error {
			fmt.Println("open called")
			test.CmpAny(t, "path", wantPath, path)
			test.CmpAny(t, "db_path", wantDbPath, dbPath)
			test.CmpAny(t, "write_mode", wantWriteMode, writeMode)
			return nil
		}
	}

	string2FlagsCapturing := func(captured *string) func(string) (uint64, uint64, error) {
		return func(s string) (uint64, uint64, error) {
			*captured = s
			return 0, 0, nil
		}
	}

	featureFnChecking := func(t *testing.T, wantPath, wantDbPath string,
		capturedEnable *string, capturedDisable *string, wantFlagValue string,
		wantShow bool) func(string, string, string, string, bool) error {
		return func(path, dbPath, enable, disable string, show bool) error {
			fmt.Println("feature called")
			test.CmpAny(t, "path", wantPath, path)
			test.CmpAny(t, "dbPath", wantDbPath, dbPath)
			if capturedEnable != nil {
				test.CmpAny(t, "enable", wantFlagValue, *capturedEnable)
			}
			if capturedDisable != nil {
				test.CmpAny(t, "disable", wantFlagValue, *capturedDisable)
			}
			test.CmpAny(t, "show", wantShow, show)
			return nil
		}
	}

	dtxAggrFnChecking := func(t *testing.T, wantPath string, wantCmtTime uint64, wantCmtDate string) func(string, uint64, string) error {
		return func(path string, cmtTime uint64, cmtDate string) error {
			fmt.Println("dtx_aggr called")
			test.CmpAny(t, "path", wantPath, path)
			test.CmpAny(t, "cmtTime", wantCmtTime, cmtTime)
			test.CmpAny(t, "cmtDate", wantCmtDate, cmtDate)
			return nil
		}
	}

	csumDumpFnChecking := func(t *testing.T, wantPath, wantDst string, wantEpoch uint64) func(string, string, uint64) error {
		return func(path, dst string, epoch uint64) error {
			fmt.Println("csum_dump called")
			test.CmpAny(t, "path", wantPath, path)
			test.CmpAny(t, "dst", wantDst, dst)
			test.CmpAny(t, "epoch", wantEpoch, epoch)
			return nil
		}
	}

	for name, tc := range map[string]struct {
		args      []string
		setup     func(*testing.T)
		expStdout []string
		expErr    error
	}{
		// --- ls command ---
		"ls invalid options": {
			args:   []string{"ls", "--bar"},
			expErr: ddbTestErr("invalid flag: --bar"),
		},
		"ls default": {
			args: []string{"ls"},
			setup: func(t *testing.T) {
				ddb_run_ls_Fn = lsFnChecking(t, "", false, false)
			},
			expStdout: []string{"ls called"},
		},
		"ls path": {
			args: []string{"ls", "/[0]"},
			setup: func(t *testing.T) {
				ddb_run_ls_Fn = lsFnChecking(t, "/[0]", false, false)
			},
			expStdout: []string{"ls called"},
		},
		"ls long recursive opt": {
			args: []string{"ls", "--recursive"},
			setup: func(t *testing.T) {
				ddb_run_ls_Fn = lsFnChecking(t, "", true, false)
			},
			expStdout: []string{"ls called"},
		},
		"ls short details opt": {
			args: []string{"ls", "-d"},
			setup: func(t *testing.T) {
				ddb_run_ls_Fn = lsFnChecking(t, "", false, true)
			},
			expStdout: []string{"ls called"},
		},
		"ls details long opt": {
			args: []string{"ls", "--details"},
			setup: func(t *testing.T) {
				ddb_run_ls_Fn = lsFnChecking(t, "", false, true)
			},
			expStdout: []string{"ls called"},
		},

		// --- open command ---
		"open default": {
			args: []string{"open", "/path/to/vos-0"},
			setup: func(t *testing.T) {
				ddb_run_open_Fn = openFnChecking(t, "/path/to/vos-0", "", false)
			},
			expStdout: []string{"open called"},
		},
		"open short write mode": {
			args: []string{"open", "-w", "/path/to/vos-0"},
			setup: func(t *testing.T) {
				ddb_run_open_Fn = openFnChecking(t, "/path/to/vos-0", "", true)
			},
			expStdout: []string{"open called"},
		},
		"open long write mode": {
			args: []string{"open", "--write_mode", "/path/to/vos-0"},
			setup: func(t *testing.T) {
				ddb_run_open_Fn = openFnChecking(t, "/path/to/vos-0", "", true)
			},
			expStdout: []string{"open called"},
		},
		"open with short db path": {
			args: []string{"open", "-p", "/sysdb", "/path/to/vos-0"},
			setup: func(t *testing.T) {
				ddb_run_open_Fn = openFnChecking(t, "/path/to/vos-0", "/sysdb", false)
			},
			expStdout: []string{"open called"},
		},
		"open with long db path": {
			args: []string{"open", "--db_path", "/sysdb", "/path/to/vos-0"},
			setup: func(t *testing.T) {
				ddb_run_open_Fn = openFnChecking(t, "/path/to/vos-0", "/sysdb", false)
			},
			expStdout: []string{"open called"},
		},

		// --- feature command ---
		"feature without flags": {
			args:   []string{"feature"},
			expErr: ddbTestErr(featureOnlyOneOptErr),
		},
		"feature with enable and disable flags": {
			args:   []string{"feature", "--enable=a", "--disable=b"},
			expErr: ddbTestErr(featureOnlyOneOptErr),
		},
		"feature with enable and show flags": {
			args:   []string{"feature", "--enable=a", "--show"},
			expErr: ddbTestErr(featureOnlyOneOptErr),
		},
		"feature with disable and show flags": {
			args:   []string{"feature", "--disable=a", "--show"},
			expErr: ddbTestErr(featureOnlyOneOptErr),
		},
		"feature with db_path but no path": {
			args:   []string{"feature", "--db_path=/sysdb", "--show"},
			expErr: ddbTestErr(vosPathMissErr),
		},
		"feature with long show flag": {
			args: []string{"feature", "--show"},
			setup: func(t *testing.T) {
				ddb_run_feature_Fn = featureFnChecking(t, "", "", nil, nil, "", true)
			},
			expStdout: []string{"feature called"},
		},
		"feature with short show flag": {
			args: []string{"feature", "-s"},
			setup: func(t *testing.T) {
				ddb_run_feature_Fn = featureFnChecking(t, "", "", nil, nil, "", true)
			},
			expStdout: []string{"feature called"},
		},
		"feature with long enable flag": {
			args: []string{"feature", "--enable=myflag"},
			setup: func(t *testing.T) {
				var capturedFlag string
				ddb_feature_string2flags_Fn = string2FlagsCapturing(&capturedFlag)
				ddb_run_feature_Fn = featureFnChecking(t, "", "", &capturedFlag, nil, "myflag", false)
			},
			expStdout: []string{"feature called"},
		},
		"feature with short enable flag": {
			args: []string{"feature", "-e", "myflag"},
			setup: func(t *testing.T) {
				var capturedFlag string
				ddb_feature_string2flags_Fn = string2FlagsCapturing(&capturedFlag)
				ddb_run_feature_Fn = featureFnChecking(t, "", "", &capturedFlag, nil, "myflag", false)
			},
			expStdout: []string{"feature called"},
		},
		"feature with long disable flag": {
			args: []string{"feature", "--disable=myflag"},
			setup: func(t *testing.T) {
				var capturedFlag string
				ddb_feature_string2flags_Fn = string2FlagsCapturing(&capturedFlag)
				ddb_run_feature_Fn = featureFnChecking(t, "", "", nil, &capturedFlag, "myflag", false)
			},
			expStdout: []string{"feature called"},
		},
		"feature with short disable flag": {
			args: []string{"feature", "-d", "myflag"},
			setup: func(t *testing.T) {
				var capturedFlag string
				ddb_feature_string2flags_Fn = string2FlagsCapturing(&capturedFlag)
				ddb_run_feature_Fn = featureFnChecking(t, "", "", nil, &capturedFlag, "myflag", false)
			},
			expStdout: []string{"feature called"},
		},
		"feature with cmd-level db_path": {
			args: []string{"feature", "--db_path=/sysdb", "--show", "/path/to/vos-0"},
			setup: func(t *testing.T) {
				ddb_run_feature_Fn = featureFnChecking(t, "/path/to/vos-0", "/sysdb", nil, nil, "", true)
			},
			expStdout: []string{"feature called"},
		},

		// --- dtx_aggr command ---
		// The Run handler in ddb_commands.go enforces that exactly one of --cmt_time or
		// --cmt_date is provided. These tests exercise that Go-layer validation.
		"dtx_aggr both cmt_time and cmt_date": {
			args:   []string{"dtx_aggr", "--cmt_time=0", "--cmt_date=2024-01-01"},
			expErr: ddbTestErr(dtxAggrMutuallyExclusiveErr),
		},
		"dtx_aggr neither cmt_time nor cmt_date": {
			args:   []string{"dtx_aggr"},
			expErr: ddbTestErr(dtxAggrRequiredOptErr),
		},
		"dtx_aggr cmt_time": {
			args: []string{"dtx_aggr", "--cmt_time=1000"},
			setup: func(t *testing.T) {
				ddb_run_dtx_aggr_Fn = dtxAggrFnChecking(t, "", 1000, "")
			},
			expStdout: []string{"dtx_aggr called"},
		},
		"dtx_aggr cmt_date": {
			args: []string{"dtx_aggr", "--cmt_date=2024-01-01"},
			setup: func(t *testing.T) {
				ddb_run_dtx_aggr_Fn = dtxAggrFnChecking(t, "", 0, "2024-01-01")
			},
			expStdout: []string{"dtx_aggr called"},
		},
		"dtx_aggr with path": {
			args: []string{"dtx_aggr", "--cmt_time=0", "[0]"},
			setup: func(t *testing.T) {
				ddb_run_dtx_aggr_Fn = dtxAggrFnChecking(t, "[0]", 0, "")
			},
			expStdout: []string{"dtx_aggr called"},
		},

		// --- close command ---
		"close": {
			args: []string{"close"},
			setup: func(t *testing.T) {
				ddb_run_close_Fn = func() error {
					fmt.Println("close called")
					return nil
				}
			},
			expStdout: []string{"close called"},
		},

		// --- version command ---
		"version": {
			args: []string{"version"},
			setup: func(t *testing.T) {
				ddb_run_version_Fn = func() error {
					fmt.Println("version called")
					return nil
				}
			},
			expStdout: []string{"version called"},
		},

		// --- rm_pool command ---
		"rm_pool with db_path": {
			args: []string{"rm_pool", "--db_path", "/sysdb", "/mnt/pool/rdb-pool"},
			setup: func(t *testing.T) {
				ddb_run_rm_pool_Fn = func(path, dbPath string) error {
					fmt.Println("rm_pool called")
					test.CmpAny(t, "path", "/mnt/pool/rdb-pool", path)
					test.CmpAny(t, "dbPath", "/sysdb", dbPath)
					return nil
				}
			},
			expStdout: []string{"rm_pool called"},
		},
		"rm_pool without db_path": {
			args: []string{"rm_pool", "/mnt/pool/rdb-pool"},
			setup: func(t *testing.T) {
				ddb_run_rm_pool_Fn = func(path, dbPath string) error {
					fmt.Println("rm_pool called")
					test.CmpAny(t, "path", "/mnt/pool/rdb-pool", path)
					test.CmpAny(t, "dbPath", "", dbPath)
					return nil
				}
			},
			expStdout: []string{"rm_pool called"},
		},

		// --- prov_mem command: flag conflict ---
		// -s / --tmpfs_size: short flag -s was consumed as global VosPath before PassAfterNonOption.
		"prov_mem with tmpfs_size short flag": {
			args: []string{"prov_mem", "-s", "10", "-p", "/db", "/mnt"},
			setup: func(t *testing.T) {
				ddb_run_prov_mem_Fn = func(dbPath, tmpfsMount string, tmpfsMountSize uint) error {
					fmt.Println("prov_mem called")
					test.CmpAny(t, "dbPath", "/db", dbPath)
					test.CmpAny(t, "tmpfsMount", "/mnt", tmpfsMount)
					test.CmpAny(t, "tmpfsMountSize", uint(10), tmpfsMountSize)
					return nil
				}
			},
			expStdout: []string{"prov_mem called"},
		},
		"prov_mem with long db_path flag": {
			args: []string{"prov_mem", "--db_path", "/db", "/mnt"},
			setup: func(t *testing.T) {
				ddb_run_prov_mem_Fn = func(dbPath, tmpfsMount string, tmpfsMountSize uint) error {
					fmt.Println("prov_mem called")
					test.CmpAny(t, "dbPath", "/db", dbPath)
					test.CmpAny(t, "tmpfsMount", "/mnt", tmpfsMount)
					test.CmpAny(t, "tmpfsMountSize", uint(0), tmpfsMountSize)
					return nil
				}
			},
			expStdout: []string{"prov_mem called"},
		},

		// --- dev_list command ---
		"dev_list with short db_path flag": {
			args: []string{"dev_list", "-p", "/db"},
			setup: func(t *testing.T) {
				ddb_run_dev_list_Fn = func(dbPath string) error {
					fmt.Println("dev_list called")
					test.CmpAny(t, "dbPath", "/db", dbPath)
					return nil
				}
			},
			expStdout: []string{"dev_list called"},
		},
		"dev_list with long db_path flag": {
			args: []string{"dev_list", "--db_path", "/db"},
			setup: func(t *testing.T) {
				ddb_run_dev_list_Fn = func(dbPath string) error {
					fmt.Println("dev_list called")
					test.CmpAny(t, "dbPath", "/db", dbPath)
					return nil
				}
			},
			expStdout: []string{"dev_list called"},
		},

		// --- dev_replace command ---
		"dev_replace with short db_path flag": {
			args: []string{"dev_replace", "-p", "/db", "old-uuid", "new-uuid"},
			setup: func(t *testing.T) {
				ddb_run_dev_replace_Fn = func(dbPath, oldDev, newDev string) error {
					fmt.Println("dev_replace called")
					test.CmpAny(t, "dbPath", "/db", dbPath)
					test.CmpAny(t, "oldDev", "old-uuid", oldDev)
					test.CmpAny(t, "newDev", "new-uuid", newDev)
					return nil
				}
			},
			expStdout: []string{"dev_replace called"},
		},
		"dev_replace with long db_path flag": {
			args: []string{"dev_replace", "--db_path", "/db", "old-uuid", "new-uuid"},
			setup: func(t *testing.T) {
				ddb_run_dev_replace_Fn = func(dbPath, oldDev, newDev string) error {
					fmt.Println("dev_replace called")
					test.CmpAny(t, "dbPath", "/db", dbPath)
					test.CmpAny(t, "oldDev", "old-uuid", oldDev)
					test.CmpAny(t, "newDev", "new-uuid", newDev)
					return nil
				}
			},
			expStdout: []string{"dev_replace called"},
		},

		// --- smd_sync command ---
		"smd_sync with short db_path flag": {
			args: []string{"smd_sync", "-p", "/db"},
			setup: func(t *testing.T) {
				ddb_run_smd_sync_Fn = func(nvmeConf, dbPath string) error {
					fmt.Println("smd_sync called")
					test.CmpAny(t, "nvmeConf", "", nvmeConf)
					test.CmpAny(t, "dbPath", "/db", dbPath)
					return nil
				}
			},
			expStdout: []string{"smd_sync called"},
		},
		"smd_sync with long db_path flag": {
			args: []string{"smd_sync", "--db_path", "/db"},
			setup: func(t *testing.T) {
				ddb_run_smd_sync_Fn = func(nvmeConf, dbPath string) error {
					fmt.Println("smd_sync called")
					test.CmpAny(t, "nvmeConf", "", nvmeConf)
					test.CmpAny(t, "dbPath", "/db", dbPath)
					return nil
				}
			},
			expStdout: []string{"smd_sync called"},
		},
		"smd_sync with nvme_conf and db_path flag": {
			args: []string{"smd_sync", "--db_path", "/db", "/nvme.conf"},
			setup: func(t *testing.T) {
				ddb_run_smd_sync_Fn = func(nvmeConf, dbPath string) error {
					fmt.Println("smd_sync called")
					test.CmpAny(t, "nvmeConf", "/nvme.conf", nvmeConf)
					test.CmpAny(t, "dbPath", "/db", dbPath)
					return nil
				}
			},
			expStdout: []string{"smd_sync called"},
		},

		// --- csum_dump command ---
		"csum_dump missing path": {
			args:   []string{"csum_dump"},
			expErr: ddbTestErr("missing argument 'path'"),
		},
		"csum_dump invalid options": {
			args:   []string{"csum_dump", "--bar"},
			expErr: ddbTestErr("invalid flag: --bar"),
		},
		"csum_dump default": {
			args: []string{"csum_dump", "/[0]"},
			setup: func(t *testing.T) {
				ddb_run_csum_dump_Fn = csumDumpFnChecking(t, "/[0]", "", math.MaxUint64)
			},
			expStdout: []string{"csum_dump called"},
		},
		"csum_dump epoch short": {
			args: []string{"csum_dump", "-e", "999", "/[0]"},
			setup: func(t *testing.T) {
				ddb_run_csum_dump_Fn = csumDumpFnChecking(t, "/[0]", "", 999)
			},
			expStdout: []string{"csum_dump called"},
		},
		"csum_dump epoch long": {
			args: []string{"csum_dump", "--epoch=666", "/[0]"},
			setup: func(t *testing.T) {
				ddb_run_csum_dump_Fn = csumDumpFnChecking(t, "/[0]", "", 666)
			},
			expStdout: []string{"csum_dump called"},
		},
		"csum_dump destination": {
			args: []string{"csum_dump", "/[0]", "/tmp/csum_dump.out"},
			setup: func(t *testing.T) {
				ddb_run_csum_dump_Fn = csumDumpFnChecking(t, "/[0]", "/tmp/csum_dump.out", math.MaxUint64)
			},
			expStdout: []string{"csum_dump called"},
		},
		"csum_dump destination with epoch": {
			args: []string{"csum_dump", "-e", "500", "/[0]", "/tmp/csum_dump.out"},
			setup: func(t *testing.T) {
				ddb_run_csum_dump_Fn = csumDumpFnChecking(t, "/[0]", "/tmp/csum_dump.out", 500)
			},
			expStdout: []string{"csum_dump called"},
		},

		// TODO(follow-up PR): Add TestCmds cases for the remaining commands.
		// Each new test case follows the same pattern as the cases above: set the
		// corresponding ddb_run_<cmd>_Fn hook in setup() to verify argument passing,
		// then add the case to this table.
		// Commands still to be covered: superblock_dump, value_dump, rm,
		// value_load, ilog_dump, ilog_commit, ilog_clear, dtx_dump, dtx_cmt_clear,
		// smd_sync, vea_dump, vea_update, dtx_act_commit, dtx_act_abort,
		// dtx_act_discard_invalid, dev_list, dev_replace, dtx_stat,
		// prov_mem (default, no flag).
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
					tc.setup(t)
				}
				stdout, err := captureStdout(func() error {
					return runDdb(ctx, tc.args)
				})
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
					tc.setup(t)
				}
				stdout, err := captureStdout(func() error {
					return runDdb(ctx, []string{"--cmd_file=" + cmdFile})
				})
				checkCmd(t, stdout, err)
			})
		})
	}
}

func TestDdb_ManPage(t *testing.T) {
	// Expected sections and commands present in every man page rendering.
	expSections := []string{
		manArgsHeader,
		manCmdsHeader,
		manPathSection[:20],
		manMdOnSsdSection[:20],
		manLoggingSection[:20],
		".B ls\n",
		".B open\n",
	}

	// manpage to stdout: must contain all section headers and known commands.
	ctx := newTestContext(t)
	stdout, err := captureStdout(func() error {
		return runDdb(ctx, []string{"manpage"})
	})
	test.CmpErr(t, nil, err)
	test.AssertStringContains(t, stdout, expSections...)

	// --output flag: man page is written to a file, stdout is empty.
	tmpDir := t.TempDir()
	outFile := filepath.Join(tmpDir, "ddb.groff")

	ctx = newTestContext(t)
	stdout, err = captureStdout(func() error {
		return runDdb(ctx, []string{"manpage", "--output=" + outFile})
	})
	if err != nil {
		t.Fatalf("unexpected error when running 'manpage --output': want nil, got %v", err)
	}
	test.AssertTrue(t, stdout == "",
		fmt.Sprintf("expected empty stdout when --output is set: got\n%s", stdout))

	content, err := os.ReadFile(outFile)
	if err != nil {
		t.Fatalf("failed to read output file: %v", err)
	}
	test.AssertStringContains(t, string(content), expSections...)
}
