//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/engine"
)

func TestParseOpts(t *testing.T) {
	for name, tc := range map[string]struct {
		args      []string
		checkFunc func(opts *cliOptions) error
		expStdout []string
		expErr    error
	}{
		"General help message": {
			args: []string{"--help"},
			expStdout: []string{
				"Usage:\n  ddb [OPTIONS] [ddb_command] [ddb_command_args...]\n",
				"Vos Tree Paths:\n",
				"Available Commands:\n",
			},
		},
		"General help message with opt": {
			args: []string{"-w", "--help"},
			expStdout: []string{
				"Usage:\n  ddb [OPTIONS] [ddb_command] [ddb_command_args...]\n",
				"Vos Tree Paths:\n",
				"Available Commands:\n",
			},
		},
		"Unknown commands with help": {
			args:   []string{"foo", "--help"},
			expErr: errUnknownCmd,
		},
		"Unknown commands with help and opt": {
			args:   []string{"-w", "foo", "--help"},
			expErr: errUnknownCmd,
		},
		"Default option values": {
			args: []string{"ls", "-d", "-r"},
			checkFunc: func(opts *cliOptions) error {
				if opts.Debug != "" {
					return fmt.Errorf("expected Debug to be empty, got %q", opts.Debug)
				}
				if opts.WriteMode {
					return fmt.Errorf("expected WriteMode to be false")
				}
				if opts.CmdFile != "" {
					return fmt.Errorf("expected CmdFile to be empty")
				}
				if opts.SysdbPath != "" {
					return fmt.Errorf("expected SysdbPath to be empty")
				}
				if opts.VosPath != "" {
					return fmt.Errorf("expected VosPath to be empty")
				}
				if opts.Args.RunCmd != "ls" {
					return fmt.Errorf("expected RunCmd to be 'ls', got %q", opts.Args.RunCmd)
				}
				if opts.Args.RunCmdArgs[0] != "-d" {
					return fmt.Errorf("expected first RunCmdArgs to be '-d', got %q", opts.Args.RunCmdArgs[0])
				}
				if opts.Args.RunCmdArgs[1] != "-r" {
					return fmt.Errorf("expected second RunCmdArgs to be '-r', got %q", opts.Args.RunCmdArgs[1])
				}
				return nil
			},
		},
		"Short miss vos path error": {
			args:   []string{"-p", "/foo", "ls"},
			expErr: ddbTestErr(vosPathMissErr),
		},
		"Long miss vos path error": {
			args:   []string{"--db_path=/bar", "ls"},
			expErr: ddbTestErr(vosPathMissErr),
		},
		"Short cmd args error": {
			args:   []string{"-f", "/foo/bar.cmd", "ls"},
			expErr: ddbTestErr(runCmdArgsErr),
		},
		"Long cmd args error": {
			args:   []string{"--cmd_file=/foo/bar.cmd", "ls"},
			expErr: ddbTestErr(runCmdArgsErr),
		},
		"Short vos path miss error": {
			args:   []string{"-p", "/foo"},
			expErr: ddbTestErr(vosPathMissErr),
		},
		"Long vos path miss error": {
			args:   []string{"--db_path=/foo"},
			expErr: ddbTestErr(vosPathMissErr),
		},
		"Long debug option": {
			args: []string{"--debug=DEBUG", "ls"},
			checkFunc: func(opts *cliOptions) error {
				if opts.Debug != "DEBUG" {
					return fmt.Errorf("expected Debug to be 'DEBUG', got %q", opts.Debug)
				}
				return nil
			},
		},
		"Short write option": {
			args: []string{"-w", "ls"},
			checkFunc: func(opts *cliOptions) error {
				if !opts.WriteMode {
					return fmt.Errorf("expected WriteMode to be true")
				}
				return nil
			},
		},
		"Long write option": {
			args: []string{"--write_mode", "ls"},
			checkFunc: func(opts *cliOptions) error {
				if !opts.WriteMode {
					return fmt.Errorf("expected WriteMode to be true")
				}
				return nil
			},
		},
		"Short vos path option": {
			args: []string{"-s", "/foo/vos-0", "ls"},
			checkFunc: func(opts *cliOptions) error {
				if opts.VosPath != "/foo/vos-0" {
					return fmt.Errorf("expected VosPath to be '/foo/vos-0', got %q", opts.VosPath)
				}
				return nil
			},
		},
		"Long vos path option": {
			args: []string{"--vos_path=/foo/vos-0", "ls"},
			checkFunc: func(opts *cliOptions) error {
				if opts.VosPath != "/foo/vos-0" {
					return fmt.Errorf("expected VosPath to be '/foo/vos-0', got %q", opts.VosPath)
				}
				return nil
			},
		},
		"Short db path option": {
			args: []string{"-s", "/foo/vos-0", "-p", "/bar", "ls"},
			checkFunc: func(opts *cliOptions) error {
				if opts.VosPath != "/foo/vos-0" {
					return fmt.Errorf("expected VosPath to be '/foo/vos-0', got %q", opts.VosPath)
				}
				if opts.SysdbPath != "/bar" {
					return fmt.Errorf("expected SysdbPath to be '/bar', got %q", opts.SysdbPath)
				}
				return nil
			},
		},
		"Long db path option": {
			args: []string{"--vos_path=/foo/vos-0", "--db_path=/bar", "ls"},
			checkFunc: func(opts *cliOptions) error {
				if opts.VosPath != "/foo/vos-0" {
					return fmt.Errorf("expected VosPath to be '/foo/vos-0', got %q", opts.VosPath)
				}
				if opts.SysdbPath != "/bar" {
					return fmt.Errorf("expected SysdbPath to be '/bar', got %q", opts.SysdbPath)
				}
				return nil
			},
		},
		"Short version option": {
			args: []string{"-v"},
			checkFunc: func(opts *cliOptions) error {
				if !opts.Version {
					return fmt.Errorf("expected Version to be true")
				}
				return nil
			},
		},
		"Long version option": {
			args: []string{"--version"},
			checkFunc: func(opts *cliOptions) error {
				if !opts.Version {
					return fmt.Errorf("expected Version to be true")
				}
				return nil
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			var ctx DdbContextStub

			opts, stdout, err := runCmdToStdout(&ctx, tc.args)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			for _, msg := range tc.expStdout {
				test.AssertTrue(t, strings.Contains(stdout, msg),
					fmt.Sprintf("expected stdout to contain %q: got\n%s", msg, stdout))
			}

			if tc.checkFunc != nil {
				if err := tc.checkFunc(&opts); err != nil {
					t.Fatal(err)
				}
			}
		})
	}
}

// TestRun covers the non-interactive execution paths of run() (command-line and
// command-file modes). Interactive mode is intentionally not tested: it delegates
// entirely to grumble's app.Run(), which requires a real terminal (readline) and
// piping os.Stdin — making tests fragile and hard to maintain for little gain.
func TestRun(t *testing.T) {
	for name, tc := range map[string]struct {
		args      []string
		setup     func(ctx *DdbContextStub)
		expStdout []string
		expErr    error
		// When cmdFileCmd is non-empty the test is also run in command-file mode.
		// cmdFileArgs holds the CLI flags (everything except the positional command),
		// and cmdFileCmd is the line written to the temporary command file.
		// Note: "no auto-open" cases intentionally omit cmdFileCmd because in
		// command-file mode opts.Args.RunCmd is always empty, so noAutoOpen is
		// never triggered and the CLI would pre-open the pool.
		cmdFileArgs []string
		cmdFileCmd  string
	}{
		"Version output": {
			args:      []string{"-v"},
			expStdout: []string{"ddb version"},
		},
		"Long version output": {
			args:      []string{"--version"},
			expStdout: []string{"ddb version"},
		},
		"Unknown command": {
			args:        []string{"foo"},
			expErr:      errUnknownCmd,
			cmdFileArgs: []string{},
			cmdFileCmd:  "foo",
		},
		"Unknown command with write option": {
			args:        []string{"-w", "foo"},
			expErr:      errUnknownCmd,
			cmdFileArgs: []string{"-w"},
			cmdFileCmd:  "foo",
		},
		"Open called with short vos path and db path": {
			args: []string{"-s", "/foo/vos-0", "-p", "/bar", "ls"},
			setup: func(ctx *DdbContextStub) {
				ctx.open = func(path string, db_path string, write_mode bool) error {
					fmt.Println("Open called")
					if err := isArgEqual("/foo/vos-0", path, "vos path"); err != nil {
						return err
					}
					if err := isArgEqual("/bar", db_path, "sysdb path"); err != nil {
						return err
					}
					return nil
				}
			},
			expStdout:   []string{"Open called"},
			cmdFileArgs: []string{"-s", "/foo/vos-0", "-p", "/bar"},
			cmdFileCmd:  "ls",
		},
		"Open called with long vos path and db path": {
			args: []string{"--vos_path=/foo/vos-0", "--db_path=/bar", "ls"},
			setup: func(ctx *DdbContextStub) {
				ctx.open = func(path string, db_path string, write_mode bool) error {
					fmt.Println("Open called")
					if err := isArgEqual("/foo/vos-0", path, "vos path"); err != nil {
						return err
					}
					if err := isArgEqual("/bar", db_path, "sysdb path"); err != nil {
						return err
					}
					return nil
				}
			},
			expStdout:   []string{"Open called"},
			cmdFileArgs: []string{"--vos_path=/foo/vos-0", "--db_path=/bar"},
			cmdFileCmd:  "ls",
		},
		"Open called with write mode": {
			args: []string{"-w", "-s", "/foo/vos-0", "ls"},
			setup: func(ctx *DdbContextStub) {
				ctx.open = func(path string, db_path string, write_mode bool) error {
					fmt.Println("Open called")
					if err := isArgEqual(true, write_mode, "write_mode"); err != nil {
						return err
					}
					return nil
				}
			},
			expStdout:   []string{"Open called"},
			cmdFileArgs: []string{"-w", "-s", "/foo/vos-0"},
			cmdFileCmd:  "ls",
		},
		"No auto-open for feature command": {
			// noAutoOpen is keyed on opts.Args.RunCmd which is empty in command-file
			// mode, so this case only applies to command-line mode.
			args: []string{"-s", "/foo/vos-0", "feature", "--show"},
			setup: func(ctx *DdbContextStub) {
				ctx.open = func(path string, db_path string, write_mode bool) error {
					return fmt.Errorf("open should not have been called")
				}
			},
		},
		"No auto-open for open command": {
			// The CLI should NOT pre-open when the 'open' command is issued; only the
			// command itself should call ctx.Open (exactly once).
			// Only valid for command-line mode (see note above).
			args: []string{"-s", "/foo/vos-0", "open", "/foo/vos-0"},
			setup: func(ctx *DdbContextStub) {
				openCount := 0
				ctx.open = func(path string, db_path string, write_mode bool) error {
					openCount++
					if openCount > 1 {
						return fmt.Errorf("open pre-opened by CLI (called %d times)", openCount)
					}
					return nil
				}
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			checkRun := func(t *testing.T, stdout string, err error) {
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

			// Command-line mode
			t.Run("command-line", func(t *testing.T) {
				var ctx DdbContextStub
				if tc.setup != nil {
					tc.setup(&ctx)
				}
				stdout, err := runMainFlow(&ctx, tc.args)
				checkRun(t, stdout, err)
			})

			// Command-file mode (only for cases that provide a command file command)
			if tc.cmdFileCmd != "" {
				t.Run("command-file", func(t *testing.T) {
					tmpDir := t.TempDir()
					cmdFile := filepath.Join(tmpDir, "cmds.txt")
					if err := os.WriteFile(cmdFile, []byte(tc.cmdFileCmd), 0644); err != nil {
						t.Fatalf("failed to write command file: %v", err)
					}

					var ctx DdbContextStub
					if tc.setup != nil {
						tc.setup(&ctx)
					}
					args := append(tc.cmdFileArgs, "--cmd_file="+cmdFile)
					stdout, err := runMainFlow(&ctx, args)
					checkRun(t, stdout, err)
				})
			}
		})
	}
}

func TestStrToLogLevels(t *testing.T) {
	for name, tc := range map[string]struct {
		input          string
		expCliLevel    logging.LogLevel
		expEngineLevel engine.LogLevel
		expErr         bool
	}{
		"TRACE":  {input: "TRACE", expCliLevel: logging.LogLevelTrace, expEngineLevel: engine.LogLevelDbug},
		"DEBUG":  {input: "DEBUG", expCliLevel: logging.LogLevelDebug, expEngineLevel: engine.LogLevelDbug},
		"DBUG":   {input: "DBUG", expCliLevel: logging.LogLevelDebug, expEngineLevel: engine.LogLevelDbug},
		"INFO":   {input: "INFO", expCliLevel: logging.LogLevelInfo, expEngineLevel: engine.LogLevelInfo},
		"NOTE":   {input: "NOTE", expCliLevel: logging.LogLevelNotice, expEngineLevel: engine.LogLevelNote},
		"NOTICE": {input: "NOTICE", expCliLevel: logging.LogLevelNotice, expEngineLevel: engine.LogLevelNote},
		"WARN":   {input: "WARN", expCliLevel: logging.LogLevelNotice, expEngineLevel: engine.LogLevelWarn},
		"ERROR":  {input: "ERROR", expCliLevel: logging.LogLevelError, expEngineLevel: engine.LogLevelErr},
		"ERR":    {input: "ERR", expCliLevel: logging.LogLevelError, expEngineLevel: engine.LogLevelErr},
		"CRIT":   {input: "CRIT", expCliLevel: logging.LogLevelError, expEngineLevel: engine.LogLevelCrit},
		"ALRT":   {input: "ALRT", expCliLevel: logging.LogLevelError, expEngineLevel: engine.LogLevelAlrt},
		"FATAL":  {input: "FATAL", expCliLevel: logging.LogLevelError, expEngineLevel: engine.LogLevelEmrg},
		"EMRG":   {input: "EMRG", expCliLevel: logging.LogLevelError, expEngineLevel: engine.LogLevelEmrg},
		"EMIT":   {input: "EMIT", expCliLevel: logging.LogLevelError, expEngineLevel: engine.LogLevelEmit},
		"lowercase debug": {
			input: "debug", expCliLevel: logging.LogLevelDebug, expEngineLevel: engine.LogLevelDbug,
		},
		"invalid level": {input: "INVALID", expErr: true},
		"empty string":  {input: "", expErr: true},
	} {
		t.Run(name, func(t *testing.T) {
			cliLevel, engineLevel, err := strToLogLevels(tc.input)
			if tc.expErr {
				if err == nil {
					t.Fatalf("expected an error for input %q: got nil", tc.input)
				}
				return
			}
			if err != nil {
				t.Fatalf("unexpected error for input %q: %v", tc.input, err)
			}
			test.AssertTrue(t, cliLevel == tc.expCliLevel,
				fmt.Sprintf("unexpected CLI log level for input %q: want %v, got %v", tc.input, tc.expCliLevel, cliLevel))
			test.AssertTrue(t, engineLevel == tc.expEngineLevel,
				fmt.Sprintf("unexpected engine log level for input %q: want %v, got %v", tc.input, tc.expEngineLevel, engineLevel))
		})
	}
}
