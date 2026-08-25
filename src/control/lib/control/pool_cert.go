//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"crypto"
	"crypto/x509"
	"time"

	"github.com/google/uuid"
	"github.com/pkg/errors"
	"google.golang.org/grpc"
	"google.golang.org/protobuf/proto"

	pbUtil "github.com/daos-stack/daos/src/control/common/proto"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/security"
)

var (
	// ErrNodeAuthEnabled is returned when a pool already has a CA installed
	// and the request did not ask to append.
	ErrNodeAuthEnabled = errors.New("node authentication is already enabled")
	// ErrPoolCABundleFull is returned when a pool's CA bundle has no room for
	// another CA.
	ErrPoolCABundleFull = errors.New("pool CA bundle is full")
)

// respPoolUUID parses the pool UUID a server response carries as a string.
func respPoolUUID(s string) (uuid.UUID, error) {
	id, err := uuid.Parse(s)
	if err != nil {
		return uuid.Nil, errors.Wrap(err, "pool UUID in response")
	}
	return id, nil
}

// PoolIssueClientCertsReq contains the parameters for issuing client
// certificates for a pool.
type PoolIssueClientCertsReq struct {
	poolRequest
	ID       string            // pool UUID or label
	CACert   *x509.Certificate // pool CA to sign with; must be in the pool's bundle
	CAKey    crypto.PrivateKey
	Nodes    []string // CN = node:<name>
	Tenants  []string // CN = tenant:<name>; mutually exclusive with Nodes
	Replace  bool     // revoke each node's existing certificate first (node certs only)
	Validity time.Duration
}

// PoolIssueClientCerts issues client certificates postdated past the pool's
// revocation watermarks, revoking the nodes first when Replace is set.
func PoolIssueClientCerts(ctx context.Context, rpcClient UnaryInvoker, req *PoolIssueClientCertsReq) ([]*security.ClientCert, error) {
	if req == nil {
		return nil, errors.New("nil PoolIssueClientCertsReq")
	}
	issueReq := &security.IssueClientCertsReq{
		CACert:   req.CACert,
		CAKey:    req.CAKey,
		Nodes:    req.Nodes,
		Tenants:  req.Tenants,
		Validity: req.Validity,
	}
	if err := issueReq.Validate(); err != nil {
		return nil, err
	}
	if req.Replace && len(req.Tenants) > 0 {
		return nil, errors.New("Replace applies to node certificates only")
	}
	if req.CACert == nil {
		return nil, errors.New("no pool CA")
	}
	if err := security.VerifyCertKey(req.CAKey, req.CACert); err != nil {
		return nil, errors.Wrap(err, "pool CA")
	}

	state, err := GetPoolNodeAuthState(ctx, rpcClient, req.ID)
	if err != nil {
		return nil, err
	}
	if !state.Enabled() {
		return nil, errors.Errorf("node authentication is not enabled on pool %s", state.PoolUUID)
	}
	if !state.Installed(req.CACert) {
		return nil, errors.Errorf("pool CA %s is not installed on pool %s",
			security.CertFingerprint(req.CACert), state.PoolUUID)
	}

	if req.Replace {
		for _, name := range req.Nodes {
			resp, err := PoolRevokeClient(ctx, rpcClient, &PoolRevokeClientReq{
				ID:        req.ID,
				Node:      name,
				EvictMode: daos.PoolRevokeEvictDefault,
			})
			if err != nil {
				return nil, errors.Wrapf(err, "revoking node %s", name)
			}
			state.Watermarks[resp.CN] = resp.Watermark
		}
	}
	issueReq.Watermarks = state.Watermarks
	return security.IssueClientCerts(issueReq)
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
	poolUUID, err := respPoolUUID(pbResp.GetPoolUuid())
	if err != nil {
		return nil, err
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

// PoolAddCAReq contains pool add-CA parameters.
type PoolAddCAReq struct {
	poolRequest
	ID      string // pool UUID or label
	CertPEM []byte // PEM-encoded CA certificate to append
	Append  bool   // add to an existing bundle (rotation) rather than refusing
	NoEvict bool   // keep existing handles when this enables node auth
}

// PoolAddCAResp carries the result of a PoolAddCA call.
type PoolAddCAResp struct {
	PoolUUID       uuid.UUID
	HandlesEvicted int32
}

// PoolAddCA installs a CA cert, refusing with ErrNodeAuthEnabled if the
// pool already has one unless Append is set, and with ErrPoolCABundleFull
// if the bundle has no room for it.
func PoolAddCA(ctx context.Context, rpcClient UnaryInvoker, req *PoolAddCAReq) (*PoolAddCAResp, error) {
	if req == nil {
		return nil, errors.New("nil PoolAddCAReq")
	}
	// Parse for early sanity only; the server verifies provenance
	// against the DAOS root before installing.
	if _, err := security.ParsePoolCACert(req.CertPEM); err != nil {
		return nil, err
	}
	caResp, err := PoolGetCA(ctx, rpcClient, &PoolGetCAReq{ID: req.ID})
	if err != nil {
		return nil, errors.Wrap(err, "checking the installed CAs")
	}
	switch n := len(caResp.Certs); {
	case !req.Append && n > 0:
		return nil, errors.Wrapf(ErrNodeAuthEnabled, "%d CA(s) installed", n)
	case n >= security.PoolCABundleMaxCerts:
		return nil, errors.Wrapf(ErrPoolCABundleFull, "%d CAs installed", n)
	}

	pbReq := &mgmtpb.PoolAddCAReq{
		Sys:     req.getSystem(rpcClient),
		Id:      req.ID,
		CertPem: req.CertPEM,
		NoEvict: req.NoEvict,
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolAddCA(ctx, pbReq)
	})

	rpcClient.Debugf("DAOS pool add-CA request: %s\n", pbUtil.Debug(pbReq))
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}
	if err := ur.getMSError(); err != nil {
		return nil, errors.Wrap(err, "pool add-CA failed")
	}
	msResp, err := ur.getMSResponse()
	if err != nil {
		return nil, errors.Wrap(err, "pool add-CA response")
	}
	pbResp, ok := msResp.(*mgmtpb.PoolAddCAResp)
	if !ok {
		return nil, errors.Errorf("unexpected response type %T", msResp)
	}
	poolUUID, err := respPoolUUID(pbResp.GetPoolUuid())
	if err != nil {
		return nil, err
	}
	return &PoolAddCAResp{PoolUUID: poolUUID, HandlesEvicted: pbResp.GetHandlesEvicted()}, nil
}

// PoolRemoveCAReq contains pool remove-CA parameters.
type PoolRemoveCAReq struct {
	poolRequest
	ID          string // pool UUID or label
	Fingerprint string // SHA-256 fingerprint (hex) of CA to remove; empty with All
	All         bool   // if true, clear the entire CA bundle
}

// PoolRemoveCAResp contains the result of a remove-CA operation.
type PoolRemoveCAResp struct {
	PoolUUID     uuid.UUID
	CertsRemoved int
}

// PoolRemoveCA removes one (by Fingerprint) or all CAs from the pool bundle.
func PoolRemoveCA(ctx context.Context, rpcClient UnaryInvoker, req *PoolRemoveCAReq) (*PoolRemoveCAResp, error) {
	if req == nil {
		return nil, errors.New("nil PoolRemoveCAReq")
	}
	if !req.All && req.Fingerprint == "" {
		return nil, errors.New("specify Fingerprint or All")
	}
	if req.All && req.Fingerprint != "" {
		return nil, errors.New("Fingerprint and All are mutually exclusive")
	}

	pbReq := &mgmtpb.PoolRemoveCAReq{
		Sys:         req.getSystem(rpcClient),
		Id:          req.ID,
		Fingerprint: req.Fingerprint,
		All:         req.All,
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolRemoveCA(ctx, pbReq)
	})

	rpcClient.Debugf("DAOS pool remove-CA request: %s\n", pbUtil.Debug(pbReq))
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}
	if err := ur.getMSError(); err != nil {
		return nil, errors.Wrap(err, "pool remove-CA failed")
	}

	msResp, err := ur.getMSResponse()
	if err != nil {
		return nil, errors.Wrap(err, "pool remove-CA response")
	}
	pbResp, ok := msResp.(*mgmtpb.PoolRemoveCAResp)
	if !ok {
		return nil, errors.Errorf("unexpected response type %T", msResp)
	}

	poolUUID, err := respPoolUUID(pbResp.GetPoolUuid())
	if err != nil {
		return nil, err
	}
	return &PoolRemoveCAResp{
		PoolUUID:     poolUUID,
		CertsRemoved: int(pbResp.GetCertsRemoved()),
	}, nil
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

var evictModes = map[daos.PoolRevokeEvictMode]mgmtpb.PoolRevokeClientReq_EvictMode{
	daos.PoolRevokeEvictDefault:  mgmtpb.PoolRevokeClientReq_EVICT_DEFAULT,
	daos.PoolRevokeEvictPoolWide: mgmtpb.PoolRevokeClientReq_EVICT_POOL_WIDE,
	daos.PoolRevokeEvictNone:     mgmtpb.PoolRevokeClientReq_EVICT_NONE,
}

// PoolRevokeClientReq contains pool revoke-client parameters.
type PoolRevokeClientReq struct {
	poolRequest
	ID        string                   // pool UUID or label
	Node      string                   // node name to revoke (mutually exclusive with Tenant)
	Tenant    string                   // tenant name to revoke (mutually exclusive with Node)
	EvictMode daos.PoolRevokeEvictMode // how to evict active handles
}

// PoolRevokeClientResp contains the result of a revoke-client operation.
type PoolRevokeClientResp struct {
	PoolUUID       uuid.UUID
	CN             string    // the revoked CN (with prefix)
	Watermark      time.Time // certs at or before this NotBefore are revoked
	HandlesEvicted int32     // number of active handles evicted
	EvictScope     daos.PoolRevokeEvictScope
}

// PoolRevokeClient advances the pool's per-CN revocation watermark.
func PoolRevokeClient(ctx context.Context, rpcClient UnaryInvoker, req *PoolRevokeClientReq) (*PoolRevokeClientResp, error) {
	if req == nil {
		return nil, errors.New("nil PoolRevokeClientReq")
	}
	if (req.Node == "") == (req.Tenant == "") {
		return nil, errors.New("specify exactly one of Node or Tenant")
	}

	cn := security.PoolCertCNPrefixNode + req.Node
	if req.Tenant != "" {
		cn = security.PoolCertCNPrefixTenant + req.Tenant
	}
	evictMode, ok := evictModes[req.EvictMode]
	if !ok {
		return nil, errors.Errorf("unknown evict mode %d", req.EvictMode)
	}

	pbReq := &mgmtpb.PoolRevokeClientReq{
		Sys:       req.getSystem(rpcClient),
		Id:        req.ID,
		Cn:        cn,
		EvictMode: evictMode,
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolRevokeClient(ctx, pbReq)
	})

	rpcClient.Debugf("DAOS pool revoke-client request: %s\n", pbUtil.Debug(pbReq))
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}
	if err := ur.getMSError(); err != nil {
		return nil, errors.Wrap(err, "pool revoke-client failed")
	}

	msResp, err := ur.getMSResponse()
	if err != nil {
		return nil, errors.Wrap(err, "pool revoke-client response")
	}
	pbResp, ok := msResp.(*mgmtpb.PoolRevokeClientResp)
	if !ok {
		return nil, errors.Errorf("unexpected response type %T", msResp)
	}

	watermark, err := time.Parse(time.RFC3339, pbResp.GetWatermarkRfc3339())
	if err != nil {
		return nil, errors.Wrap(err, "parsing committed watermark")
	}

	poolUUID, err := respPoolUUID(pbResp.GetPoolUuid())
	if err != nil {
		return nil, err
	}
	return &PoolRevokeClientResp{
		PoolUUID:       poolUUID,
		CN:             cn,
		Watermark:      watermark.UTC(),
		HandlesEvicted: pbResp.GetHandlesEvictedCount(),
		EvictScope:     daos.PoolRevokeEvictScope(pbResp.GetEvictScope()),
	}, nil
}
