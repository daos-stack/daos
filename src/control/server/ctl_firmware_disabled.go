//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
// +build !firmware

package server

import (
	"context"

	"github.com/pkg/errors"

	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
)

// FirmwareQuery is not implemented if firmware management is not enabled in the build.
func (svc *ControlService) FirmwareQuery(parent context.Context, pbReq *ctlpb.FirmwareQueryReq) (*ctlpb.FirmwareQueryResp, error) {
	return nil, errors.New("not implemented")
}

// FirmwareUpdate is not implemented if firmware management is not enabled in the build.
func (svc *ControlService) FirmwareUpdate(parent context.Context, pbReq *ctlpb.FirmwareUpdateReq) (*ctlpb.FirmwareUpdateResp, error) {
	return nil, errors.New("not implemented")
}
