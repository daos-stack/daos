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
	"github.com/golang/protobuf/proto"
	"strings"
	"testing"
)

func getTestNotifyReadyReqBytes(t *testing.T, sockPath string, idx uint32) []byte {
	req := getTestNotifyReadyReq(t, sockPath, idx)
	reqBytes, err := proto.Marshal(req)

	if err != nil {
		t.Fatalf("Couldn't create fake request: %v", err)
	}

	return reqBytes
}

func TestSrvModule_HandleNotifyReady_Invalid(t *testing.T) {
	expectedErr := "unmarshal NotifyReady request"
	mod := &srvModule{}

	// Some arbitrary bytes, shouldn't translate to a request
	badBytes := make([]byte, 16)
	for i := range badBytes {
		badBytes[i] = byte(i)
	}

	err := mod.handleNotifyReady(badBytes)

	if err == nil {
		t.Fatalf("Expected error, got nil")
	}

	if !strings.Contains(err.Error(), expectedErr) {
		t.Errorf("Expected error to contain %q, got %q",
			expectedErr, err.Error())
	}
}

func TestSrvModule_HandleNotifyReady_BadSockPath(t *testing.T) {
	expectedErr := "check NotifyReady request socket path"
	mod := &srvModule{}

	reqBytes := getTestNotifyReadyReqBytes(t, "/some/bad/path", 0)

	err := mod.handleNotifyReady(reqBytes)

	if err == nil {
		t.Fatalf("Expected error, got nil")
	}

	if !strings.Contains(err.Error(), expectedErr) {
		t.Errorf("Expected error to contain %q, got %q",
			expectedErr, err.Error())
	}
}

func TestSrvModule_HandleNotifyReady_Success_Single(t *testing.T) {
	mod := &srvModule{}
	mod.iosrv = append(mod.iosrv, getTestIOServerInstance())

	// Needs to be a real socket at the path
	sockPath := "/tmp/mgmt_drpc_test.sock"
	sock := createTestSocket(t, sockPath)
	defer cleanupTestSocket(sockPath, sock)

	reqBytes := getTestNotifyReadyReqBytes(t, sockPath, 0)

	err := mod.handleNotifyReady(reqBytes)

	if err != nil {
		t.Fatalf("Expected no error, got %q", err.Error())
	}

	waitForIosrvReady(t, mod.iosrv[0])
}

func TestSrvModule_HandleNotifyReady_Success_Multi(t *testing.T) {
	mod := &srvModule{}
	numInstances := 5
	idx := uint32(numInstances - 1)

	for i := 0; i < numInstances; i++ {
		mod.iosrv = append(mod.iosrv, getTestIOServerInstance())
	}

	// Needs to be a real socket at the path
	sockPath := "/tmp/mgmt_drpc_test.sock"
	sock := createTestSocket(t, sockPath)
	defer cleanupTestSocket(sockPath, sock)

	reqBytes := getTestNotifyReadyReqBytes(t, sockPath, idx)

	err := mod.handleNotifyReady(reqBytes)

	if err != nil {
		t.Fatalf("Expected no error, got %q", err.Error())
	}

	// IO server at idx should be marked ready
	waitForIosrvReady(t, mod.iosrv[idx])
	// None of the other IO servers should have gotten the message
	for i, s := range mod.iosrv {
		if uint32(i) != idx && isIosrvReady(s) {
			t.Errorf("Expected IOsrv at idx %v to be NOT ready", i)
		}
	}
}
