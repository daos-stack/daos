//
// (C) Copyright 2019 Intel Corporation.
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
package scm

import (
	"context"
	"encoding/json"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/pbin"
)

type Forwarder struct {
	log logging.Logger
}

func (f *Forwarder) sendReq(method string, fwdReq interface{}, fwdRes interface{}) error {
	if fwdReq == nil {
		return errors.New("nil request")
	}
	if fwdRes == nil {
		return errors.New("nil response")
	}

	pbinPath, err := common.FindBinary(pbin.DaosAdminName)
	if err != nil {
		return err
	}

	payload, err := json.Marshal(fwdReq)
	if err != nil {
		return errors.Wrap(err, "failed to marshal forwarded request as payload")
	}

	req := &pbin.Request{
		Method:  method,
		Payload: payload,
	}

	ctx := context.TODO()
	res, err := pbin.ExecReq(ctx, f.log, pbinPath, req)
	if err != nil {
		if pbin.IsFailedRequest(err) {
			return err
		}
		return errors.Wrap(err, "privileged binary execution failed")
	}

	if err := json.Unmarshal(res.Payload, fwdRes); err != nil {
		return errors.Wrap(err, "failed to unmarshal forwarded response")
	}

	return nil
}

func (f *Forwarder) Mount(req MountRequest) (*MountResponse, error) {
	req.Forwarded = true

	var res MountResponse
	if err := f.sendReq("ScmMount", req, &res); err != nil {
		return nil, err
	}

	return &res, nil
}

func (f *Forwarder) Unmount(req MountRequest) (*MountResponse, error) {
	req.Forwarded = true

	var res MountResponse
	if err := f.sendReq("ScmUnmount", req, &res); err != nil {
		return nil, err
	}

	return &res, nil
}

func (f *Forwarder) Format(req FormatRequest) (*FormatResponse, error) {
	req.Forwarded = true

	var res FormatResponse
	if err := f.sendReq("ScmFormat", req, &res); err != nil {
		return nil, err
	}

	return &res, nil
}

func (f *Forwarder) CheckFormat(req FormatRequest) (*FormatResponse, error) {
	req.Forwarded = true

	var res FormatResponse
	if err := f.sendReq("ScmCheckFormat", req, &res); err != nil {
		return nil, err
	}

	return &res, nil
}

func (f *Forwarder) Scan(req ScanRequest) (*ScanResponse, error) {
	req.Forwarded = true

	var res ScanResponse
	if err := f.sendReq("ScmScan", req, &res); err != nil {
		return nil, err
	}

	return &res, nil
}

func (f *Forwarder) Prepare(req PrepareRequest) (*PrepareResponse, error) {
	req.Forwarded = true

	var res PrepareResponse
	if err := f.sendReq("ScmPrepare", req, &res); err != nil {
		return nil, err
	}

	return &res, nil
}
