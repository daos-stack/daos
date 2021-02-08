//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
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

// scmMountUnmountHandler implements the ScmMount and ScmUnmount methods.
type scmMountUnmountHandler struct {
	scmHandler
}

func (h *scmMountUnmountHandler) Handle(log logging.Logger, req *pbin.Request) *pbin.Response {
	if req == nil {
		return getNilRequestResp()
	}

	var mReq scm.MountRequest
	if err := json.Unmarshal(req.Payload, &mReq); err != nil {
		return pbin.NewResponseWithError(err)
	}

	h.setupProvider(log)

	var mRes *scm.MountResponse
	var err error
	switch req.Method {
	case "ScmMount":
		mRes, err = h.scmProvider.Mount(mReq)
	case "ScmUnmount":
		mRes, err = h.scmProvider.Unmount(mReq)
	}
	if err != nil {
		return pbin.NewResponseWithError(err)
	}
	return pbin.NewResponseWithPayload(mRes)
}

// scmFormatCheckHandler implements the ScmFormat and ScmCheckFormat methods.
type scmFormatCheckHandler struct {
	scmHandler
}

func (h *scmFormatCheckHandler) Handle(log logging.Logger, req *pbin.Request) *pbin.Response {
	if req == nil {
		return getNilRequestResp()
	}

	var fReq scm.FormatRequest
	if err := json.Unmarshal(req.Payload, &fReq); err != nil {
		return pbin.NewResponseWithError(err)
	}

	h.setupProvider(log)

	var fRes *scm.FormatResponse
	var err error
	switch req.Method {
	case "ScmFormat":
		fRes, err = h.scmProvider.Format(fReq)
	case "ScmCheckFormat":
		fRes, err = h.scmProvider.CheckFormat(fReq)
	}
	if err != nil {
		return pbin.NewResponseWithError(err)
	}

	return pbin.NewResponseWithPayload(fRes)
}

// scmScanHandler implements the ScmScan method.
type scmScanHandler struct {
	scmHandler
}

func (h *scmScanHandler) Handle(log logging.Logger, req *pbin.Request) *pbin.Response {
	if req == nil {
		return getNilRequestResp()
	}

	var sReq scm.ScanRequest
	if err := json.Unmarshal(req.Payload, &sReq); err != nil {
		return pbin.NewResponseWithError(err)
	}

	h.setupProvider(log)

	sRes, err := h.scmProvider.Scan(sReq)
	if err != nil {
		return pbin.NewResponseWithError(err)
	}

	return pbin.NewResponseWithPayload(sRes)
}

// scmPrepHandler implements the ScmPrepare method.
type scmPrepHandler struct {
	scmHandler
}

func (h *scmPrepHandler) Handle(log logging.Logger, req *pbin.Request) *pbin.Response {
	if req == nil {
		return getNilRequestResp()
	}

	var pReq scm.PrepareRequest
	if err := json.Unmarshal(req.Payload, &pReq); err != nil {
		return pbin.NewResponseWithError(err)
	}

	h.setupProvider(log)

	pRes, err := h.scmProvider.Prepare(pReq)
	if err != nil {
		return pbin.NewResponseWithError(err)
	}

	return pbin.NewResponseWithPayload(pRes)
}

// bdevHandler provides the ability to set up the bdev.Provider for bdev methods.
type bdevHandler struct {
	bdevProvider *bdev.Provider
}

func (h *bdevHandler) setupProvider(log logging.Logger) {
	if h.bdevProvider == nil {
		h.bdevProvider = bdev.DefaultProvider(log).WithForwardingDisabled()
	}
}

// bdevScanHandler implements the BdevScan method.
type bdevScanHandler struct {
	bdevHandler
}

func (h *bdevScanHandler) Handle(log logging.Logger, req *pbin.Request) *pbin.Response {
	if req == nil {
		return getNilRequestResp()
	}

	var sReq bdev.ScanRequest
	if err := json.Unmarshal(req.Payload, &sReq); err != nil {
		return pbin.NewResponseWithError(err)
	}

	h.setupProvider(log)

	sRes, err := h.bdevProvider.Scan(sReq)
	if err != nil {
		return pbin.NewResponseWithError(err)
	}

	return pbin.NewResponseWithPayload(sRes)
}

// bdevPrepHandler implements the BdevPrepare method.
type bdevPrepHandler struct {
	bdevHandler
}

func (h *bdevPrepHandler) Handle(log logging.Logger, req *pbin.Request) *pbin.Response {
	if req == nil {
		return getNilRequestResp()
	}

	var pReq bdev.PrepareRequest
	if err := json.Unmarshal(req.Payload, &pReq); err != nil {
		return pbin.NewResponseWithError(err)
	}

	h.setupProvider(log)

	pRes, err := h.bdevProvider.Prepare(pReq)
	if err != nil {
		return pbin.NewResponseWithError(err)
	}

	return pbin.NewResponseWithPayload(pRes)
}

// bdevFormatHandler implements the BdevFormat method.
type bdevFormatHandler struct {
	bdevHandler
}

func (h *bdevFormatHandler) Handle(log logging.Logger, req *pbin.Request) *pbin.Response {
	if req == nil {
		return getNilRequestResp()
	}

	var fReq bdev.FormatRequest
	if err := json.Unmarshal(req.Payload, &fReq); err != nil {
		return pbin.NewResponseWithError(err)
	}

	h.setupProvider(log)

	fRes, err := h.bdevProvider.Format(fReq)
	if err != nil {
		return pbin.NewResponseWithError(err)
	}

	return pbin.NewResponseWithPayload(fRes)
}
