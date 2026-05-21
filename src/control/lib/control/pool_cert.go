//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/pem"
	"fmt"
	"math/big"
	"path/filepath"
	"strings"
	"time"

	"github.com/pkg/errors"
	"google.golang.org/grpc"
	"google.golang.org/protobuf/proto"

	pbUtil "github.com/daos-stack/daos/src/control/common/proto"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/security"
)

// PoolSetupCertAuthReq contains the parameters for setting up
// certificate-based authentication on a pool.
type PoolSetupCertAuthReq struct {
	poolRequest
	ID string // pool UUID or label

	// For generate mode: provide both.
	DaosCACertPath string // path to DAOS CA certificate
	DaosCAKeyPath  string // path to DAOS CA private key

	// For import mode: provide CertPEM instead.
	CertPEM []byte // PEM-encoded pool CA cert to import

	// Replace clears any existing CA bundle before installing the new cert.
	Replace bool
}

// PoolSetupCertAuthResp contains the result of setting up cert auth.
type PoolSetupCertAuthResp struct {
	PoolUUID  string
	CACertPEM []byte // PEM-encoded pool CA certificate
	CAKeyPEM  []byte // PEM-encoded pool CA private key (empty for import mode)
}

// PoolSetupCertAuth installs a pool intermediate CA, generating a fresh
// ECDSA P-384 CA signed by the DAOS root unless CertPEM is supplied.
func PoolSetupCertAuth(ctx context.Context, rpcClient UnaryInvoker, req *PoolSetupCertAuthReq) (*PoolSetupCertAuthResp, error) {
	var certPEM, keyPEM []byte

	if len(req.CertPEM) > 0 {
		cert, err := parsePEMCACert(req.CertPEM)
		if err != nil {
			return nil, err
		}
		if req.DaosCACertPath != "" {
			if err := verifyCertChain(req.DaosCACertPath, cert); err != nil {
				return nil, errors.Wrap(err, "pool CA does not chain to DAOS CA")
			}
		}
		certPEM = req.CertPEM
	} else {
		if req.DaosCACertPath == "" || req.DaosCAKeyPath == "" {
			return nil, errors.New("DaosCACertPath and DaosCAKeyPath are required for generate mode")
		}

		poolResp, err := PoolQuery(ctx, rpcClient, &PoolQueryReq{ID: req.ID})
		if err != nil {
			return nil, errors.Wrap(err, "resolving pool UUID")
		}
		poolUUID := poolResp.UUID.String()

		daosCACert, err := security.LoadCertificate(req.DaosCACertPath)
		if err != nil {
			return nil, errors.Wrap(err, "loading DAOS CA certificate")
		}
		daosCAKey, err := security.LoadPrivateKey(req.DaosCAKeyPath)
		if err != nil {
			return nil, errors.Wrap(err, "loading DAOS CA private key")
		}

		certPEM, keyPEM, err = generatePoolCA(poolUUID, daosCACert, daosCAKey)
		if err != nil {
			return nil, err
		}
	}

	addResp, err := PoolAddCA(ctx, rpcClient, &PoolAddCAReq{
		ID:      req.ID,
		Replace: req.Replace,
		CertPEM: certPEM,
	})
	if err != nil {
		return nil, errors.Wrap(err, "adding pool CA")
	}

	return &PoolSetupCertAuthResp{
		PoolUUID:  addResp.PoolUUID,
		CACertPEM: certPEM,
		CAKeyPEM:  keyPEM,
	}, nil
}

// PoolGenerateClientCertsReq contains the parameters for generating
// client certificates signed by a pool's CA.
type PoolGenerateClientCertsReq struct {
	CAKeyPath string
	Nodes     []string // CN = node:<name>
	Tenants   []string // CN = tenant:<name>; mutually exclusive with Nodes
}

// ClientCert contains the generated certificate and key for a client.
type ClientCert struct {
	Name    string // the node or tenant name (without prefix)
	CN      string // the full CN (with prefix)
	CertPEM []byte
	KeyPEM  []byte
}

// PoolGenerateClientCerts generates certificates signed by a pool CA.
// This is a local crypto operation — no RPC is needed.
func PoolGenerateClientCerts(req *PoolGenerateClientCertsReq) ([]*ClientCert, error) {
	if len(req.Nodes) == 0 && len(req.Tenants) == 0 {
		return nil, errors.New("specify Nodes or Tenants")
	}
	if len(req.Nodes) > 0 && len(req.Tenants) > 0 {
		return nil, errors.New("Nodes and Tenants are mutually exclusive")
	}

	caCert, caKey, err := loadCertAndKey(req.CAKeyPath)
	if err != nil {
		return nil, errors.Wrap(err, "loading pool CA")
	}

	type target struct {
		name string
		cn   string
	}
	var targets []target
	for _, n := range req.Nodes {
		targets = append(targets, target{name: n, cn: security.CertCNPrefixNode + n})
	}
	for _, t := range req.Tenants {
		targets = append(targets, target{name: t, cn: security.CertCNPrefixTenant + t})
	}

	var results []*ClientCert
	for _, tgt := range targets {
		certPEM, keyPEM, err := generateClientCert(tgt.cn, caCert, caKey)
		if err != nil {
			return nil, errors.Wrapf(err, "generating cert for %s", tgt.cn)
		}
		results = append(results, &ClientCert{
			Name:    tgt.name,
			CN:      tgt.cn,
			CertPEM: certPEM,
			KeyPEM:  keyPEM,
		})
	}

	return results, nil
}

// generatePoolCA creates a pool-specific intermediate CA key pair signed
// by the DAOS CA.
func generatePoolCA(poolUUID string, daosCACert *x509.Certificate, daosCAKey interface{}) (certPEM, keyPEM []byte, err error) {
	poolCAKey, err := ecdsa.GenerateKey(elliptic.P384(), rand.Reader)
	if err != nil {
		return nil, nil, errors.Wrap(err, "generating pool CA key")
	}

	serialNumber, err := rand.Int(rand.Reader, new(big.Int).Lsh(big.NewInt(1), 128))
	if err != nil {
		return nil, nil, errors.Wrap(err, "generating serial number")
	}

	now := time.Now()
	template := &x509.Certificate{
		SerialNumber: serialNumber,
		Subject: pkix.Name{
			CommonName:   fmt.Sprintf("DAOS Pool CA %s", poolUUID),
			Organization: []string{"DAOS"},
		},
		NotBefore:             now,
		NotAfter:              now.Add(365 * 24 * time.Hour),
		KeyUsage:              x509.KeyUsageCertSign | x509.KeyUsageCRLSign,
		BasicConstraintsValid: true,
		IsCA:                  true,
		MaxPathLen:            0,
		MaxPathLenZero:        true,
	}

	certDER, err := x509.CreateCertificate(rand.Reader, template, daosCACert, &poolCAKey.PublicKey, daosCAKey)
	if err != nil {
		return nil, nil, errors.Wrap(err, "creating pool CA certificate")
	}

	certPEM = pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: certDER})

	keyDER, err := x509.MarshalPKCS8PrivateKey(poolCAKey)
	if err != nil {
		return nil, nil, errors.Wrap(err, "marshaling pool CA key")
	}
	keyPEM = pem.EncodeToMemory(&pem.Block{Type: "PRIVATE KEY", Bytes: keyDER})

	return certPEM, keyPEM, nil
}

// generateClientCert creates a client certificate signed by a pool CA.
// NotBefore is set to the current time.
func generateClientCert(cn string, caCert *x509.Certificate, caKey interface{}) (certPEM, keyPEM []byte, err error) {
	return generateClientCertAt(cn, caCert, caKey, time.Now().UTC())
}

// generateClientCertAt creates a client cert with an explicit NotBefore.
func generateClientCertAt(cn string, caCert *x509.Certificate, caKey interface{}, notBefore time.Time) (certPEM, keyPEM []byte, err error) {
	key, err := ecdsa.GenerateKey(elliptic.P384(), rand.Reader)
	if err != nil {
		return nil, nil, errors.Wrap(err, "generating key")
	}

	serialNumber, err := rand.Int(rand.Reader, new(big.Int).Lsh(big.NewInt(1), 128))
	if err != nil {
		return nil, nil, errors.Wrap(err, "generating serial number")
	}

	template := &x509.Certificate{
		SerialNumber: serialNumber,
		Subject: pkix.Name{
			CommonName:   cn,
			Organization: []string{"DAOS"},
		},
		NotBefore: notBefore,
		NotAfter:  notBefore.Add(365 * 24 * time.Hour),
		KeyUsage:  x509.KeyUsageDigitalSignature,
		ExtKeyUsage: []x509.ExtKeyUsage{
			x509.ExtKeyUsageClientAuth,
		},
	}

	certDER, err := x509.CreateCertificate(rand.Reader, template, caCert, &key.PublicKey, caKey)
	if err != nil {
		return nil, nil, errors.Wrap(err, "creating certificate")
	}

	certPEM = pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: certDER})

	keyDER, err := x509.MarshalPKCS8PrivateKey(key)
	if err != nil {
		return nil, nil, errors.Wrap(err, "marshaling key")
	}
	keyPEM = pem.EncodeToMemory(&pem.Block{Type: "PRIVATE KEY", Bytes: keyDER})

	return certPEM, keyPEM, nil
}

// parsePEMCACert parses a PEM-encoded certificate and validates it is a CA.
func parsePEMCACert(certPEM []byte) (*x509.Certificate, error) {
	block, _ := pem.Decode(certPEM)
	if block == nil || block.Type != "CERTIFICATE" {
		return nil, errors.New("invalid PEM certificate")
	}
	cert, err := x509.ParseCertificate(block.Bytes)
	if err != nil {
		return nil, errors.Wrap(err, "parsing certificate")
	}
	if !cert.IsCA {
		return nil, errors.New("certificate is not a CA")
	}
	return cert, nil
}

// verifyCertChain verifies that cert chains to the CA at caCertPath.
func verifyCertChain(caCertPath string, cert *x509.Certificate) error {
	caCert, err := security.LoadCertificate(caCertPath)
	if err != nil {
		return errors.Wrap(err, "loading CA certificate")
	}

	roots := x509.NewCertPool()
	roots.AddCert(caCert)

	_, err = cert.Verify(x509.VerifyOptions{
		Roots:     roots,
		KeyUsages: []x509.ExtKeyUsage{x509.ExtKeyUsageAny},
	})
	return err
}

// loadCertAndKey loads a key and its sibling .crt (stripping any extension).
func loadCertAndKey(keyPath string) (*x509.Certificate, interface{}, error) {
	if keyPath == "" {
		return nil, nil, errors.New("key path is empty")
	}
	base := strings.TrimSuffix(keyPath, filepath.Ext(keyPath))
	certPath := base + ".crt"

	cert, err := security.LoadCertificate(certPath)
	if err != nil {
		return nil, nil, errors.Wrap(err, "loading certificate")
	}

	key, err := security.LoadPrivateKey(keyPath)
	if err != nil {
		return nil, nil, errors.Wrap(err, "loading private key")
	}

	return cert, key, nil
}

// PoolGetCAReq contains pool get-CA parameters.
type PoolGetCAReq struct {
	poolRequest
	ID string // pool UUID or label
}

// PoolGetCAResp carries the pool's CA bundle and the parsed cert summary.
type PoolGetCAResp struct {
	PoolUUID string
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
	resp := &PoolGetCAResp{
		PoolUUID: pbResp.GetPoolUuid(),
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

// PoolAddCAReq contains pool add-CA parameters.
type PoolAddCAReq struct {
	poolRequest
	ID      string // pool UUID or label
	CertPEM []byte // PEM-encoded CA certificate to append
	Replace bool   // clear existing bundle before append
}

// PoolAddCAResp carries the result of a PoolAddCA call.
type PoolAddCAResp struct {
	PoolUUID string
}

// PoolAddCA appends a CA cert to the pool's CA bundle.
func PoolAddCA(ctx context.Context, rpcClient UnaryInvoker, req *PoolAddCAReq) (*PoolAddCAResp, error) {
	if req == nil {
		return nil, errors.New("nil PoolAddCAReq")
	}
	if len(req.CertPEM) == 0 {
		return nil, errors.New("CertPEM is empty")
	}

	pbReq := &mgmtpb.PoolAddCAReq{
		Sys:     req.getSystem(rpcClient),
		Id:      req.ID,
		CertPem: req.CertPEM,
		Replace: req.Replace,
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
	return &PoolAddCAResp{PoolUUID: pbResp.GetPoolUuid()}, nil
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
	PoolUUID     string
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

	return &PoolRemoveCAResp{
		PoolUUID:     pbResp.GetPoolUuid(),
		CertsRemoved: int(pbResp.GetCertsRemoved()),
	}, nil
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

// PoolRevokeEvictMode controls how PoolRevokeClient evicts active handles.
type PoolRevokeEvictMode int

const (
	// PoolRevokeEvictDefault uses per-machine evict for node:X, pool-wide for tenant:X.
	PoolRevokeEvictDefault PoolRevokeEvictMode = iota
	// PoolRevokeEvictPoolWide evicts all handles on the pool.
	PoolRevokeEvictPoolWide
	// PoolRevokeEvictNone leaves active handles alone.
	PoolRevokeEvictNone
)

// PoolRevokeClientReq contains pool revoke-client parameters.
type PoolRevokeClientReq struct {
	poolRequest
	ID        string              // pool UUID or label
	CAKeyPath string              // path to the pool CA private key
	Node      string              // node name to revoke (mutually exclusive with Tenant)
	Tenant    string              // tenant name to revoke (mutually exclusive with Node)
	EvictMode PoolRevokeEvictMode // how to evict active handles
}

// RevokedClientCert is the fresh replacement cert+key produced by a
// revoke-client invocation.
type RevokedClientCert struct {
	Name    string // the node or tenant name (without prefix)
	CN      string // the full CN (with prefix)
	CertPEM []byte
	KeyPEM  []byte
}

// PoolRevokeClientResp contains the result of a revoke-client operation.
type PoolRevokeClientResp struct {
	PoolUUID       string
	Cert           *RevokedClientCert
	Watermark      time.Time // certs at or before this NotBefore are revoked
	HandlesEvicted int32     // number of active handles evicted
	EvictScope     string    // "machine" | "pool" | "none"
}

// PoolRevokeClient advances the pool's per-CN revocation watermark
// and signs a replacement cert with NotBefore strictly past the
// committed watermark.
func PoolRevokeClient(ctx context.Context, rpcClient UnaryInvoker, req *PoolRevokeClientReq) (*PoolRevokeClientResp, error) {
	if req == nil {
		return nil, errors.New("nil PoolRevokeClientReq")
	}
	if req.CAKeyPath == "" {
		return nil, errors.New("CAKeyPath is required")
	}
	if (req.Node == "") == (req.Tenant == "") {
		return nil, errors.New("specify exactly one of Node or Tenant")
	}

	name := req.Node
	cn := security.CertCNPrefixNode + req.Node
	if req.Tenant != "" {
		name = req.Tenant
		cn = security.CertCNPrefixTenant + req.Tenant
	}

	pbReq := &mgmtpb.PoolRevokeClientReq{
		Sys:       req.getSystem(rpcClient),
		Id:        req.ID,
		Cn:        cn,
		EvictMode: mgmtpb.PoolRevokeClientReq_EvictMode(req.EvictMode),
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
	watermark = watermark.UTC()

	caCert, caKey, err := loadCertAndKey(req.CAKeyPath)
	if err != nil {
		return nil, errors.Wrap(err, "loading pool CA")
	}

	certPEM, keyPEM, err := generateClientCertAt(cn, caCert, caKey, watermark.Add(time.Second))
	if err != nil {
		return nil, errors.Wrapf(err, "generating replacement cert for %s", cn)
	}

	return &PoolRevokeClientResp{
		PoolUUID: pbResp.GetPoolUuid(),
		Cert: &RevokedClientCert{
			Name:    name,
			CN:      cn,
			CertPEM: certPEM,
			KeyPEM:  keyPEM,
		},
		Watermark:      watermark,
		HandlesEvicted: pbResp.GetHandlesEvictedCount(),
		EvictScope:     pbResp.GetEvictScope(),
	}, nil
}
