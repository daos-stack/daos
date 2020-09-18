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
package bdev

import (
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/pbin"
)

type Forwarder struct {
	pbin.Forwarder
}

func NewForwarder(log logging.Logger) *Forwarder {
	pf := pbin.NewForwarder(log, pbin.DaosAdminName)

	return &Forwarder{
		Forwarder: *pf,
	}
}

func (f *Forwarder) Scan(req ScanRequest) (*ScanResponse, error) {
	req.Forwarded = true

	res := new(ScanResponse)
	if err := f.SendReq("BdevScan", req, res); err != nil {
		return nil, err
	}

	return res, nil
}

func (f *Forwarder) Prepare(req PrepareRequest) (*PrepareResponse, error) {
	req.Forwarded = true

	res := new(PrepareResponse)
	if err := f.SendReq("BdevPrepare", req, res); err != nil {
		return nil, err
	}

	return res, nil
}

func (f *Forwarder) Format(req FormatRequest) (*FormatResponse, error) {
	req.Forwarded = true

	res := new(FormatResponse)
	if err := f.SendReq("BdevFormat", req, res); err != nil {
		return nil, err
	}

	return res, nil
}
