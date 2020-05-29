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

// +build firmware

package main

import (
	"encoding/json"
	"errors"
	"fmt"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/pbin"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

func getNilRequestResp() *pbin.Response {
	return pbin.NewResponseWithError(errors.New("nil request"))
}

// scmHandler provides the ability to set up the scm.Provider for SCM method handlers.
type scmHandler struct {
	scmProvider *scm.Provider
}

func (h *scmHandler) setupProvider(log logging.Logger) {
	if h.scmProvider == nil {
		h.scmProvider = scm.DefaultProvider(log).WithForwardingDisabled()
	}
}

// scmQueryHandler handles a request to query the firmware information.
type scmQueryHandler struct {
	scmHandler
}

func (h *scmQueryHandler) Handle(log logging.Logger, req *pbin.Request) *pbin.Response {
	if req == nil {
		return getNilRequestResp()
	}

	var qReq scm.FirmwareQueryRequest
	if err := json.Unmarshal(req.Payload, &qReq); err != nil {
		return pbin.NewResponseWithError(err)
	}

	h.setupProvider(log)

	res, err := h.scmProvider.QueryFirmware(qReq)
	if err != nil {
		return pbin.NewResponseWithError(err)
	}

	return pbin.NewResponseWithPayload(res)
}

// scmUpdateHandler handles a request to update the firmware from a file.
type scmUpdateHandler struct {
	scmHandler
}

func (h *scmUpdateHandler) Handle(log logging.Logger, req *pbin.Request) *pbin.Response {
	if req == nil {
		return getNilRequestResp()
	}

	var uReq scm.FirmwareUpdateRequest
	if err := json.Unmarshal(req.Payload, &uReq); err != nil {
		return pbin.NewResponseWithError(err)
	}

	h.setupProvider(log)

	res, err := h.scmProvider.UpdateFirmware(uReq)
	if err != nil {
		return pbin.NewResponseWithError(err)
	}

	fmt.Printf("%+v\n", res)
	return pbin.NewResponseWithPayload(res)
}
