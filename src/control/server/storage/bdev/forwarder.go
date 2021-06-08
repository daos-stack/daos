//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package bdev

import (
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/pbin"
)

// Forwarder forwards requests to the DAOS admin binary.
type Forwarder struct {
	pbin.Forwarder
}

// NewForwarder creates a new Forwarder.
func NewForwarder(log logging.Logger) *Forwarder {
	pf := pbin.NewForwarder(log, pbin.DaosAdminName)

	return &Forwarder{
		Forwarder: *pf,
	}
}

// Scan forwards an Bdev scan request.
func (f *Forwarder) Scan(req ScanRequest) (*ScanResponse, error) {
	req.Forwarded = true

	res := new(ScanResponse)
	if err := f.SendReq("BdevScan", req, res); err != nil {
		return nil, err
	}

	return res, nil
}

// Prepare forwards an Bdev prepare request.
func (f *Forwarder) Prepare(req PrepareRequest) (*PrepareResponse, error) {
	req.Forwarded = true

	res := new(PrepareResponse)
	if err := f.SendReq("BdevPrepare", req, res); err != nil {
		return nil, err
	}

	return res, nil
}

// Format forwards an Bdev format request.
func (f *Forwarder) Format(req FormatRequest) (*FormatResponse, error) {
	req.Forwarded = true

	res := new(FormatResponse)
	if err := f.SendReq("BdevFormat", req, res); err != nil {
		return nil, err
	}

	return res, nil
}
