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

package pbin

import (
	"bytes"
	"encoding/json"
	"testing"
)

// mockProcess is a mock process provider.
type mockProcess struct {
	name           string
	parentName     string
	parentNameErr  error
	privileged     bool
	elevatePrivErr error
}

func (m *mockProcess) CurrentProcessName() string {
	return m.name
}

func (m *mockProcess) ParentProcessName() (string, error) {
	return m.parentName, m.parentNameErr
}

func (m *mockProcess) IsPrivileged() bool {
	return m.privileged
}

func (m *mockProcess) ElevatePrivileges() error {
	return m.elevatePrivErr
}

func defaultMockProcess() *mockProcess {
	return &mockProcess{
		name:       "process",
		parentName: "parent",
		privileged: true,
	}
}

// mockReadWriter is a mock of the io.ReadWriteCloser interface that provides
// convenient access to pbin Request/Response data structures.
type mockReadWriter struct {
	toRead   bytes.Buffer
	readErr  error
	written  bytes.Buffer
	writeErr error
}

func (r *mockReadWriter) setRequestToRead(t *testing.T, req *Request) {
	data, err := json.Marshal(req)
	if err != nil {
		t.Fatalf("failed to marshal req: %v", err)
	}

	r.toRead.Write(data)
}

func (r *mockReadWriter) Read(p []byte) (n int, err error) {
	if r.readErr != nil {
		return 0, r.readErr
	}
	return r.toRead.Read(p)
}

func (r *mockReadWriter) getWrittenResponse(t *testing.T) *Response {
	if r.written.Len() == 0 {
		return nil
	}
	buf := make([]byte, r.written.Len())
	_, err := r.written.Read(buf)
	if err != nil {
		t.Fatalf("couldn't read written response: %v", err)
	}
	var res Response
	if err := json.Unmarshal(buf, &res); err != nil {
		t.Fatalf("failed to unmarshal response: %v", err)
	}

	return &res
}

func (r *mockReadWriter) Write(p []byte) (n int, err error) {
	if r.writeErr != nil {
		return 0, r.writeErr
	}
	return r.written.Write(p)
}

func (r *mockReadWriter) Close() error {
	return nil
}

// newTestApp creates an instance of the app with a mock process provider.
func newTestApp(process *mockProcess) *App {
	app := NewApp()
	app.process = process
	app.setupPing() // re-setup the ping handler with the mock process
	return app
}
