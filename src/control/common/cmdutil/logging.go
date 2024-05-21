//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package cmdutil

import (
	"context"
	"os"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
)

var _ LogSetter = (*LogCmd)(nil)

type (
	// LogSetter defines an interface to be implemented by types
	// that can set a logger.
	LogSetter interface {
		SetLog(log logging.Logger)
	}

	// LogCmd is an embeddable type that extends a command with
	// logging capabilities.
	LogCmd struct {
		logging.Logger
	}
	// LogConfig contains parameters used to configure the logger.
	LogConfig struct {
		LogFile  string
		LogLevel common.ControlLogLevel
		JSON     bool
	}
)

// SetLog sets the logger for the command.
func (cmd *LogCmd) SetLog(log logging.Logger) {
	cmd.Logger = log
}

// LogCtx returns a context with the command's logger set.
func (cmd *LogCmd) LogCtx() (context.Context, error) {
	return logging.ToContext(context.Background(), cmd.Logger)
}

// MustLogCtx returns a context with the command's logger set.
// NB: Panics on error.
func (cmd *LogCmd) MustLogCtx() context.Context {
	ctx, err := cmd.LogCtx()
	if err != nil {
		panic(err)
	}
	return ctx
}

// ConfigureLogger configures the logger according to the requested config.
func ConfigureLogger(logIn logging.Logger, cfg LogConfig) error {
	log, ok := logIn.(*logging.LeveledLogger)
	if !ok {
		return errors.New("logger is not a LeveledLogger")
	}

	// Set log level mask for default logger from config,
	// unless it was explicitly set to debug via CLI flag.
	applyLogConfig := func() error {
		switch logging.LogLevel(cfg.LogLevel) {
		case logging.LogLevelTrace:
			log.SetLevel(logging.LogLevelTrace)
			log.Debugf("Switching control log level to TRACE")
		case logging.LogLevelDebug:
			log.SetLevel(logging.LogLevelDebug)
			log.Debugf("Switching control log level to DEBUG")
		case logging.LogLevelNotice:
			log.Debugf("Switching control log level to NOTICE")
			log.SetLevel(logging.LogLevelNotice)
		case logging.LogLevelError:
			log.Debugf("Switching control log level to ERROR")
			log.SetLevel(logging.LogLevelError)
		}

		if cfg.JSON {
			log = log.WithJSONOutput()
		}

		log.Debugf("configured logging: level=%s, file=%s, json=%v",
			cfg.LogLevel, cfg.LogFile, cfg.JSON)

		return nil
	}

	hostname, err := os.Hostname()
	if err != nil {
		return errors.Wrap(err, "getting hostname")
	}

	// Set log file for default logger if specified in config.
	if cfg.LogFile != "" {
		f, err := common.AppendFile(cfg.LogFile)
		if err != nil {
			return errors.Wrap(err, "create log file")
		}

		log.Infof("%s logging to file %s", os.Args[0], cfg.LogFile)

		// Create an additional set of loggers which append everything
		// to the specified file.
		log = log.
			WithErrorLogger(logging.NewErrorLogger(hostname, f)).
			WithNoticeLogger(logging.NewNoticeLogger(hostname, f)).
			WithInfoLogger(logging.NewInfoLogger(hostname, f)).
			WithDebugLogger(logging.NewDebugLogger(f)).
			WithTraceLogger(logging.NewTraceLogger(f))

		return applyLogConfig()
	}

	log.Info("no control log file specified; logging to stdout")

	return applyLogConfig()
}
