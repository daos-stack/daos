//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

/*
#include <stdint.h>
*/
import "C"
import (
	"context"
	"errors"
	"io"
	"os"
	"runtime/cgo"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/logging"
)

// ctrlContext holds the client connection state for a management context.
type ctrlContext struct {
	client  control.UnaryInvoker
	cfg     *control.Config
	log     *logging.LeveledLogger
	logFile *os.File
}

// newContext creates a new management context from a config file path.
// If configFile is empty, uses default config (localhost, insecure mode).
// logFilePath and logLevelStr configure logging; empty strings use defaults.
func newContext(configFile, logFilePath, logLevelStr string) (*ctrlContext, error) {
	var cfg *control.Config
	var err error

	if configFile == "" {
		// No config file - use default config (similar to dmg -i)
		cfg = control.DefaultConfig()
		cfg.TransportConfig.AllowInsecure = true
	} else {
		cfg, err = control.LoadConfig(configFile)
		if err != nil {
			return nil, err
		}
	}

	// Logging is quiet by default.
	var logDest io.Writer = io.Discard
	var logFile *os.File

	if logFilePath != "" {
		logFile, err = os.OpenFile(logFilePath, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0644)
		if err != nil {
			return nil, err
		}
		logDest = logFile
	}

	log := logging.NewCombinedLogger("daos_control", logDest).
		WithLogLevel(logging.LogLevelNotice)

	if logLevelStr != "" {
		var level logging.LogLevel
		if err := level.SetString(logLevelStr); err == nil {
			log.SetLevel(level)
		}
	}

	client := control.NewClient(
		control.WithConfig(cfg),
		control.WithClientLogger(log),
		control.WithClientComponent(build.ComponentAdmin),
	)

	return &ctrlContext{
		client:  client,
		cfg:     cfg,
		log:     log,
		logFile: logFile,
	}, nil
}

// ctx returns a background context for operations.
func (c *ctrlContext) ctx() context.Context {
	ctx, _ := logging.ToContext(context.Background(), c.log)
	return ctx
}

// close releases resources associated with the context.
func (c *ctrlContext) close() {
	if c.logFile != nil {
		c.logFile.Close()
	}
}

// newTestContext creates a context with a mock invoker for testing.
func newTestContext(client control.UnaryInvoker, log *logging.LeveledLogger) *ctrlContext {
	if log == nil {
		log = logging.NewCombinedLogger("test", io.Discard)
	}
	return &ctrlContext{
		client: client,
		cfg:    control.DefaultConfig(),
		log:    log,
	}
}

// errInvalidHandle is returned when a zero/invalid handle is provided.
var errInvalidHandle = errors.New("invalid control API handle")

// getContext validates the handle and retrieves the context.
// Returns nil and a non-zero error code if the handle is invalid.
func getContext(handle C.uintptr_t) (ctx *ctrlContext, rc C.int) {
	if handle == 0 {
		return nil, C.int(errorToRC(errInvalidHandle))
	}

	defer func() {
		if r := recover(); r != nil {
			ctx = nil
			rc = C.int(errorToRC(errInvalidHandle))
		}
	}()

	h := cgo.Handle(handle)
	c, ok := h.Value().(*ctrlContext)
	if !ok {
		return nil, C.int(errorToRC(errInvalidHandle))
	}
	return c, 0
}
