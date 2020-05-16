//
// (C) Copyright 2020 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package helper

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/pbin"
)

func expectDefaultProcess(t *testing.T, app *App) {
	t.Helper()

	switch app.process.(type) {
	case *Process:
	default:
		t.Errorf("expected process type Process, got %T", app.process)
	}
}

func expectCallers(t *testing.T, app *App, expCallers []string) {
	t.Helper()

	if diff := cmp.Diff(expCallers, app.allowedCallers); diff != "" {
		t.Errorf("callers not set correctly (-want, +got):\n%s\n", diff)
	}
}

func TestHelper_NewApp(t *testing.T) {
	app := NewApp()

	if app == nil {
		t.Fatal("resulting app was nil")
	}

	expectDefaultProcess(t, app)
	common.AssertNotEqual(t, app.log, nil, "expected non-nil logger")
	common.AssertEqual(t, len(app.allowedCallers), 0, "expected no callers by default")
	common.AssertEqual(t, len(app.handlers), 0, "expected no handlers by default")
	common.AssertEqual(t, app.input, os.Stdin, "default input should be stdin")
	common.AssertEqual(t, app.output, os.Stdout, "default output should be stdout")
}

func TestHelperApp_WithCallers(t *testing.T) {
	expCallers := []string{"caller1", "caller2"}

	app := NewApp().WithAllowedCallers(expCallers...)

	if app == nil {
		t.Fatal("resulting app was nil")
	}

	expectCallers(t, app, expCallers)
}

func TestHelperApp_WithLogFile_GoodPath(t *testing.T) {
	logDir, cleanup := common.CreateTestDir(t)
	defer cleanup()

	logFile := filepath.Join(logDir, "test.log")

	app := NewApp().WithLogFile(logFile)

	if app == nil {
		t.Fatal("resulting app was nil")
	}

	common.AssertNotEqual(t, app.log, nil, "expected non-nil logger")

	_, err := os.Stat(logFile)
	if err != nil {
		t.Errorf("expected log file to be created, but stat failed: %v", err)
	}
}

func TestHelperApp_WithLogFile_BadPath(t *testing.T) {
	// Bad path should silently fall back to acting as if no path supplied
	app := NewApp().WithLogFile("/not/a/real/path")

	if app == nil {
		t.Fatal("resulting app was nil")
	}

	common.AssertNotEqual(t, app.log, nil, "expected non-nil logger")
}

func TestHelperApp_WithInput(t *testing.T) {
	expInput := &mockReadWriter{}
	app := NewApp().WithInput(expInput)

	if app == nil {
		t.Fatal("resulting app was nil")
	}

	common.AssertEqual(t, app.input, expInput, "input should be custom")
}

func TestHelperApp_WithOutput(t *testing.T) {
	expOutput := &mockReadWriter{}
	app := NewApp().WithOutput(expOutput)

	if app == nil {
		t.Fatal("resulting app was nil")
	}

	common.AssertEqual(t, app.output, expOutput, "output should be custom")
}

func expectHandlerAdded(t *testing.T, app *App, name string, handler RequestHandler, expLen int) {
	t.Helper()

	app.AddHandler(name, handler)
	if _, ok := app.handlers[name]; !ok {
		t.Fatal("added handler not found in map")
	}
	common.AssertEqual(t, len(app.handlers), expLen, "incorrect resulting number of handlers")
}

func TestHelperApp_AddHandler(t *testing.T) {
	h1 := &testHandler{}
	h2 := &testHandler{outputResp: &pbin.Response{}}

	app := NewApp()
	expectHandlerAdded(t, app, "Method1", h1, 1)
	expectHandlerAdded(t, app, "Method2", h2, 2)
	expectHandlerAdded(t, app, "Method1", h2, 2) // Should overwrite original Method1 handler
}

func TestHelperApp_Name(t *testing.T) {
	mockProcess := defaultMockProcess()
	app := newTestApp(mockProcess)

	common.AssertEqual(t, mockProcess.name, app.Name(), "name didn't come from process")
}

// testPayload is a pbin.Response payload used by the test handler.
type testPayload struct {
	result string
}

// testHandler is an implementation of the RequestHandler for unit tests.
type testHandler struct {
	outputResp *pbin.Response
}

func (h *testHandler) Handle(_ logging.Logger, _ *pbin.Request) *pbin.Response {
	return h.outputResp
}

func TestHelperApp_Run(t *testing.T) {
	testMethod := "TestMethod"
	defaultReq := &pbin.Request{
		Method: testMethod,
	}
	defaultResp := NewResponseWithPayload(&testPayload{result: "defaultResp"})

	for name, tc := range map[string]struct {
		process        *mockProcess
		allowedCallers []string
		inputReq       *pbin.Request
		readErr        error
		outputResp     *pbin.Response
		writeErr       error
		expResp        *pbin.Response
		expErr         error
	}{
		"can't get parent process name": {
			process: &mockProcess{
				parentNameErr: errors.New("parent name error"),
			},
			expErr: errors.New("parent name error"),
		},
		"parent process not allowed": {
			process:        defaultMockProcess(),
			allowedCallers: []string{"allowed1", "allowed2", "parent2"},
			expErr:         errors.New("may only be invoked by: allowed1, allowed2, parent2"),
		},
		"parent process not allowed - single": {
			process:        defaultMockProcess(),
			allowedCallers: []string{"allowed1"},
			expErr:         errors.New("may only be invoked by: allowed1"),
		},
		"not privileged": {
			process: &mockProcess{
				name:       "myprocess",
				parentName: "parent",
			},
			expErr:  pbin.PrivilegedHelperNotPrivileged("myprocess"),
			expResp: NewResponseWithError(pbin.PrivilegedHelperNotPrivileged("myprocess")),
		},
		"can't elevate privs": {
			process: &mockProcess{
				name:           "myprocess",
				parentName:     "parent",
				privileged:     true,
				elevatePrivErr: errors.New("elevate privs error"),
			},
			expErr:  errors.New("elevate privs error"),
			expResp: NewResponseWithError(errors.New("elevate privs error")),
		},
		"input read failed": {
			process:  defaultMockProcess(),
			inputReq: defaultReq,
			readErr:  errors.New("mock read failed"),
			expErr:   errors.New("mock read failed"),
		},
		"unhandled method": {
			process: defaultMockProcess(),
			inputReq: &pbin.Request{
				Method: "garbage",
			},
			expErr:  errors.New("unhandled method 'garbage'"),
			expResp: NewResponseWithError(errors.New("unhandled method 'garbage'")),
		},
		"response nil": {
			process:  defaultMockProcess(),
			inputReq: defaultReq,
			expResp:  NewResponseWithError(fmt.Errorf("handler for method '%s' returned nil", defaultReq.Method)),
			expErr:   fmt.Errorf("handler for method '%s' returned nil", defaultReq.Method),
		},
		"response has error": {
			process:    defaultMockProcess(),
			inputReq:   defaultReq,
			outputResp: NewResponseWithError(errors.New("response error")),
			expResp:    NewResponseWithError(errors.New("response error")),
			expErr:     errors.New("response error"),
		},
		"output write failed": {
			process:    defaultMockProcess(),
			inputReq:   defaultReq,
			outputResp: defaultResp,
			writeErr:   errors.New("mock write failed"),
			expErr:     errors.New("mock write failed"),
		},
		"success": {
			process:    defaultMockProcess(),
			inputReq:   defaultReq,
			outputResp: defaultResp,
			expResp:    defaultResp,
		},
		"success - parent explicitly allowed": {
			process:        defaultMockProcess(),
			allowedCallers: []string{"parent"},
			inputReq:       defaultReq,
			outputResp:     defaultResp,
			expResp:        defaultResp,
		},
	} {
		t.Run(name, func(t *testing.T) {
			rw := &mockReadWriter{
				readErr:  tc.readErr,
				writeErr: tc.writeErr,
			}
			rw.setRequestToRead(t, tc.inputReq)

			app := newTestApp(tc.process).
				WithAllowedCallers(tc.allowedCallers...).
				WithInput(rw).
				WithOutput(rw)
			app.log = nil // quiet the test from logging to stderr
			app.AddHandler(testMethod, &testHandler{outputResp: tc.outputResp})

			err := app.Run()

			common.CmpErr(t, tc.expErr, err)

			if diff := cmp.Diff(tc.expResp, rw.getWrittenResponse(t)); diff != "" {
				t.Errorf("bad response (-want, +got):\n%s\n", diff)
			}
		})
	}
}
