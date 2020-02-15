//
// (C) Copyright 2019-2020 Intel Corporation.
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
package pbin_test

import (
	"os"
	"os/exec"
	"testing"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/pbin"
)

func echo() {
	sc := pbin.NewStdioConn("child", "parent", os.Stdin, os.Stdout)
	lis := pbin.NewStdioListener(sc)

	conn, err := lis.Accept()
	if err != nil {
		childErrExit(err)
	}
	defer lis.Close()

	buf := make([]byte, len(testMsg))
	var total int
	for {
		read, err := conn.Read(buf)
		if err != nil {
			childErrExit(err)
		}
		total += read
		if total == len(buf) {
			_, err = conn.Write(buf)
			if err != nil {
				childErrExit(err)
			}
			break
		}
	}
}

func TestPbin_StdioSimpleParentChild(t *testing.T) {
	errBuf := &logging.LogBuffer{}
	defer func() {
		if t.Failed() {
			t.Logf("child stderr:\n%s", errBuf.String())
		}
	}()

	childCmd := exec.Command(os.Args[0])
	childCmd.Stderr = errBuf
	childCmd.Env = []string{childModeEnvVar + "=" + childModeEcho}
	toChild, err := childCmd.StdinPipe()
	if err != nil {
		t.Fatal(err)
	}
	fromChild, err := childCmd.StdoutPipe()
	if err != nil {
		t.Fatal(err)
	}
	if err := childCmd.Start(); err != nil {
		t.Fatal(err)
	}

	conn := pbin.NewStdioConn("parent", "child", fromChild, toChild)
	_, err = conn.Write([]byte(testMsg))
	if err != nil {
		t.Fatal(err)
	}

	buf := make([]byte, len(testMsg))
	var total int
	for {
		read, err := conn.Read(buf)
		if err != nil {
			t.Fatal(err)
		}
		total += read

		if total == len(buf) {
			break
		}
	}
	got := string(buf)
	if got != testMsg {
		t.Fatalf("expected %q to be read back; got %q", testMsg, got)
	}

	if err := childCmd.Wait(); err != nil {
		t.Fatal(err)
	}
}
