package main

import (
	"fmt"
	"strings"
	"testing"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestParseOpts(t *testing.T) {
	for name, tc := range map[string]struct {
		args      []string
		checkFunc func(opts *cliOptions) error
		expCalls  func(ctx *DdbContextStub)
		expStdout []string
		expErr    error
	}{
		"General help message": {
			args: []string{"--help"},
			expStdout: []string{
				"Usage:\n  ddb [OPTIONS] [ddb_command] [ddb_command_args...]\n",
				"Example Paths:\n",
				"Available commands:\n",
			},
		},
		"Unknown commands": {
			args: []string{"foo"},
			expStdout: []string{
				"Available commands:\n",
			},
			expErr: errUnknownCmd,
		},
		"Unknown commands with help": {
			args: []string{"foo", "--help"},
			expStdout: []string{
				"Available commands:\n",
			},
			expErr: errUnknownCmd,
		},
		"General help message with opt": {
			args: []string{"-w", "--help"},
			expStdout: []string{
				"Usage:\n  ddb [OPTIONS] [ddb_command] [ddb_command_args...]\n",
				"Example Paths:\n",
				"Available commands:\n",
			},
		},
		"Unknown commands with opt": {
			args: []string{"-w", "foo"},
			expStdout: []string{
				"Available commands:\n",
			},
			expErr: errUnknownCmd,
		},
		"Unknown commands with help and opt": {
			args: []string{"-w", "foo", "--help"},
			expStdout: []string{
				"Available commands:\n",
			},
			expErr: errUnknownCmd,
		},
		"Default option value": {
			args: []string{"ls", "-d", "-r"},
			checkFunc: func(opts *cliOptions) error {
				if opts.Debug {
					return fmt.Errorf("expected debug to be false")
				}
				if opts.WriteMode {
					return fmt.Errorf("expected writable to be false")
				}
				if opts.CmdFile != "" {
					return fmt.Errorf("expected cmd file to be empty")
				}
				if opts.SysdbPath != "" {
					return fmt.Errorf("expected sysdb path to be empty")
				}
				if opts.VosPath != "" {
					return fmt.Errorf("expected vos path to be empty")
				}
				if opts.Args.RunCmd != "ls" {
					return fmt.Errorf("expected command to be 'ls'")
				}
				if opts.Args.RunCmdArgs[0] != "-d" {
					return fmt.Errorf("expected first command arg to be '-d'")
				}
				if opts.Args.RunCmdArgs[1] != "-r" {
					return fmt.Errorf("expected second command arg to be '-r'")
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
		"long cmd args error": {
			args:   []string{"--cmd_file=/foo/bar.cmd", "ls"},
			expErr: ddbTestErr(runCmdArgsErr),
		},
		"Short vos path miss error": {
			args:   []string{"-p /foo"},
			expErr: ddbTestErr(vosPathMissErr),
		},
		"Long vos path miss error": {
			args:   []string{"--db_path=/foo"},
			expErr: ddbTestErr(vosPathMissErr),
		},
		"Long debug option": {
			args: []string{"--debug", "ls"},
			checkFunc: func(opts *cliOptions) error {
				if !opts.Debug {
					return fmt.Errorf("expected debug to be true")
				}
				return nil
			},
		},
		"Short write option": {
			args: []string{"-w", "ls"},
			checkFunc: func(opts *cliOptions) error {
				if !opts.WriteMode {
					return fmt.Errorf("expected writable to be true")
				}
				return nil
			},
		},
		"Long write option": {
			args: []string{"--write_mode", "ls"},
			checkFunc: func(opts *cliOptions) error {
				if !opts.WriteMode {
					return fmt.Errorf("expected writable to be true")
				}
				return nil
			},
		},
		"Short vos path option": {
			args: []string{"-s", "/foo/vos-0", "ls"},
			checkFunc: func(opts *cliOptions) error {
				if opts.VosPath != "/foo/vos-0" {
					return fmt.Errorf("expected vos path to be '/foo/vos-0'")
				}
				return nil
			},
		},
		"Long vos path option": {
			args: []string{"--vos_path=/foo/vos-0", "ls"},
			checkFunc: func(opts *cliOptions) error {
				if opts.VosPath != "/foo/vos-0" {
					return fmt.Errorf("expected vos path to be '/foo/vos-0'")
				}
				return nil
			},
		},
		"Short db path option": {
			args: []string{"-s", "/foo/vos-0", "-p", "/bar", "ls"},
			checkFunc: func(opts *cliOptions) error {
				if opts.VosPath != "/foo/vos-0" {
					return fmt.Errorf("expected vos path to be '/foo/vos-0'")
				}
				if opts.SysdbPath != "/bar" {
					return fmt.Errorf("expected sysdb path to be '/bar'")
				}
				return nil
			},
			expCalls: func(ctx *DdbContextStub) {
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
			expStdout: []string{"Open called"},
		},
		"Long db path option": {
			args: []string{"--vos_path=/foo/vos-0", "--db_path=/bar", "ls"},
			checkFunc: func(opts *cliOptions) error {
				if opts.VosPath != "/foo/vos-0" {
					return fmt.Errorf("expected vos path to be '/foo/vos-0'")
				}
				if opts.SysdbPath != "/bar" {
					return fmt.Errorf("expected sysdb path to be '/bar'")
				}
				return nil
			},
			expCalls: func(ctx *DdbContextStub) {
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
			expStdout: []string{"Open called"},
		},
		"Short version option": {
			args: []string{"-v"},
			checkFunc: func(opts *cliOptions) error {
				if !opts.Version {
					return fmt.Errorf("expected writable to be true")
				}
				return nil
			},
			expStdout: []string{"ddb version"},
		},
		"Long version option": {
			args: []string{"--version"},
			checkFunc: func(opts *cliOptions) error {
				if !opts.Version {
					return fmt.Errorf("expected writable to be true")
				}
				return nil
			},
			expStdout: []string{"ddb version"},
		},
	} {
		t.Run(name, func(t *testing.T) {
			var ctx DdbContextStub
			var opts cliOptions

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

			if tc.checkFunc != nil {
				if err := tc.checkFunc(&opts); err != nil {
					t.Fatal(err)
				}
			}
		})
	}
}
