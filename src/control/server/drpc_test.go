//
// (C) Copyright 2019 Intel Corporation.
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

package server

import (
	"io/ioutil"
	"net"
	"os"
	"path/filepath"
	"syscall"
	"testing"
)

func createTestDir(t *testing.T) string {
	tmpDir, err := ioutil.TempDir("", t.Name())
	if err != nil {
		t.Fatalf("Couldn't create temporary directory: %v", err)
	}

	return tmpDir
}

func createTestSocket(t *testing.T, sockPath string) *net.UnixListener {
	addr := &net.UnixAddr{Name: sockPath, Net: "unixpacket"}
	sock, err := net.ListenUnix("unixpacket", addr)
	if err != nil {
		t.Fatalf("Couldn't set up test socket: %v", err)
	}

	err = os.Chmod(sockPath, 0777)
	if err != nil {
		cleanupTestSocket(sockPath, sock)
		t.Fatalf("Unable to set permissions on test socket: %v", err)
	}

	return sock
}

func cleanupTestSocket(path string, sock *net.UnixListener) {
	sock.Close()
	syscall.Unlink(path)
}

func TestCheckDrpcClientSocketPath_Empty(t *testing.T) {
	err := checkDrpcClientSocketPath("")

	if err == nil {
		t.Fatal("Expected an error, got nil")
	}
}

func TestCheckDrpcClientSocketPath_BadPath(t *testing.T) {
	err := checkDrpcClientSocketPath("/not/a/real/path")

	if err == nil {
		t.Fatal("Expected an error, got nil")
	}
}

func TestCheckDrpcClientSocketPath_DirNotSocket(t *testing.T) {
	tmpDir := createTestDir(t)
	defer os.Remove(tmpDir)

	path := filepath.Join(tmpDir, "drpc_test.sock")
	err := os.Mkdir(path, 0755)
	if err != nil {
		t.Fatalf("Failed to create directory: %v", err)
	}
	defer os.Remove(path)

	err = checkDrpcClientSocketPath(path)

	if err == nil {
		t.Fatal("Expected an error, got nil")
	}
}

func TestCheckDrpcClientSocketPath_FileNotSocket(t *testing.T) {
	tmpDir := createTestDir(t)
	defer os.Remove(tmpDir)

	path := filepath.Join(tmpDir, "drpc_test.sock")
	f, err := os.Create(path)
	if err != nil {
		t.Fatalf("Failed to create temp file: %v", err)
	}
	f.Close()
	defer os.Remove(path)

	err = checkDrpcClientSocketPath(path)

	if err == nil {
		t.Fatal("Expected an error, got nil")
	}
}

func TestCheckDrpcClientSocketPath_Success(t *testing.T) {
	tmpDir := createTestDir(t)
	defer os.Remove(tmpDir)

	path := filepath.Join(tmpDir, "drpc_test.sock")
	sock := createTestSocket(t, path)
	defer cleanupTestSocket(path, sock)

	err := checkDrpcClientSocketPath(path)

	if err != nil {
		t.Fatalf("Expected no error, got error: %v", err)
	}
}
