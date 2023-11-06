package main

import (
	"context"
	"os"
	"path"
	"runtime/debug"

	"github.com/jessevdk/go-flags"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/lib/atm"
	daosAPI "github.com/daos-stack/daos/src/control/lib/daos/client"
	"github.com/daos-stack/daos/src/control/logging"
)

type daosCmd struct {
	daosCtx context.Context
	cmdutil.LogCmd
	cmdutil.NoArgsCmd
	cmdutil.JSONOutputCmd
}

func (cmd *daosCmd) setCtx(ctx context.Context) {
	cmd.daosCtx = ctx
}

type cliOptions struct {
	Quiet         bool             `long:"quiet" short:"q" description:"Only display output at ERROR or higher"`
	Debug         bool             `long:"debug" description:"enable debug output"`
	Verbose       bool             `long:"verbose" description:"enable verbose output (when applicable)"`
	JSON          bool             `long:"json" short:"j" description:"enable JSON output"`
	ManPage       cmdutil.ManCmd   `command:"manpage" hidden:"true"`
	ConnectStress connectStressCmd `command:"connect" description:"connection stress tests"`
}

func exitWithError(log logging.Logger, err error) {
	cmdName := path.Base(os.Args[0])
	log.Errorf("%s: %v", cmdName, err)
	if fault.HasResolution(err) {
		log.Errorf("%s: %s", cmdName, fault.ShowResolutionFor(err))
	}
	os.Exit(1)
}

func initDaosDebug() (func(), error) {
	if err := daosAPI.DebugInit(); err != nil {
		return nil, err
	}

	return func() {
		daosAPI.DebugFini()
	}, nil
}

func parseOpts(parent context.Context, args []string, opts *cliOptions, log *logging.LeveledLogger) error {
	var wroteJSON atm.Bool
	p := flags.NewParser(opts, flags.Default)
	p.Name = "dstress"
	p.ShortDescription = "Command to stress test DAOS"
	p.LongDescription = `the dstress tool can be used to stress test DAOS`
	p.Options ^= flags.PrintErrors // Don't allow the library to print errors
	p.CommandHandler = func(cmd flags.Commander, args []string) error {
		if cmd == nil {
			return nil
		}

		if manCmd, ok := cmd.(cmdutil.ManPageWriter); ok {
			manCmd.SetWriteFunc(p.WriteManPage)
			return cmd.Execute(args)
		}

		if opts.Debug && opts.Quiet {
			return errors.New("--debug and --quiet are incompatible")
		}

		if opts.Quiet {
			log.SetLevel(logging.LogLevelError)
		}

		if opts.Debug {
			log.SetLevel(logging.LogLevelTrace)
			if os.Getenv("D_LOG_MASK") == "" {
				os.Setenv("D_LOG_MASK", "DEBUG,OBJECT=ERR,PLACEMENT=ERR")
			}
			if os.Getenv("DD_MASK") == "" {
				os.Setenv("DD_MASK", "mgmt")
			}
			log.Debug("debug output enabled")
			daosAPI.SetDebugLog(log)
		}

		if jsonCmd, ok := cmd.(cmdutil.JSONOutputter); ok && opts.JSON {
			jsonCmd.EnableJSONOutput(os.Stdout, &wroteJSON)
			// disable output on stdout other than JSON
			log.ClearLevel(logging.LogLevelInfo)
		}

		if logCmd, ok := cmd.(cmdutil.LogSetter); ok {
			logCmd.SetLog(log)
		}

		if daosCmd, ok := cmd.(interface{ setCtx(context.Context) }); ok {
			ctx, err := daosAPI.Init(parent)
			if err != nil {
				return err
			}
			daosCmd.setCtx(ctx)
			//defer daosAPI.Fini(ctx)
		}

		if argsCmd, ok := cmd.(cmdutil.ArgsHandler); ok {
			if err := argsCmd.CheckArgs(args); err != nil {
				return err
			}
		}

		if opts.Quiet {
			args = append(args, "quiet")
		}
		if err := cmd.Execute(args); err != nil {
			return err
		}

		return nil
	}

	// Configure DAOS client logging to stderr if no log file
	// is specified. This is to avoid polluting the JSON output.
	if os.Getenv("D_LOG_FILE") == "" {
		os.Setenv("D_LOG_FILE", "/dev/null")
		if os.Getenv("DD_STDERR") == "" {
			os.Setenv("DD_STDERR", "debug")
		}
	} else if os.Getenv("DD_STDERR") == "" {
		os.Setenv("DD_STDERR", "err")
	}

	// Initialize the daos debug system first so that
	// any allocations made as part of argument parsing
	// are logged when running under NLT.
	debugFini, err := initDaosDebug()
	if err != nil {
		exitWithError(log, err)
	}
	defer debugFini()

	// Set the traceback level such that a crash results in
	// a coredump (when ulimit -c is set appropriately).
	debug.SetTraceback("crash")

	_, err = p.ParseArgs(args)
	if opts.JSON && wroteJSON.IsFalse() {
		return cmdutil.OutputJSON(os.Stdout, nil, err)
	}
	return err
}

func main() {
	var opts cliOptions
	log := logging.NewCommandLineLogger()

	ctx := context.Background()
	if err := parseOpts(ctx, os.Args[1:], &opts, log); err != nil {
		if fe, ok := errors.Cause(err).(*flags.Error); ok && fe.Type == flags.ErrHelp {
			log.Info(fe.Error())
			os.Exit(0)
		}
		exitWithError(log, err)
	}
}
