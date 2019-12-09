//
// (C) Copyright 2018-2019 Intel Corporation.
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

package drpc

// Failure represents a dRPC protocol failure.
type Failure struct {
	statusCode Status
	message    string
}

// Error provides a descriptive string associated with the failure.
func (e Failure) Error() string {
	return e.message
}

// GetStatus provides a dRPC status code associated with the failure.
func (e Failure) GetStatus() Status {
	return e.statusCode
}

// NewFailure returns a Failure with the given status and a corresponding message.
func NewFailure(status Status) Failure {
	message := statusToString(status)
	return Failure{
		message:    message,
		statusCode: status,
	}
}

func statusToString(status Status) string {
	switch status {
	case Status_UNKNOWN_MODULE:
		return "unrecognized module ID"
	case Status_UNKNOWN_METHOD:
		return "unrecognized method ID"
	case Status_INVALID_MESSAGE:
		return "invalid incoming message"
	case Status_INVALID_PAYLOAD:
		return "invalid method-specific payload"
	case Status_FAILED_MARSHAL:
		return "failed to marshal response payload"
	case Status_SUCCESS:
		fallthrough
	case Status_SUBMITTED:
		return ""
	}

	return "an unknown error occurred"
}

// UnknownModuleFailure creates a Failure for unknown dRPC module.
func UnknownModuleFailure() Failure {
	return NewFailure(Status_UNKNOWN_MODULE)
}

// UnknownMethodFailure creates a Failure for unknown dRPC method.
func UnknownMethodFailure() Failure {
	return NewFailure(Status_UNKNOWN_METHOD)
}

// InvalidMessageFailure creates a Failure for an invalid call message.
func InvalidMessageFailure() Failure {
	return NewFailure(Status_INVALID_MESSAGE)
}

// InvalidPayloadFailure creates a Failure for an invalid call payload.
func InvalidPayloadFailure() Failure {
	return NewFailure(Status_INVALID_PAYLOAD)
}

// MarshalingFailure creates a Failure for a failed attempt at marshaling a response.
func MarshalingFailure() Failure {
	return NewFailure(Status_FAILED_MARSHAL)
}

// NewFailureWithMessage returns a generic failure with a custom message
func NewFailureWithMessage(message string) Failure {
	return Failure{
		message:    message,
		statusCode: Status_FAILURE,
	}
}

// ErrorToStatus translates an error to a dRPC Status.
// In practice it checks to see if it was a dRPC Failure error, and uses the Status if so.
// Otherwise it is assumed to be a generic failure.
func ErrorToStatus(err error) Status {
	if err == nil {
		return Status_SUCCESS
	}
	if failure, ok := err.(Failure); ok {
		return failure.GetStatus()
	}
	return Status_FAILURE
}
