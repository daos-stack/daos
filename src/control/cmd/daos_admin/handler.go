//
// (C) Copyright 2019-2020 Intel Corporation.
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
package main

import (
	"encoding/json"
	"io"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/pbin"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

func sendFailure(err error, res *pbin.Response, dest io.Writer) error {
	f, ok := errors.Cause(err).(*fault.Fault)
	if !ok {
		f = pbin.PrivilegedHelperRequestFailed(err.Error())
	}
	res.Error = f
	data, err := json.Marshal(res)
	if err != nil {
		return err
	}

	_, err = dest.Write(data)
	return err
}

func sendSuccess(payloadSrc interface{}, res *pbin.Response, dest io.Writer) error {
	payload, err := json.Marshal(payloadSrc)
	if err != nil {
		return sendFailure(err, res, dest)
	}

	res.Payload = payload
	data, err := json.Marshal(res)
	if err != nil {
		return err
	}

	_, err = dest.Write(data)
	return err
}

func readRequest(rdr io.Reader) (*pbin.Request, error) {
	buf, err := pbin.ReadMessage(rdr)
	if err != nil {
		return nil, err
	}

	var req pbin.Request
	if err := json.Unmarshal(buf, &req); err != nil {
		return nil, err
	}

	return &req, nil
}

func handleRequest(log logging.Logger, scmProvider *scm.Provider, bdevProvider *bdev.Provider, req *pbin.Request, resDest io.Writer) (err error) {
	if req == nil {
		return errors.New("nil request")
	}
	var res pbin.Response

	switch req.Method {
	case "Ping":
		return sendSuccess(&pbin.PingResp{Version: build.DaosVersion}, &res, resDest)
	case "ScmMount", "ScmUnmount":
		var mReq scm.MountRequest
		if err := json.Unmarshal(req.Payload, &mReq); err != nil {
			return sendFailure(err, &res, resDest)
		}

		var mRes *scm.MountResponse
		switch req.Method {
		case "ScmMount":
			mRes, err = scmProvider.Mount(mReq)
		case "ScmUnmount":
			mRes, err = scmProvider.Unmount(mReq)
		}
		if err != nil {
			return sendFailure(err, &res, resDest)
		}

		return sendSuccess(mRes, &res, resDest)
	case "ScmFormat", "ScmCheckFormat":
		var fReq scm.FormatRequest
		if err := json.Unmarshal(req.Payload, &fReq); err != nil {
			return sendFailure(err, &res, resDest)
		}

		var fRes *scm.FormatResponse
		switch req.Method {
		case "ScmFormat":
			fRes, err = scmProvider.Format(fReq)
		case "ScmCheckFormat":
			fRes, err = scmProvider.CheckFormat(fReq)
		}
		if err != nil {
			return sendFailure(err, &res, resDest)
		}

		return sendSuccess(fRes, &res, resDest)
	case "ScmScan":
		var sReq scm.ScanRequest
		if err := json.Unmarshal(req.Payload, &sReq); err != nil {
			return sendFailure(err, &res, resDest)
		}

		sRes, err := scmProvider.Scan(sReq)
		if err != nil {
			return sendFailure(err, &res, resDest)
		}

		return sendSuccess(sRes, &res, resDest)
	case "ScmPrepare":
		var pReq scm.PrepareRequest
		if err := json.Unmarshal(req.Payload, &pReq); err != nil {
			return sendFailure(err, &res, resDest)
		}

		pRes, err := scmProvider.Prepare(pReq)
		if err != nil {
			return sendFailure(err, &res, resDest)
		}

		return sendSuccess(pRes, &res, resDest)
	case "BdevInit":
		var iReq bdev.InitRequest
		if err := json.Unmarshal(req.Payload, &iReq); err != nil {
			return sendFailure(err, &res, resDest)
		}

		err = bdevProvider.Init(iReq)
		if err != nil {
			return sendFailure(err, &res, resDest)
		}

		return sendSuccess(nil, &res, resDest)
	case "BdevScan":
		var sReq bdev.ScanRequest
		if err := json.Unmarshal(req.Payload, &sReq); err != nil {
			return sendFailure(err, &res, resDest)
		}

		sRes, err := bdevProvider.Scan(sReq)
		if err != nil {
			return sendFailure(err, &res, resDest)
		}

		return sendSuccess(sRes, &res, resDest)
	case "BdevPrepare":
		var pReq bdev.PrepareRequest
		if err := json.Unmarshal(req.Payload, &pReq); err != nil {
			return sendFailure(err, &res, resDest)
		}

		pRes, err := bdevProvider.Prepare(pReq)
		if err != nil {
			return sendFailure(err, &res, resDest)
		}

		return sendSuccess(pRes, &res, resDest)
	case "BdevFormat":
		var fReq bdev.FormatRequest
		if err := json.Unmarshal(req.Payload, &fReq); err != nil {
			return sendFailure(err, &res, resDest)
		}

		fRes, err := bdevProvider.Format(fReq)
		if err != nil {
			return sendFailure(err, &res, resDest)
		}

		return sendSuccess(fRes, &res, resDest)
	default:
		return sendFailure(errors.Errorf("unhandled method %q", req.Method), &res, resDest)
	}
}
