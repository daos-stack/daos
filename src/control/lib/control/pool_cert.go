//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"crypto/x509"

	"github.com/google/uuid"
	"github.com/pkg/errors"
	"google.golang.org/grpc"
	"google.golang.org/protobuf/proto"

	pbUtil "github.com/daos-stack/daos/src/control/common/proto"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/security"
)

// respPoolUUID parses the pool UUID a server response carries as a string.
func respPoolUUID(s string) (uuid.UUID, error) {
	id, err := uuid.Parse(s)
	if err != nil {
		return uuid.Nil, errors.Wrap(err, "pool UUID in response")
	}
	return id, nil
}

// PoolNodeAuthState is what the system holds about a pool's node
// authentication: its CA bundle and, when enabled, its revocation watermarks.
type PoolNodeAuthState struct {
	PoolUUID   uuid.UUID
	CABundle   []byte // PEM, as stored in the ca_cert property
	CAs        []security.CABundleEntry
	Watermarks security.CertWatermarks
}

// Enabled reports whether the pool requires node certificates.
func (s *PoolNodeAuthState) Enabled() bool {
	return len(s.CAs) > 0
}

// Installed reports whether cert is in the pool's CA bundle.
func (s *PoolNodeAuthState) Installed(cert *x509.Certificate) bool {
	fingerprint := security.CertFingerprint(cert)
	for _, e := range s.CAs {
		if e.Fingerprint == fingerprint {
			return true
		}
	}
	return false
}

// GetPoolNodeAuthState reads the pool's CA bundle and, if node
// authentication is enabled, its revocation watermarks.
func GetPoolNodeAuthState(ctx context.Context, rpcClient UnaryInvoker, id string) (*PoolNodeAuthState, error) {
	caResp, err := PoolGetCA(ctx, rpcClient, &PoolGetCAReq{ID: id})
	if err != nil {
		return nil, err
	}
	state := &PoolNodeAuthState{PoolUUID: caResp.PoolUUID, CABundle: caResp.PEM, CAs: caResp.Certs}
	if !state.Enabled() {
		return state, nil
	}
	wmResp, err := PoolGetCertWatermarks(ctx, rpcClient, &PoolGetCertWatermarksReq{ID: id})
	if err != nil {
		return nil, err
	}
	state.Watermarks = wmResp.Watermarks
	return state, nil
}

// PoolGetCAReq contains pool get-CA parameters.
type PoolGetCAReq struct {
	poolRequest
	ID string // pool UUID or label
}

// PoolGetCAResp carries the pool's CA bundle, raw and parsed.
type PoolGetCAResp struct {
	PoolUUID uuid.UUID
	PEM      []byte
	Certs    []security.CABundleEntry
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

	rpcClient.Debugf("DAOS pool get-CA request: %s\n", pbUtil.Debug(pbReq))
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
	resp.Certs, err = security.ParseCABundle(resp.PEM)
	if err != nil {
		return nil, errors.Wrap(err, "parsing pool CA bundle")
	}
	return resp, nil
}

// PoolGetCertWatermarksReq contains pool get-cert-watermarks parameters.
type PoolGetCertWatermarksReq struct {
	poolRequest
	ID string // pool UUID or label
}

// PoolGetCertWatermarksResp carries the pool's per-CN revocation watermarks.
type PoolGetCertWatermarksResp struct {
	PoolUUID   uuid.UUID
	Watermarks security.CertWatermarks
}

// PoolGetCertWatermarks returns the pool's per-CN revocation watermarks.
func PoolGetCertWatermarks(ctx context.Context, rpcClient UnaryInvoker, req *PoolGetCertWatermarksReq) (*PoolGetCertWatermarksResp, error) {
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

	rpcClient.Debugf("DAOS pool get-cert-watermarks request: %s\n", pbUtil.Debug(pbReq))
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
	poolUUID, err := respPoolUUID(pbResp.GetPoolUuid())
	if err != nil {
		return nil, err
	}
	resp := &PoolGetCertWatermarksResp{PoolUUID: poolUUID, Watermarks: security.CertWatermarks{}}
	if len(pbResp.GetWatermarks()) > 0 {
		resp.Watermarks, err = security.DecodeCertWatermarks(pbResp.GetWatermarks())
		if err != nil {
			return nil, err
		}
	}
	return resp, nil
}
