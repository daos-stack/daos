//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"

	"github.com/google/uuid"
	"github.com/pkg/errors"
	"google.golang.org/grpc"
	"google.golang.org/protobuf/proto"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/security"
)

// PoolGetCAReq contains pool get-CA parameters.
type PoolGetCAReq struct {
	poolRequest
	ID string // pool UUID or label
}

// PoolGetCAResp carries the pool's CA bundle and the parsed cert summary.
type PoolGetCAResp struct {
	PoolUUID uuid.UUID
	PEM      []byte
	Certs    []security.PoolCertInfo
}

// PoolGetCA returns the pool's CA bundle.
func PoolGetCA(ctx context.Context, rpcClient UnaryInvoker, req *PoolGetCAReq) (*PoolGetCAResp, error) {
	if req == nil {
		return nil, errors.New("nil PoolGetCAReq")
	}
	pbReq := &mgmtpb.PoolGetCAReq{
		Sys: req.getSystem(rpcClient),
		Id:  req.ID,
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolGetCA(ctx, pbReq)
	})
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}
	if err := ur.getMSError(); err != nil {
		return nil, errors.Wrap(err, "pool get-CA failed")
	}
	msResp, err := ur.getMSResponse()
	if err != nil {
		return nil, errors.Wrap(err, "pool get-CA response")
	}
	pbResp, ok := msResp.(*mgmtpb.PoolGetCAResp)
	if !ok {
		return nil, errors.Errorf("unexpected response type %T", msResp)
	}
	poolUUID, err := uuid.Parse(pbResp.GetPoolUuid())
	if err != nil {
		return nil, errors.Wrap(err, "pool get-CA response: pool UUID")
	}
	resp := &PoolGetCAResp{
		PoolUUID: poolUUID,
		PEM:      pbResp.GetCaBundle(),
	}
	if len(resp.PEM) > 0 {
		resp.Certs, err = security.ParseCABundle(resp.PEM)
		if err != nil {
			return nil, errors.Wrap(err, "parsing pool CA bundle")
		}
	}
	return resp, nil
}

// PoolGetCertWatermarksReq contains pool get-cert-watermarks parameters.
type PoolGetCertWatermarksReq struct {
	poolRequest
	ID string // pool UUID or label
}

// PoolGetCertWatermarks returns the pool's per-CN revocation watermarks.
func PoolGetCertWatermarks(ctx context.Context, rpcClient UnaryInvoker, req *PoolGetCertWatermarksReq) (security.CertWatermarks, error) {
	if req == nil {
		return nil, errors.New("nil PoolGetCertWatermarksReq")
	}
	pbReq := &mgmtpb.PoolGetCertWatermarksReq{
		Sys: req.getSystem(rpcClient),
		Id:  req.ID,
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolGetCertWatermarks(ctx, pbReq)
	})
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}
	if err := ur.getMSError(); err != nil {
		return nil, errors.Wrap(err, "pool get-cert-watermarks failed")
	}
	msResp, err := ur.getMSResponse()
	if err != nil {
		return nil, errors.Wrap(err, "pool get-cert-watermarks response")
	}
	pbResp, ok := msResp.(*mgmtpb.PoolGetCertWatermarksResp)
	if !ok {
		return nil, errors.Errorf("unexpected response type %T", msResp)
	}
	if len(pbResp.GetWatermarks()) == 0 {
		return nil, nil
	}
	return security.DecodeCertWatermarks(pbResp.GetWatermarks())
}
