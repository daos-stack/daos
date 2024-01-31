//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package logging

import (
	"context"
	"errors"
)

type contextKeyType string

var contextKey contextKeyType = "logging.Logger"

func getCtxLogger(ctx context.Context) (Logger, bool) {
	if ctx == nil {
		return nil, false
	}

	if logger, ok := ctx.Value(contextKey).(Logger); ok {
		return logger, true
	}
	return nil, false
}

// FromContext returns the logger from the context,
// or a no-op logger if no logger is present.
func FromContext(ctx context.Context) Logger {
	if logger, ok := getCtxLogger(ctx); ok {
		return logger
	}
	return &LeveledLogger{level: LogLevelDisabled}
}

// ToContext adds the logger to the context if
// it is not already present.
func ToContext(ctx context.Context, logger Logger) (context.Context, error) {
	if ctx == nil {
		return nil, errors.New("nil context")
	}
	if logger == nil {
		return nil, errors.New("nil logger")
	}

	if _, ok := getCtxLogger(ctx); ok {
		return nil, errors.New("logger already present in context")
	}
	return context.WithValue(ctx, contextKey, logger), nil
}
