//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package cmdutil

import (
	"context"

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
