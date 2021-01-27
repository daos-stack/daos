//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pbin

import (
	"encoding/json"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/logging"
)

// RequestHandler is an interface that handles a pbin.Request.
type RequestHandler interface {
	Handle(logging.Logger, *Request) *Response
}

// NewResponseWithError creates a new pbin.Response indicating a failure.
func NewResponseWithError(err error) *Response {
	f, ok := errors.Cause(err).(*fault.Fault)
	if !ok {
		f = PrivilegedHelperRequestFailed(err.Error())
	}
	return &Response{
		Error:   f,
		Payload: json.RawMessage([]byte("null")),
	}
}

// NewResponseWithPayload creates a new pbin.Response with a payload structure
// marshalled into JSON.
func NewResponseWithPayload(payloadSrc interface{}) *Response {
	payload, err := json.Marshal(payloadSrc)
	if err != nil {
		return NewResponseWithError(err)
	}

	return &Response{
		Payload: payload,
	}
}

// PingMethod is the string naming the ping method, which is universal to all
// privileged Apps.
const PingMethod string = "Ping"

// pingHandler is the type that implements the Ping method.
type pingHandler struct {
	appName string
}

// Handle responds to a Ping request.
func (h *pingHandler) Handle(_ logging.Logger, req *Request) *Response {
	if req == nil {
		return NewResponseWithError(errors.New("nil request"))
	}
	return NewResponseWithPayload(&PingResp{
		Version: build.DaosVersion,
		AppName: h.appName,
	})
}

// newPingHandler creates a new PingHandler for a given application process.
func newPingHandler(process processProvider) *pingHandler {
	return &pingHandler{
		appName: process.CurrentProcessName(),
	}
}
