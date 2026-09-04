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
		"help for 'version' command": {
			cmdStr:     "version",
			helpSubStr: "Usage:\n  version [flags]\n",
		},
		"help for 'close' command": {
			cmdStr:     "close",
			helpSubStr: "Usage:\n  close [flags]\n",
		},
		"help for 'superblock_dump' command": {
			cmdStr:     "superblock_dump",
			helpSubStr: "Usage:\n  superblock_dump [flags]\n",
		},
		"help for 'value_dump' command": {
			cmdStr:     "value_dump",
			helpSubStr: "Usage:\n  value_dump [flags] path [dst]\n",
		},
		"help for 'rm' command": {
			cmdStr:     "rm",
			helpSubStr: "Usage:\n  rm [flags] path\n",
		},
		"help for 'value_load' command": {
			cmdStr:     "value_load",
			helpSubStr: "Usage:\n  value_load [flags] src dst\n",
		},
		"help for 'ilog_dump' command": {
			cmdStr:     "ilog_dump",
			helpSubStr: "Usage:\n  ilog_dump [flags] path\n",
		},
		"help for 'ilog_commit' command": {
			cmdStr:     "ilog_commit",
			helpSubStr: "Usage:\n  ilog_commit [flags] path\n",
		},
		"help for 'ilog_clear' command": {
			cmdStr:     "ilog_clear",
			helpSubStr: "Usage:\n  ilog_clear [flags] path\n",
		},
		"help for 'dtx_dump' command": {
			cmdStr:     "dtx_dump",
			helpSubStr: "Usage:\n  dtx_dump [flags] path\n",
		},
		"help for 'dtx_cmt_clear' command": {
			cmdStr:     "dtx_cmt_clear",
			helpSubStr: "Usage:\n  dtx_cmt_clear [flags] path\n",
		},
		"help for 'smd_sync' command": {
			cmdStr:     "smd_sync",
			helpSubStr: "Usage:\n  smd_sync [flags] [nvme_conf]\n",
		},
		"help for 'vea_dump' command": {
			cmdStr:     "vea_dump",
			helpSubStr: "Usage:\n  vea_dump [flags]\n",
		},
		"help for 'vea_update' command": {
			cmdStr:     "vea_update",
			helpSubStr: "Usage:\n  vea_update [flags] offset blk_cnt\n",
		},
		"help for 'dtx_act_commit' command": {
			cmdStr:     "dtx_act_commit",
			helpSubStr: "Usage:\n  dtx_act_commit [flags] path dtx_id\n",
		},
		"help for 'dtx_act_abort' command": {
			cmdStr:     "dtx_act_abort",
			helpSubStr: "Usage:\n  dtx_act_abort [flags] path dtx_id\n",
		},
		"help for 'feature' command": {
			cmdStr:     "feature",
			helpSubStr: "Usage:\n  feature [flags] [path]\n",
		},
		"help for 'rm_pool' command": {
			cmdStr:     "rm_pool",
			helpSubStr: "Usage:\n  rm_pool [flags] path\n",
		},
		"help for 'dtx_act_discard_invalid' command": {
			cmdStr:     "dtx_act_discard_invalid",
			helpSubStr: "Usage:\n  dtx_act_discard_invalid [flags] path dtx_id\n",
		},
		"help for 'dev_list' command": {
			cmdStr:     "dev_list",
			helpSubStr: "Usage:\n  dev_list [flags]\n",
		},
		"help for 'dev_replace' command": {
			cmdStr:     "dev_replace",
			helpSubStr: "Usage:\n  dev_replace [flags] old_dev new_dev\n",
		},
		"help for 'dtx_stat' command": {
			cmdStr:     "dtx_stat",
			helpSubStr: "Usage:\n  dtx_stat [flags] [path]\n",
		},
		"help for 'prov_mem' command": {
			cmdStr:     "prov_mem",
			helpSubStr: "Usage:\n  prov_mem [flags] tmpfs_mount\n",
		},
		"help for 'dtx_aggr' command": {
			cmdStr:     "dtx_aggr",
			helpSubStr: "Usage:\n  dtx_aggr [flags] [path]\n",
		},
		"help for 'csum_dump' command": {
			cmdStr:     "csum_dump",
			helpSubStr: "Usage:\n  csum_dump [flags] path [dst]\n",
		},
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

	valueDumpFnChecking := func(t *testing.T, wantPath, wantDst string) func(string, string) error {
		return func(path, dst string) error {
			fmt.Println("value_dump called")
			test.CmpAny(t, "path", wantPath, path)
			test.CmpAny(t, "dst", wantDst, dst)
			return nil
		}
	}

	smdSyncFnChecking := func(t *testing.T, wantNvmeConf, wantDbPath string) func(string, string) error {
		return func(nvmeConf, dbPath string) error {
			fmt.Println("smd_sync called")
			test.CmpAny(t, "nvmeConf", wantNvmeConf, nvmeConf)
			test.CmpAny(t, "dbPath", wantDbPath, dbPath)
			return nil
		}
	}

	dtxStatFnChecking := func(t *testing.T, wantPath string, wantDetails bool) func(string, bool) error {
		return func(path string, details bool) error {
			fmt.Println("dtx_stat called")
			test.CmpAny(t, "path", wantPath, path)
			test.CmpAny(t, "details", wantDetails, details)
			return nil
		}
	}

	dtxDumpFnChecking := func(t *testing.T, wantPath string, wantActive, wantCommitted bool) func(string, bool, bool) error {
		return func(path string, active, committed bool) error {
			fmt.Println("dtx_dump called")
			test.CmpAny(t, "path", wantPath, path)
			test.CmpAny(t, "active", wantActive, active)
			test.CmpAny(t, "committed", wantCommitted, committed)
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

		// --- superblock_dump command ---
		"superblock_dump default": {
			args: []string{"superblock_dump"},
			setup: func(t *testing.T) {
				ddb_run_superblock_dump_Fn = func() error {
					fmt.Println("superblock_dump called")
					return nil
				}
			},
			expStdout: []string{"superblock_dump called"},
		},

		// --- value_dump command ---
		"value_dump path only": {
			args: []string{"value_dump", "[0]/[0]/[0]/[0]"},
			setup: func(t *testing.T) {
				ddb_run_value_dump_Fn = valueDumpFnChecking(t, "[0]/[0]/[0]/[0]", "")
			},
			expStdout: []string{"value_dump called"},
		},
		"value_dump with dst": {
			args: []string{"value_dump", "[0]/[0]/[0]/[0]", "/tmp/value.out"},
			setup: func(t *testing.T) {
				ddb_run_value_dump_Fn = valueDumpFnChecking(t, "[0]/[0]/[0]/[0]", "/tmp/value.out")
			},
			expStdout: []string{"value_dump called"},
		},

		// --- rm command ---
		"rm default": {
			args: []string{"rm", "[0]/[0]"},
			setup: func(t *testing.T) {
				ddb_run_rm_Fn = func(path string) error {
					fmt.Println("rm called")
					test.CmpAny(t, "path", "[0]/[0]", path)
					return nil
				}
			},
			expStdout: []string{"rm called"},
		},

		// --- value_load command ---
		"value_load default": {
			args: []string{"value_load", "/tmp/value.in", "[0]/[0]/[0]/[0]"},
			setup: func(t *testing.T) {
				ddb_run_value_load_Fn = func(src, dst string) error {
					fmt.Println("value_load called")
					test.CmpAny(t, "src", "/tmp/value.in", src)
					test.CmpAny(t, "dst", "[0]/[0]/[0]/[0]", dst)
					return nil
				}
			},
			expStdout: []string{"value_load called"},
		},

		// --- ilog_dump command ---
		"ilog_dump default": {
			args: []string{"ilog_dump", "[0]/[0]/[0]"},
			setup: func(t *testing.T) {
				ddb_run_ilog_dump_Fn = func(path string) error {
					fmt.Println("ilog_dump called")
					test.CmpAny(t, "path", "[0]/[0]/[0]", path)
					return nil
				}
			},
			expStdout: []string{"ilog_dump called"},
		},

		// --- ilog_commit command ---
		"ilog_commit default": {
			args: []string{"ilog_commit", "[0]/[0]/[0]"},
			setup: func(t *testing.T) {
				ddb_run_ilog_commit_Fn = func(path string) error {
					fmt.Println("ilog_commit called")
					test.CmpAny(t, "path", "[0]/[0]/[0]", path)
					return nil
				}
			},
			expStdout: []string{"ilog_commit called"},
		},

		// --- ilog_clear command ---
		"ilog_clear default": {
			args: []string{"ilog_clear", "[0]/[0]/[0]"},
			setup: func(t *testing.T) {
				ddb_run_ilog_clear_Fn = func(path string) error {
					fmt.Println("ilog_clear called")
					test.CmpAny(t, "path", "[0]/[0]/[0]", path)
					return nil
				}
			},
			expStdout: []string{"ilog_clear called"},
		},

		// --- dtx_dump command ---
		"dtx_dump default": {
			args: []string{"dtx_dump", "[0]"},
			setup: func(t *testing.T) {
				ddb_run_dtx_dump_Fn = dtxDumpFnChecking(t, "[0]", false, false)
			},
			expStdout: []string{"dtx_dump called"},
		},
		"dtx_dump with active flag": {
			args: []string{"dtx_dump", "-a", "[0]"},
			setup: func(t *testing.T) {
				ddb_run_dtx_dump_Fn = dtxDumpFnChecking(t, "[0]", true, false)
			},
			expStdout: []string{"dtx_dump called"},
		},
		"dtx_dump with committed flag": {
			args: []string{"dtx_dump", "--committed", "[0]"},
			setup: func(t *testing.T) {
				ddb_run_dtx_dump_Fn = dtxDumpFnChecking(t, "[0]", false, true)
			},
			expStdout: []string{"dtx_dump called"},
		},

		// --- dtx_cmt_clear command ---
		"dtx_cmt_clear default": {
			args: []string{"dtx_cmt_clear", "[0]"},
			setup: func(t *testing.T) {
				ddb_run_dtx_cmt_clear_Fn = func(path string) error {
					fmt.Println("dtx_cmt_clear called")
					test.CmpAny(t, "path", "[0]", path)
					return nil
				}
			},
			expStdout: []string{"dtx_cmt_clear called"},
		},

		// --- smd_sync command ---
		"smd_sync default": {
			args: []string{"smd_sync"},
			setup: func(t *testing.T) {
				ddb_run_smd_sync_Fn = smdSyncFnChecking(t, "", "")
			},
			expStdout: []string{"smd_sync called"},
		},
		"smd_sync with args": {
			args: []string{"smd_sync", "--db_path", "/mnt/daos", "/mnt/daos/daos_nvme.conf"},
			setup: func(t *testing.T) {
				ddb_run_smd_sync_Fn = smdSyncFnChecking(t, "/mnt/daos/daos_nvme.conf", "/mnt/daos")
			},
			expStdout: []string{"smd_sync called"},
		},

		// --- vea_dump command ---
		"vea_dump default": {
			args: []string{"vea_dump"},
			setup: func(t *testing.T) {
				ddb_run_vea_dump_Fn = func() error {
					fmt.Println("vea_dump called")
					return nil
				}
			},
			expStdout: []string{"vea_dump called"},
		},

		// --- vea_update command ---
		"vea_update default": {
			args: []string{"vea_update", "1024", "8"},
			setup: func(t *testing.T) {
				ddb_run_vea_update_Fn = func(offset, blkCnt string) error {
					fmt.Println("vea_update called")
					test.CmpAny(t, "offset", "1024", offset)
					test.CmpAny(t, "blkCnt", "8", blkCnt)
					return nil
				}
			},
			expStdout: []string{"vea_update called"},
		},

		// --- dtx_act_commit command ---
		"dtx_act_commit default": {
			args: []string{"dtx_act_commit", "[0]", "1.2.3"},
			setup: func(t *testing.T) {
				ddb_run_dtx_act_commit_Fn = func(path, dtxID string) error {
					fmt.Println("dtx_act_commit called")
					test.CmpAny(t, "path", "[0]", path)
					test.CmpAny(t, "dtxID", "1.2.3", dtxID)
					return nil
				}
			},
			expStdout: []string{"dtx_act_commit called"},
		},

		// --- dtx_act_abort command ---
		"dtx_act_abort default": {
			args: []string{"dtx_act_abort", "[0]", "1.2.3"},
			setup: func(t *testing.T) {
				ddb_run_dtx_act_abort_Fn = func(path, dtxID string) error {
					fmt.Println("dtx_act_abort called")
					test.CmpAny(t, "path", "[0]", path)
					test.CmpAny(t, "dtxID", "1.2.3", dtxID)
					return nil
				}
			},
			expStdout: []string{"dtx_act_abort called"},
		},

		// --- dtx_act_discard_invalid command ---
		"dtx_act_discard_invalid default": {
			args: []string{"dtx_act_discard_invalid", "[0]", "all"},
			setup: func(t *testing.T) {
				ddb_run_dtx_act_discard_invalid_Fn = func(path, dtxID string) error {
					fmt.Println("dtx_act_discard_invalid called")
					test.CmpAny(t, "path", "[0]", path)
					test.CmpAny(t, "dtxID", "all", dtxID)
					return nil
				}
			},
			expStdout: []string{"dtx_act_discard_invalid called"},
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

		// --- dtx_stat command ---
		"dtx_stat default": {
			args: []string{"dtx_stat"},
			setup: func(t *testing.T) {
				ddb_run_dtx_stat_Fn = dtxStatFnChecking(t, "", false)
			},
			expStdout: []string{"dtx_stat called"},
		},
		"dtx_stat with details flag": {
			args: []string{"dtx_stat", "--details", "[0]"},
			setup: func(t *testing.T) {
				ddb_run_dtx_stat_Fn = dtxStatFnChecking(t, "[0]", true)
			},
			expStdout: []string{"dtx_stat called"},
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
