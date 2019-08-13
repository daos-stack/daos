package main

import (
	"os"

	"github.com/jessevdk/go-flags"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/logging"
)

type mainOpts struct {
	// Minimal set of top-level options
	ConfigPath string `short:"o" long:"config" description:"Server config file path"`
	Debug      bool   `short:"d" long:"debug" description:"Enable debug output"`
	JSON       bool   `short:"j" long:"json" description:"Enable JSON output"`

	// Define subcommands
	Storage storageCmd `command:"storage" description:"Perform tasks related to locally-attached storage"`
	Start   startCmd   `command:"start" description:"Start daos_server"`
}

type cmdLogger interface {
	setLog(*logging.LeveledLogger)
}

type logCmd struct {
	log *logging.LeveledLogger
}

func (c *logCmd) setLog(log *logging.LeveledLogger) {
	c.log = log
}

func exitWithError(log *logging.LeveledLogger, err error) {
	log.Debugf("%+v", err)
	log.Errorf("%v", err)
	os.Exit(1)
}

func parseOpts(args []string, opts *mainOpts, log *logging.LeveledLogger) error {
	p := flags.NewParser(opts, flags.Default)
	p.SubcommandsOptional = false
	p.Options ^= flags.PrintErrors // Don't allow the library to print errors
	p.CommandHandler = func(cmd flags.Commander, args []string) error {
		if cmd == nil {
			return nil
		}

		if opts.Debug {
			log.SetLevel(logging.LogLevelDebug)
		}
		if opts.JSON {
			log.WithJSONOutput()
		}

		if logCmd, ok := cmd.(cmdLogger); ok {
			logCmd.setLog(log)
		}

		if cfgCmd, ok := cmd.(cfgLoader); ok {
			if err := cfgCmd.loadConfig(opts.ConfigPath); err != nil {
				return errors.Wrapf(err, "failed to load config from %s", cfgCmd.configPath())
			}
			log.Debugf("DAOS config loaded from %s", cfgCmd.configPath())

			if ovrCmd, ok := cfgCmd.(cliOverrider); ok {
				if err := ovrCmd.setCLIOverrides(); err != nil {
					return errors.Wrap(err, "failed to set CLI config overrides")
				}
			}
		}

		if err := cmd.Execute(args); err != nil {
			return err
		}

		return nil
	}

	// Parse commandline flags which override options loaded from config.
	_, err := p.ParseArgs(args)
	if err != nil {
		return err
	}

	return nil
}

func main() {
	log := logging.NewCommandLineLogger()
	var opts mainOpts

	if err := parseOpts(os.Args[1:], &opts, log); err != nil {
		exitWithError(log, err)
	}
}
