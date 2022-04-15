//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package cmdutil

import (
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
