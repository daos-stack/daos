//
// (C) Copyright 2019-2022 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package drpc

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
)

const defaultTestModID int32 = 1

func TestNewModuleService(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	service := NewModuleService(log)

	if service == nil {
		t.Fatal("service was nil")
	}

	test.AssertEqual(t, len(service.modules), 0, "expected empty module list")
}

func TestService_RegisterModule_Single_Success(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	service := NewModuleService(log)
	expectedID := defaultTestModID
	testMod := newTestModule(expectedID)

	service.RegisterModule(testMod)
	mod, ok := service.GetModule(expectedID)

	if !ok {
		t.Fatalf("module wasn't found under ID %d", expectedID)
	}

	if diff := cmp.Diff(testMod, mod); diff != "" {
		t.Fatalf("(-want, +got)\n%s", diff)
	}
}

func TestService_RegisterModule_Multiple_Success(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	service := NewModuleService(log)
	expectedIDs := []int32{-1, 7, 255, defaultTestModID}
	testMods := make([]*mockModule, 0, len(expectedIDs))

	for _, id := range expectedIDs {
		mod := newTestModule(id)
		testMods = append(testMods, mod)

		service.RegisterModule(mod)
	}

	for i, id := range expectedIDs {
		mod, ok := service.GetModule(id)

		if !ok {
			t.Fatalf("registered module %d wasn't found", id)
		}

		if diff := cmp.Diff(testMods[i], mod); diff != "" {
			t.Fatalf("(-want, +got)\n%s", diff)
		}
	}
}

func TestService_RegisterModule_DuplicateID(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	service := NewModuleService(log)
	testMod := newTestModule(defaultTestModID)
	dupMod := newTestModule(testMod.IDValue)

	service.RegisterModule(testMod)

	defer func() {
		if r := recover(); r == nil {
			t.Fatal("expected duplicate registration to panic")
		}
	}()
	service.RegisterModule(dupMod)
}

func TestService_GetModule_NotFound(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	service := NewModuleService(log)
	service.RegisterModule(newTestModule(defaultTestModID))

	_, ok := service.GetModule(defaultTestModID + 1)

	if ok {
		t.Fatal("module wasn't expected to match ID")
	}
}

func getGarbageBytes() []byte {
	badBytes := make([]byte, 250)
	for i := range badBytes {
		badBytes[i] = byte(i)
	}

	return badBytes
}

func getCallBytes(t *testing.T, sequence int64, moduleID int32, method Method) []byte {
	t.Helper()

	call := &Call{
		Sequence: sequence,
		Module:   moduleID,
		Method:   method.ID(),
	}

	callBytes, err := proto.Marshal(call)
	if err != nil {
		t.Fatalf("Got error marshalling test call: %v", err)
	}

	return callBytes
}

func getResponse(sequence int64, status Status, body []byte) *Response {
	return &Response{
		Sequence: sequence,
		Status:   status,
		Body:     body,
	}
}

func TestService_ProcessMessage(t *testing.T) {
	const testSequenceNum int64 = 13

	testMethod := func(module, method int32) Method {
		return &mockMethod{
			id:     method,
			module: module,
		}
	}

	for name, tc := range map[string]struct {
		callBytes      []byte
		handleCallErr  error
		handleCallResp []byte
		expectedResp   *Response
	}{
		"garbage input bytes": {
			callBytes:    getGarbageBytes(),
			expectedResp: getResponse(-1, Status_FAILED_UNMARSHAL_CALL, nil),
		},
		"module doesn't exist": {
			callBytes:    getCallBytes(t, testSequenceNum, 256, testMethod(256, 1)),
			expectedResp: getResponse(testSequenceNum, Status_UNKNOWN_MODULE, nil),
		},
		"HandleCall fails with regular error": {
			callBytes:     getCallBytes(t, testSequenceNum, int32(defaultTestModID), testMethod(int32(defaultTestModID), 1)),
			handleCallErr: errors.New("HandleCall error"),
			expectedResp:  getResponse(testSequenceNum, Status_FAILURE, nil),
		},
		"HandleCall fails with drpc.Failure": {
			callBytes: getCallBytes(t, testSequenceNum, int32(defaultTestModID),
				testMethod(int32(defaultTestModID), 1)),
			handleCallErr: NewFailure(Status_FAILED_UNMARSHAL_PAYLOAD),
			expectedResp:  getResponse(testSequenceNum, Status_FAILED_UNMARSHAL_PAYLOAD, nil),
		},
		"HandleCall succeeds": {
			callBytes: getCallBytes(t, testSequenceNum, int32(defaultTestModID),
				testMethod(int32(defaultTestModID), 1)),
			handleCallResp: []byte("succeeded"),
			expectedResp:   getResponse(testSequenceNum, Status_SUCCESS, []byte("succeeded")),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mockMod := newTestModule(defaultTestModID)
			mockMod.HandleCallErr = tc.handleCallErr
			mockMod.HandleCallResponse = tc.handleCallResp

			service := NewModuleService(log)
			service.RegisterModule(mockMod)

			respBytes, err := service.ProcessMessage(test.Context(t), &Session{}, tc.callBytes)

			if err != nil {
				t.Fatalf("expected nil error, got: %v", err)
			}

			resp := &Response{}
			err = proto.Unmarshal(respBytes, resp)
			if err != nil {
				t.Fatalf("couldn't unmarshal response bytes: %v", err)
			}

			cmpOpts := test.DefaultCmpOpts()
			if diff := cmp.Diff(tc.expectedResp, resp, cmpOpts...); diff != "" {
				t.Fatalf("(-want, +got)\n%s", diff)
			}
		})
	}
}

func TestDrpc_Marshal_Success(t *testing.T) {
	message := &Call{Module: 1, Method: 2, Sequence: 3}

	result, err := Marshal(message)

	if err != nil {
		t.Errorf("Expected no error, got: %+v", err)
	}

	// Unmarshaled result should match original
	pMsg := &Call{}
	_ = proto.Unmarshal(result, pMsg)

	cmpOpts := test.DefaultCmpOpts()
	if diff := cmp.Diff(message, pMsg, cmpOpts...); diff != "" {
		t.Fatalf("(-want, +got)\n%s", diff)
	}
}
