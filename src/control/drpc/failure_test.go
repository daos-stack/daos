//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package drpc

import (
	"testing"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
)

func TestNewFailure(t *testing.T) {
	for name, tt := range map[string]struct {
		expectedMessage string
		status          Status
	}{
		"generic failure": {
			expectedMessage: "an unknown error occurred",
			status:          Status_FAILURE,
		},
		"unknown module": {
			expectedMessage: "unrecognized module ID",
			status:          Status_UNKNOWN_MODULE,
		},
		"unknown method": {
			expectedMessage: "unrecognized method ID",
			status:          Status_UNKNOWN_METHOD,
		},
		"failed to unmarshal call": {
			expectedMessage: "failed to unmarshal incoming call",
			status:          Status_FAILED_UNMARSHAL_CALL,
		},
		"failed to unmarshal payload": {
			expectedMessage: "failed to unmarshal method-specific payload",
			status:          Status_FAILED_UNMARSHAL_PAYLOAD,
		},
		"failed to marshal": {
			expectedMessage: "failed to marshal response payload",
			status:          Status_FAILED_MARSHAL,
		},
		"success - no error": {
			expectedMessage: "", // no error
			status:          Status_SUCCESS,
		},
		"submitted - no error": {
			expectedMessage: "", // no error
			status:          Status_SUBMITTED,
		},
		"unrecognized status": {
			expectedMessage: "an unknown error occurred",
			status:          -1,
		},
	} {
		t.Run(name, func(t *testing.T) {
			f := NewFailure(tt.status)

			common.AssertEqual(t, f.GetStatus(), tt.status, name)
			common.AssertEqual(t, f.Error(), tt.expectedMessage, name)
		})
	}
}

func TestNewFailureWithMessage(t *testing.T) {
	expectedMessage := "a custom message"
	f := NewFailureWithMessage(expectedMessage)

	common.AssertEqual(t, f.Error(), expectedMessage, "didn't get the custom message")
	common.AssertEqual(t, f.GetStatus(), Status_FAILURE, "expected a generic failure")
}

func TestErrorToStatus(t *testing.T) {
	for name, tt := range map[string]struct {
		err            error
		expectedStatus Status
	}{
		"nil": {
			err:            nil,
			expectedStatus: Status_SUCCESS,
		},
		"generic error": {
			err:            errors.New("a surprising error"),
			expectedStatus: Status_FAILURE,
		},
		"generic failure": {
			err:            NewFailure(Status_FAILURE),
			expectedStatus: Status_FAILURE,
		},
		"unknown module failure": {
			err:            NewFailure(Status_UNKNOWN_MODULE),
			expectedStatus: Status_UNKNOWN_MODULE,
		},
		"unknown method failure": {
			err:            NewFailure(Status_UNKNOWN_METHOD),
			expectedStatus: Status_UNKNOWN_METHOD,
		},
		"failed to unmarshal call": {
			err:            NewFailure(Status_FAILED_UNMARSHAL_CALL),
			expectedStatus: Status_FAILED_UNMARSHAL_CALL,
		},
		"failed to unmarshal payload": {
			err:            NewFailure(Status_FAILED_UNMARSHAL_PAYLOAD),
			expectedStatus: Status_FAILED_UNMARSHAL_PAYLOAD,
		},
		"failed to marshal": {
			err:            NewFailure(Status_FAILED_MARSHAL),
			expectedStatus: Status_FAILED_MARSHAL,
		},
		"success": {
			err:            NewFailure(Status_SUCCESS),
			expectedStatus: Status_SUCCESS,
		},
		"submitted": {
			err:            NewFailure(Status_SUBMITTED),
			expectedStatus: Status_SUBMITTED,
		},
	} {
		t.Run(name, func(t *testing.T) {
			status := ErrorToStatus(tt.err)

			common.AssertEqual(t, status, tt.expectedStatus, name)
		})
	}
}

func TestFailureCreationMethods(t *testing.T) {
	for name, tt := range map[string]struct {
		function       func() Failure
		expectedStatus Status
	}{
		"UnknownModuleFailure": {
			function:       UnknownModuleFailure,
			expectedStatus: Status_UNKNOWN_MODULE,
		},
		"UnknownMethodFailure": {
			function:       UnknownMethodFailure,
			expectedStatus: Status_UNKNOWN_METHOD,
		},
		"UnmarshalingCallFailure": {
			function:       UnmarshalingCallFailure,
			expectedStatus: Status_FAILED_UNMARSHAL_CALL,
		},
		"UnmarshalingPayloadFailure": {
			function:       UnmarshalingPayloadFailure,
			expectedStatus: Status_FAILED_UNMARSHAL_PAYLOAD,
		},
		"MarshalingFailure": {
			function:       MarshalingFailure,
			expectedStatus: Status_FAILED_MARSHAL,
		},
	} {
		t.Run(name, func(t *testing.T) {
			failure := tt.function()

			common.AssertEqual(t, failure.GetStatus(), tt.expectedStatus, name)
		})
	}
}
