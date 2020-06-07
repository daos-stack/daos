//
// (C) Copyright 2020 Intel Corporation.
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

package pbin

import (
	"encoding/json"

	"github.com/pkg/errors"

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
