//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"

	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
)

// CheckEngineRepair sends a check repair request to a specific engine.
func (svc *ControlService) CheckEngineRepair(ctx context.Context, req *ctlpb.CheckEngineActReq) (*ctlpb.CheckEngineActResp, error) {
	if req == nil {
		return nil, errors.Errorf("nil %T", req)
	}

	if req.Req == nil {
		return nil, errors.New("no CheckActReq in request")
	}

	r := ranklist.Rank(req.Rank)
	if r == ranklist.NilRank {
		return nil, errors.New("nil rank in request")
	}

	engSlice, err := svc.harness.FilterInstancesByRankSet(r.String())
	if err != nil {
		return nil, err
	}

	if len(engSlice) == 0 {
		return nil, errors.Errorf("rank %d is not managed by this DAOS server", r)
	}

	eng := engSlice[0]
	dResp, err := eng.CallDrpc(ctx, daos.MethodCheckerAction, req.Req)
	if err != nil {
		return nil, errors.Wrapf(err, "dRPC to engine %d (rank %d)", eng.Index(), r)
	}

	checkActResp := new(mgmtpb.CheckActResp)
	if err := proto.Unmarshal(dResp.Body, checkActResp); err != nil {
		return nil, errors.Wrapf(err, "unable to unmarshal engine %d response", eng.Index())
	}

	return &ctlpb.CheckEngineActResp{
		Rank: req.Rank,
		Resp: checkActResp,
	}, nil
}
