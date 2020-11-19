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

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/pbin"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
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

// Handle queries the SCM firmware information and returns the data in a pbin.Response.
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

// scmUpdateHandler handles a request to update the SCM module firmware from a file.
type scmUpdateHandler struct {
	scmHandler
}

// Handle updates the SCM firmware and returns the result in a pbin.Response.
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

	return pbin.NewResponseWithPayload(res)
}

// bdevHandler provides the ability to set up the bdev.Provider for NVMe method handlers.
type bdevHandler struct {
	bdevProvider *bdev.Provider
}

func (h *bdevHandler) setupProvider(log logging.Logger) {
	if h.bdevProvider == nil {
		h.bdevProvider = bdev.DefaultProvider(log).WithForwardingDisabled()
	}
}

// nvmeUpdateHandler handles a request to update the NVMe device firmware from a file.
type nvmeUpdateHandler struct {
	bdevHandler
}

// Handle updates the NVMe device firmware and returns the result as a pbin.Response.
func (h *nvmeUpdateHandler) Handle(log logging.Logger, req *pbin.Request) *pbin.Response {
	if req == nil {
		return getNilRequestResp()
	}

	var uReq bdev.FirmwareUpdateRequest
	if err := json.Unmarshal(req.Payload, &uReq); err != nil {
		return pbin.NewResponseWithError(err)
	}

	h.setupProvider(log)

	res, _ := h.bdevProvider.UpdateFirmware(uReq)

	return pbin.NewResponseWithPayload(res)
}
