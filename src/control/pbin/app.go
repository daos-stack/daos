//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pbin

import (
	"encoding/json"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
)

// processProvider is an interface for interacting with the current process.
type processProvider interface {
	CurrentProcessName() string
	ParentProcessName() (string, error)
	IsPrivileged() bool
	ElevatePrivileges() error
}

// NewApp sets up a new privileged application.
func NewApp() *App {
	app := &App{
		process:  &Process{},
		input:    os.Stdin,
		output:   os.Stdout,
		handlers: make(map[string]RequestHandler),
	}

	app.setupPing()
	app.configureLogging("")
	return app
}

// App is a framework for a privileged external helper application to be invoked by one or
// more DAOS processes.
type App struct {
	log            logging.Logger
	allowedCallers []string
	process        processProvider
	input          io.ReadCloser
	output         io.WriteCloser
	handlers       map[string]RequestHandler
}

// Name returns the name of this application.
func (a *App) Name() string {
	return a.process.CurrentProcessName()
}

// WithLogFile sets up App logging to a file at a given path.
func (a *App) WithLogFile(path string) *App {
	a.configureLogging(path)
	return a
}

// configureLogging configures the application's logger. If logPath
// is empty, non-error messages are not logged.
func (a *App) configureLogging(logPath string) {
	logLevel := logging.LogLevelError
	combinedOut := ioutil.Discard
	if logPath != "" {
		lf, err := common.AppendFile(logPath)
		if err == nil {
			combinedOut = lf
			logLevel = logging.LogLevelDebug
		}
	}

	// By default, we only want to log errors to stderr.
	a.log = logging.NewCombinedLogger(a.Name(), combinedOut).
		WithErrorLogger(logging.NewCommandLineErrorLogger(os.Stderr)).
		WithLogLevel(logLevel)
}

// WithAllowedCallers adds a list of process names allowed to call this
// application.
func (a *App) WithAllowedCallers(callers ...string) *App {
	a.allowedCallers = callers
	return a
}

// WithInput adds a custom input source to the App.
func (a *App) WithInput(reader io.ReadCloser) *App {
	a.input = reader
	return a
}

// WithOutput adds a custom output sink to the App.
func (a *App) WithOutput(writer io.WriteCloser) *App {
	a.output = writer
	return a
}

// AddHandler adds a new handler to the App for a given method.
// There is at most one handler per method.
func (a *App) AddHandler(method string, handler RequestHandler) {
	a.handlers[method] = handler
}

// setupPing sets up the default Ping handler built into every App.
func (a *App) setupPing() {
	a.AddHandler(PingMethod, newPingHandler(a.process))
}

// logError is a convenience method that logs an error and returns the same error.
func (a *App) logError(err error) error {
	if a.log != nil && err != nil {
		a.log.Error(err.Error())
	}
	return err
}

// Run executes the helper application process.
func (a *App) Run() error {
	parentName, err := a.process.ParentProcessName()
	if err != nil {
		return a.logError(err)
	}

	if !a.isCallerPermitted(parentName) {
		return a.logError(errors.Errorf("%s (version %s) may only be invoked by: %s",
			a.Name(), build.DaosVersion, strings.Join(a.allowedCallers, ", ")))
	}

	// set up the r/w pipe from the parent process
	conn := NewStdioConn(a.Name(), parentName, a.input, a.output)

	if err = a.checkPrivileges(); err != nil {
		resp := NewResponseWithError(a.logError(err))
		sendErr := a.writeResponse(resp, conn)
		if sendErr != nil {
			return a.logError(sendErr)
		}

		return err
	}

	req, err := a.readRequest(conn)
	if err != nil {
		return a.logError(err)
	}

	resp := a.handleRequest(req)

	err = a.writeResponse(resp, conn)
	if err != nil {
		return a.logError(err)
	}

	if resp.Error != nil {
		return resp.Error
	}
	return nil
}

func (a *App) isCallerPermitted(callerName string) bool {
	if len(a.allowedCallers) == 0 {
		return true
	}
	for _, name := range a.allowedCallers {
		if callerName == name {
			return true
		}
	}
	return false
}

func (a *App) checkPrivileges() error {
	if !a.process.IsPrivileged() {
		return PrivilegedHelperNotPrivileged(a.Name())
	}

	// hack for stuff that doesn't use geteuid() (e.g. ipmctl)
	if err := a.process.ElevatePrivileges(); err != nil {
		return err
	}

	return nil
}

func (a *App) readRequest(rdr io.Reader) (*Request, error) {
	buf, err := ReadMessage(rdr)
	if err != nil {
		return nil, errors.Wrap(err, "failed to read request")
	}

	var req Request
	if err := json.Unmarshal(buf, &req); err != nil {
		return nil, errors.Wrap(err, "failed to unmarshal request")
	}

	return &req, nil
}

func (a *App) handleRequest(req *Request) *Response {
	reqHandler, ok := a.handlers[req.Method]
	if !ok {
		err := a.logError(errors.Errorf("unhandled method %q", req.Method))
		return NewResponseWithError(err)
	}

	resp := reqHandler.Handle(a.log, req)
	if resp == nil {
		err := a.logError(errors.Errorf("handler for method %q returned nil", req.Method))
		return NewResponseWithError(err)
	}

	if resp.Error != nil {
		_ = a.logError(resp.Error)
	}
	return resp
}

func (a *App) writeResponse(res *Response, dest io.Writer) error {
	data, err := json.Marshal(res)
	if err != nil {
		return errors.Wrap(err, fmt.Sprintf("failed to marshal response %+v", res))
	}

	_, err = dest.Write(data)
	return errors.Wrap(err, fmt.Sprintf("failed to send response %+v", res))
}
