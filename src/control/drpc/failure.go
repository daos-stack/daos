//
// (C) Copyright 2018-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

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
	case Status_FAILED_UNMARSHAL_CALL:
		return "failed to unmarshal incoming call"
	case Status_FAILED_UNMARSHAL_PAYLOAD:
		return "failed to unmarshal method-specific payload"
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

// UnmarshalingCallFailure creates a Failure for a failed attempt to unmarshal an incoming call.
func UnmarshalingCallFailure() Failure {
	return NewFailure(Status_FAILED_UNMARSHAL_CALL)
}

// UnmarshalingPayloadFailure creates a Failure for a failed attempt to unmarshal a call payload.
func UnmarshalingPayloadFailure() Failure {
	return NewFailure(Status_FAILED_UNMARSHAL_PAYLOAD)
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
