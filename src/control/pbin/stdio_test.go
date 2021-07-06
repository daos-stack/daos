//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
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
