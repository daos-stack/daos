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

func (svc *ControlService) findRankIndex(rank ranklist.Rank) (uint32, error) {
	foundIdx := -1
	for _, e := range svc.harness.instances {
		r, err := e.GetRank()
		if err != nil {
			svc.log.Errorf("unable to get rank for engine %d: %s", e.Index(), err)
			continue
		}

		if rank == r {
			svc.log.Tracef("rank %d is local engine %d", rank, e.Index())
			foundIdx = int(e.Index())
			break
		}
	}

	if foundIdx < 0 {
		return 0, errors.Errorf("rank %d is not managed by this DAOS server", rank)
	}

	return uint32(foundIdx), nil
}

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

	idx, err := svc.findRankIndex(r)
	if err != nil {
		return nil, err
	}

	dResp, err := svc.harness.instances[idx].CallDrpc(ctx, daos.MethodCheckerAction, req.Req)
	if err != nil {
		return nil, errors.Wrapf(err, "dRPC to engine %d (rank %d)", idx, r)
	}

	checkActResp := new(mgmtpb.CheckActResp)
	if err := proto.Unmarshal(dResp.Body, checkActResp); err != nil {
		return nil, errors.Wrapf(err, "unable to unmarshal engine %d response", idx)
	}

	return &ctlpb.CheckEngineActResp{
		Rank: req.Rank,
		Resp: checkActResp,
	}, nil
}
