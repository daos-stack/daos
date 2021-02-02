//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"path/filepath"
	"strings"
	"testing"

	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto/shared"
	sharedpb "github.com/daos-stack/daos/src/control/common/proto/shared"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/logging"
)

func getTestNotifyReadyReqBytes(t *testing.T, sockPath string, idx uint32) []byte {
	req := getTestNotifyReadyReq(t, sockPath, idx)
	reqBytes, err := proto.Marshal(req)

	if err != nil {
		t.Fatalf("Couldn't create fake request: %v", err)
	}

	return reqBytes
}

func isIosrvReady(instance *IOServerInstance) bool {
	select {
	case <-instance.awaitDrpcReady():
		return true
	default:
		return false
	}
}

func addIOServerInstances(mod *srvModule, numInstances int, log logging.Logger) {
	for i := 0; i < numInstances; i++ {
		mod.iosrvs = append(mod.iosrvs, getTestIOServerInstance(log))
	}
}

func TestSrvModule_HandleNotifyReady_Invalid(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	expectedErr := drpc.UnmarshalingPayloadFailure()
	mod := &srvModule{}
	addIOServerInstances(mod, 1, log)

	// Some arbitrary bytes, shouldn't translate to a request
	badBytes := make([]byte, 16)
	for i := range badBytes {
		badBytes[i] = byte(i)
	}

	err := mod.handleNotifyReady(badBytes)

	if err == nil {
		t.Fatalf("Expected error, got nil")
	}

	if !strings.Contains(err.Error(), expectedErr.Error()) {
		t.Errorf("Expected error to contain %q, got %q",
			expectedErr, err.Error())
	}
}

func TestSrvModule_HandleNotifyReady_BadSockPath(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	expectedErr := "check NotifyReady request socket path"
	mod := &srvModule{}
	addIOServerInstances(mod, 1, log)

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
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	mod := &srvModule{}
	addIOServerInstances(mod, 1, log)

	// Needs to be a real socket at the path
	tmpDir, tmpCleanup := common.CreateTestDir(t)
	defer tmpCleanup()
	sockPath := filepath.Join(tmpDir, "mgmt_drpc_test.sock")

	_, cleanup := common.CreateTestSocket(t, sockPath)
	defer cleanup()

	reqBytes := getTestNotifyReadyReqBytes(t, sockPath, 0)

	err := mod.handleNotifyReady(reqBytes)

	if err != nil {
		t.Fatalf("Expected no error, got %q", err.Error())
	}

	waitForIosrvReady(t, mod.iosrvs[0])
}

func TestSrvModule_HandleNotifyReady_Success_Multi(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	mod := &srvModule{}
	numInstances := 5
	idx := uint32(numInstances - 1)

	addIOServerInstances(mod, numInstances, log)

	// Needs to be a real socket at the path
	tmpDir, tmpCleanup := common.CreateTestDir(t)
	defer tmpCleanup()
	sockPath := filepath.Join(tmpDir, "mgmt_drpc_test.sock")

	_, cleanup := common.CreateTestSocket(t, sockPath)
	defer cleanup()

	reqBytes := getTestNotifyReadyReqBytes(t, sockPath, idx)

	err := mod.handleNotifyReady(reqBytes)

	if err != nil {
		t.Fatalf("Expected no error, got %q", err.Error())
	}

	// IO server at idx should be marked ready
	waitForIosrvReady(t, mod.iosrvs[idx])
	// None of the other IO servers should have gotten the message
	for i, s := range mod.iosrvs {
		if uint32(i) != idx && isIosrvReady(s) {
			t.Errorf("Expected IOsrv at idx %v to be NOT ready", i)
		}
	}
}

func TestSrvModule_HandleNotifyReady_IdxOutOfRange(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	expectedError := "out of range"
	mod := &srvModule{}
	numInstances := 5

	addIOServerInstances(mod, numInstances, log)

	// Needs to be a real socket at the path
	tmpDir, tmpCleanup := common.CreateTestDir(t)
	defer tmpCleanup()
	sockPath := filepath.Join(tmpDir, "mgmt_drpc_test.sock")

	_, cleanup := common.CreateTestSocket(t, sockPath)
	defer cleanup()

	reqBytes := getTestNotifyReadyReqBytes(t, sockPath,
		uint32(numInstances))

	err := mod.handleNotifyReady(reqBytes)

	if err == nil {
		t.Fatal("Expected error, got nil")
	}

	if !strings.Contains(err.Error(), expectedError) {
		t.Errorf("Expected error to contain %q, got %q",
			expectedError, err.Error())
	}
}

func getTestBioErrorReqBytes(t *testing.T, sockPath string, idx uint32, tgt int32, unmap bool, read bool, write bool) []byte {
	req := getTestBioErrorReq(t, sockPath, idx, tgt, unmap, read, write)
	reqBytes, err := proto.Marshal(req)

	if err != nil {
		t.Fatalf("Couldn't create fake request: %v", err)
	}

	return reqBytes
}

func TestSrvModule_HandleBioError_Invalid(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	expectedErr := errors.New("unmarshal BioError request")
	mod := &srvModule{}
	addIOServerInstances(mod, 1, log)

	// Some arbitrary bytes, shouldn't translate to a request
	badBytes := make([]byte, 16)
	for i := range badBytes {
		badBytes[i] = byte(i)
	}

	err := mod.handleBioErr(badBytes)

	if err == nil {
		t.Fatalf("Expected error, got nil")
	}

	common.CmpErr(t, expectedErr, err)
}

func TestSrvModule_HandleBioError_BadSockPath(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	expectedErr := errors.New("check BioErr request socket path")
	mod := &srvModule{}
	addIOServerInstances(mod, 1, log)

	reqBytes := getTestBioErrorReqBytes(t, "/some/bad/path", 0, 0, false,
		false, true)

	err := mod.handleBioErr(reqBytes)

	if err == nil {
		t.Fatalf("Expected error, got nil")
	}

	common.CmpErr(t, expectedErr, err)
}

func TestSrvModule_HandleBioError_Success_Single(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	mod := &srvModule{}
	addIOServerInstances(mod, 1, log)

	// Needs to be a real socket at the path
	tmpDir, tmpCleanup := common.CreateTestDir(t)
	defer tmpCleanup()
	sockPath := filepath.Join(tmpDir, "mgmt_drpc_test.sock")

	_, cleanup := common.CreateTestSocket(t, sockPath)
	defer cleanup()

	reqBytes := getTestBioErrorReqBytes(t, sockPath, 0, 0, false, false,
		true)

	err := mod.handleBioErr(reqBytes)

	if err != nil {
		t.Fatalf("Expected no error, got %q", err.Error())
	}
}

func TestSrvModule_HandleBioError_Success_Multi(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	mod := &srvModule{}
	numInstances := 5
	idx := uint32(numInstances - 1)

	addIOServerInstances(mod, numInstances, log)

	// Needs to be a real socket at the path
	tmpDir, tmpCleanup := common.CreateTestDir(t)
	defer tmpCleanup()
	sockPath := filepath.Join(tmpDir, "mgmt_drpc_test.sock")

	_, cleanup := common.CreateTestSocket(t, sockPath)
	defer cleanup()

	reqBytes := getTestBioErrorReqBytes(t, sockPath, idx, 0, false, false,
		true)

	err := mod.handleBioErr(reqBytes)

	if err != nil {
		t.Fatalf("Expected no error, got %q", err.Error())
	}
}

func TestSrvModule_HandleBioErr_IdxOutOfRange(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	expectedError := errors.New("out of range")
	mod := &srvModule{}
	numInstances := 5

	addIOServerInstances(mod, numInstances, log)

	// Needs to be a real socket at the path
	tmpDir, tmpCleanup := common.CreateTestDir(t)
	defer tmpCleanup()
	sockPath := filepath.Join(tmpDir, "mgmt_drpc_test.sock")

	_, cleanup := common.CreateTestSocket(t, sockPath)
	defer cleanup()

	reqBytes := getTestBioErrorReqBytes(t, sockPath, uint32(numInstances),
		0, false, false, true)

	err := mod.handleBioErr(reqBytes)

	if err == nil {
		t.Fatal("Expected error, got nil")
	}

	common.CmpErr(t, expectedError, err)
}

func getTestClusterEventReqBytes(t *testing.T, event *sharedpb.RASEvent, seq uint64) []byte {
	req := &shared.ClusterEventReq{Event: event, Sequence: seq}
	reqBytes, err := proto.Marshal(req)

	if err != nil {
		t.Fatalf("Couldn't create fake request: %v", err)
	}

	return reqBytes
}

func TestSrvModule_HandleClusterEvent_Invalid(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	expectedErr := errors.New("unmarshal method-specific payload")
	mod := &srvModule{}
	addIOServerInstances(mod, 1, log)

	// Some arbitrary bytes, shouldn't translate to a request
	badBytes := make([]byte, 16)
	for i := range badBytes {
		badBytes[i] = byte(i)
	}

	_, err := mod.handleClusterEvent(badBytes)

	if err == nil {
		t.Fatalf("Expected error, got nil")
	}

	common.CmpErr(t, expectedErr, err)
}
