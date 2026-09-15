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
	"math/big"
	"path/filepath"
	"strings"
	"time"

	"github.com/google/uuid"
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

	Append bool // add to an existing bundle (rotation) rather than refusing

	Validity time.Duration // generate mode; zero means until the DAOS CA expires
	NoEvict  bool          // keep existing handles when enabling
}

// ErrNodeAuthEnabled is returned when a pool already has a CA installed
// and the request did not ask to append.
var ErrNodeAuthEnabled = errors.New("node authentication is already enabled")

// PoolSetupCertAuthResp contains the result of setting up cert auth.
type PoolSetupCertAuthResp struct {
	PoolUUID       uuid.UUID
	CACertPEM      []byte // PEM-encoded pool CA certificate
	CAKeyPEM       []byte // PEM-encoded pool CA private key (empty for import mode)
	HandlesEvicted int32
}

// PoolGenerateCAReq contains the parameters for generating a pool CA.
type PoolGenerateCAReq struct {
	poolRequest
	ID             string        // pool UUID or label
	DaosCACertPath string        // path to DAOS CA certificate
	DaosCAKeyPath  string        // path to DAOS CA private key
	Validity       time.Duration // zero means until the DAOS CA expires
}

// respPoolUUID parses the pool UUID a server response carries as a string.
func respPoolUUID(s string) (uuid.UUID, error) {
	id, err := uuid.Parse(s)
	if err != nil {
		return uuid.Nil, errors.Wrap(err, "pool UUID in response")
	}
	return id, nil
}

// PoolCA is a generated pool CA key pair.
type PoolCA struct {
	PoolUUID uuid.UUID
	CertPEM  []byte
	KeyPEM   []byte
	NotAfter time.Time
}

// DefaultClientCertValidity is the client certificate lifetime when none is requested.
const DefaultClientCertValidity = 365 * 24 * time.Hour

// clipNotAfter bounds a lifetime to the signer's own NotAfter; a certificate
// cannot verify past its issuer.
func clipNotAfter(notBefore time.Time, validity time.Duration, signer *x509.Certificate) time.Time {
	if validity <= 0 || notBefore.Add(validity).After(signer.NotAfter) {
		return signer.NotAfter
	}
	return notBefore.Add(validity)
}

// PoolGenerateCA generates a pool CA signed by the DAOS root.
func PoolGenerateCA(ctx context.Context, rpcClient UnaryInvoker, req *PoolGenerateCAReq) (*PoolCA, error) {
	if req == nil {
		return nil, errors.New("nil PoolGenerateCAReq")
	}
	if req.DaosCACertPath == "" || req.DaosCAKeyPath == "" {
		return nil, errors.New("DaosCACertPath and DaosCAKeyPath are required")
	}

	poolResp, err := PoolQuery(ctx, rpcClient, &PoolQueryReq{ID: req.ID})
	if err != nil {
		return nil, errors.Wrap(err, "resolving pool UUID")
	}
	return GeneratePoolCA(poolResp.UUID, req.DaosCACertPath, req.DaosCAKeyPath, req.Validity)
}

// GeneratePoolCA mints a pool CA signed by the DAOS CA without contacting
// the system, so it can run where the DAOS CA key is kept.
func GeneratePoolCA(poolUUID uuid.UUID, daosCACertPath, daosCAKeyPath string, validity time.Duration) (*PoolCA, error) {
	if daosCACertPath == "" || daosCAKeyPath == "" {
		return nil, errors.New("DAOS CA certificate and key paths are required")
	}
	daosCACert, err := security.LoadCertificate(daosCACertPath)
	if err != nil {
		return nil, errors.Wrap(err, "loading DAOS CA certificate")
	}
	daosCAKey, err := security.LoadPrivateKey(daosCAKeyPath)
	if err != nil {
		return nil, errors.Wrap(err, "loading DAOS CA private key")
	}

	certPEM, keyPEM, notAfter, err := generatePoolCA(poolUUID, daosCACert, daosCAKey, validity)
	if err != nil {
		return nil, err
	}
	return &PoolCA{PoolUUID: poolUUID, CertPEM: certPEM, KeyPEM: keyPEM, NotAfter: notAfter}, nil
}

// PoolInstallCAReq contains the parameters for installing a pool CA.
type PoolInstallCAReq struct {
	poolRequest
	ID      string // pool UUID or label
	CertPEM []byte // PEM-encoded pool CA certificate
	Append  bool   // add to an existing bundle (rotation) rather than refusing
	NoEvict bool   // keep existing handles when enabling
}

// PoolInstallCA installs a pool CA certificate, refusing with
// ErrNodeAuthEnabled if the pool already has one unless Append is set.
func PoolInstallCA(ctx context.Context, rpcClient UnaryInvoker, req *PoolInstallCAReq) (*PoolAddCAResp, error) {
	if req == nil {
		return nil, errors.New("nil PoolInstallCAReq")
	}
	// Parse for early sanity only; the server verifies provenance
	// against the DAOS root before installing.
	if _, err := parsePEMCACert(req.CertPEM); err != nil {
		return nil, err
	}
	if !req.Append {
		caResp, err := PoolGetCA(ctx, rpcClient, &PoolGetCAReq{ID: req.ID})
		if err != nil {
			return nil, errors.Wrap(err, "checking for an installed CA")
		}
		if n := len(caResp.Certs); n > 0 {
			return nil, errors.Wrapf(ErrNodeAuthEnabled, "%d CA(s) installed", n)
		}
	}
	resp, err := PoolAddCA(ctx, rpcClient, &PoolAddCAReq{ID: req.ID, CertPEM: req.CertPEM, NoEvict: req.NoEvict})
	if err != nil {
		return nil, errors.Wrap(err, "adding pool CA")
	}
	return resp, nil
}

// PoolSetupCertAuth generates (or imports, when CertPEM is supplied) and
// installs a pool CA.
func PoolSetupCertAuth(ctx context.Context, rpcClient UnaryInvoker, req *PoolSetupCertAuthReq) (*PoolSetupCertAuthResp, error) {
	var certPEM, keyPEM []byte

	if len(req.CertPEM) > 0 {
		certPEM = req.CertPEM
	} else {
		ca, err := PoolGenerateCA(ctx, rpcClient, &PoolGenerateCAReq{
			Validity:       req.Validity,
			ID:             req.ID,
			DaosCACertPath: req.DaosCACertPath,
			DaosCAKeyPath:  req.DaosCAKeyPath,
		})
		if err != nil {
			return nil, err
		}
		certPEM, keyPEM = ca.CertPEM, ca.KeyPEM
	}

	addResp, err := PoolInstallCA(ctx, rpcClient, &PoolInstallCAReq{
		NoEvict: req.NoEvict,
		ID:      req.ID,
		CertPEM: certPEM,
		Append:  req.Append,
	})
	if err != nil {
		return nil, err
	}

	return &PoolSetupCertAuthResp{
		HandlesEvicted: addResp.HandlesEvicted,
		PoolUUID:       addResp.PoolUUID,
		CACertPEM:      certPEM,
		CAKeyPEM:       keyPEM,
	}, nil
}

// PoolGenerateClientCertsReq contains the parameters for generating
// client certificates signed by a pool's CA.
type PoolGenerateClientCertsReq struct {
	CAKeyPath string
	Nodes     []string // CN = node:<name>
	Tenants   []string // CN = tenant:<name>; mutually exclusive with Nodes

	// Watermarks holds the pool's revocation watermarks. A cert for a
	// revoked CN is postdated past its watermark so it is valid from the
	// moment it is minted.
	Watermarks security.CertWatermarks
	Validity   time.Duration // zero means DefaultClientCertValidity; never past the CA's expiry
}

// ClientCert contains the generated certificate and key for a client.
type ClientCert struct {
	Name     string // the node or tenant name (without prefix)
	CN       string // the full CN (with prefix)
	CertPEM  []byte
	KeyPEM   []byte
	NotAfter time.Time
}

// PoolGenerateClientCerts generates certificates signed by a pool CA.
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
	// Truncate to match the precision of both the RFC3339 watermark and
	// the cert's ASN.1 NotBefore encoding; a full-precision comparison
	// can skip the bump and mint NotBefore == watermark, which is revoked.
	now := time.Now().UTC().Truncate(time.Second)
	for _, tgt := range targets {
		notBefore := now
		if wm, ok := req.Watermarks[tgt.cn]; ok && !notBefore.After(wm) {
			notBefore = wm.Add(time.Second)
		}
		validity := req.Validity
		if validity <= 0 {
			validity = DefaultClientCertValidity
		}
		certPEM, keyPEM, notAfter, err := generateClientCertAt(tgt.cn, caCert, caKey, notBefore, validity)
		if err != nil {
			return nil, errors.Wrapf(err, "generating cert for %s", tgt.cn)
		}
		results = append(results, &ClientCert{
			Name:     tgt.name,
			CN:       tgt.cn,
			CertPEM:  certPEM,
			KeyPEM:   keyPEM,
			NotAfter: notAfter,
		})
	}

	return results, nil
}

// generatePoolCA creates a pool-specific intermediate CA key pair signed
// by the DAOS CA.
func generatePoolCA(poolUUID uuid.UUID, daosCACert *x509.Certificate, daosCAKey interface{}, validity time.Duration) (certPEM, keyPEM []byte, notAfter time.Time, err error) {
	poolCAKey, err := ecdsa.GenerateKey(elliptic.P384(), rand.Reader)
	if err != nil {
		return nil, nil, notAfter, errors.Wrap(err, "generating pool CA key")
	}

	serialNumber, err := rand.Int(rand.Reader, new(big.Int).Lsh(big.NewInt(1), 128))
	if err != nil {
		return nil, nil, notAfter, errors.Wrap(err, "generating serial number")
	}

	now := time.Now()
	notAfter = clipNotAfter(now, validity, daosCACert)
	template := &x509.Certificate{
		SerialNumber: serialNumber,
		Subject: pkix.Name{
			CommonName:   security.PoolCACommonName(poolUUID),
			Organization: []string{"DAOS"},
		},
		NotBefore:             now,
		NotAfter:              notAfter,
		KeyUsage:              x509.KeyUsageCertSign | x509.KeyUsageCRLSign,
		BasicConstraintsValid: true,
		IsCA:                  true,
		MaxPathLen:            0,
		MaxPathLenZero:        true,
	}

	certDER, err := x509.CreateCertificate(rand.Reader, template, daosCACert, &poolCAKey.PublicKey, daosCAKey)
	if err != nil {
		return nil, nil, notAfter, errors.Wrap(err, "creating pool CA certificate")
	}

	certPEM = pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: certDER})

	keyDER, err := x509.MarshalPKCS8PrivateKey(poolCAKey)
	if err != nil {
		return nil, nil, notAfter, errors.Wrap(err, "marshaling pool CA key")
	}
	keyPEM = pem.EncodeToMemory(&pem.Block{Type: "PRIVATE KEY", Bytes: keyDER})

	return certPEM, keyPEM, notAfter, nil
}

// generateClientCert creates a client certificate signed by a pool CA.
// NotBefore is set to the current time.
func generateClientCert(cn string, caCert *x509.Certificate, caKey interface{}) (certPEM, keyPEM []byte, err error) {
	certPEM, keyPEM, _, err = generateClientCertAt(cn, caCert, caKey, time.Now().UTC(), DefaultClientCertValidity)
	return
}

// generateClientCertAt creates a client cert with an explicit NotBefore.
func generateClientCertAt(cn string, caCert *x509.Certificate, caKey interface{}, notBefore time.Time, validity time.Duration) (certPEM, keyPEM []byte, notAfter time.Time, err error) {
	key, err := ecdsa.GenerateKey(elliptic.P384(), rand.Reader)
	if err != nil {
		return nil, nil, notAfter, errors.Wrap(err, "generating key")
	}

	serialNumber, err := rand.Int(rand.Reader, new(big.Int).Lsh(big.NewInt(1), 128))
	if err != nil {
		return nil, nil, notAfter, errors.Wrap(err, "generating serial number")
	}

	notAfter = clipNotAfter(notBefore, validity, caCert)
	template := &x509.Certificate{
		SerialNumber: serialNumber,
		Subject: pkix.Name{
			CommonName:   cn,
			Organization: []string{"DAOS"},
		},
		NotBefore: notBefore,
		NotAfter:  notAfter,
		KeyUsage:  x509.KeyUsageDigitalSignature,
		ExtKeyUsage: []x509.ExtKeyUsage{
			x509.ExtKeyUsageClientAuth,
		},
	}

	certDER, err := x509.CreateCertificate(rand.Reader, template, caCert, &key.PublicKey, caKey)
	if err != nil {
		return nil, nil, notAfter, errors.Wrap(err, "creating certificate")
	}

	certPEM = pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: certDER})

	keyDER, err := x509.MarshalPKCS8PrivateKey(key)
	if err != nil {
		return nil, nil, notAfter, errors.Wrap(err, "marshaling key")
	}
	keyPEM = pem.EncodeToMemory(&pem.Block{Type: "PRIVATE KEY", Bytes: keyDER})

	return certPEM, keyPEM, notAfter, nil
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

// PoolIssueClientCertsReq contains the parameters for issuing client
// certificates for a pool.
type PoolIssueClientCertsReq struct {
	poolRequest
	ID        string   // pool UUID or label
	CAKeyPath string   // path to the pool CA private key
	Nodes     []string // CN = node:<name>
	Tenants   []string // CN = tenant:<name>; mutually exclusive with Nodes
	Replace   bool     // revoke each node's existing certificate first (node certs only)
	Validity  time.Duration
}

// PoolIssueClientCerts issues client certificates postdated past the pool's
// revocation watermarks, revoking the nodes first when Replace is set.
func PoolIssueClientCerts(ctx context.Context, rpcClient UnaryInvoker, req *PoolIssueClientCertsReq) ([]*ClientCert, error) {
	if req == nil {
		return nil, errors.New("nil PoolIssueClientCertsReq")
	}
	if (len(req.Nodes) == 0) == (len(req.Tenants) == 0) {
		return nil, errors.New("specify Nodes or Tenants, not both")
	}
	if req.Replace && len(req.Tenants) > 0 {
		return nil, errors.New("Replace applies to node certificates only")
	}

	if req.Replace {
		for _, name := range req.Nodes {
			if _, err := PoolRevokeClient(ctx, rpcClient, &PoolRevokeClientReq{
				ID:        req.ID,
				Node:      name,
				EvictMode: PoolRevokeEvictDefault,
			}); err != nil {
				return nil, errors.Wrapf(err, "revoking node %s", name)
			}
		}
	}

	watermarks, err := PoolGetCertWatermarks(ctx, rpcClient, &PoolGetCertWatermarksReq{ID: req.ID})
	if err != nil {
		return nil, errors.Wrap(err, "reading cert watermarks")
	}

	return PoolGenerateClientCerts(&PoolGenerateClientCertsReq{
		CAKeyPath:  req.CAKeyPath,
		Nodes:      req.Nodes,
		Tenants:    req.Tenants,
		Watermarks: watermarks,
		Validity:   req.Validity,
	})
}

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
	poolUUID, err := respPoolUUID(pbResp.GetPoolUuid())
	if err != nil {
		return nil, err
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

// PoolAddCAReq contains pool add-CA parameters.
type PoolAddCAReq struct {
	poolRequest
	ID      string // pool UUID or label
	CertPEM []byte // PEM-encoded CA certificate to append
	Replace bool   // clear existing bundle before append
	NoEvict bool   // keep existing handles when this enables node auth
}

// PoolAddCAResp carries the result of a PoolAddCA call.
type PoolAddCAResp struct {
	PoolUUID       uuid.UUID
	HandlesEvicted int32
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
	Node      string              // node name to revoke (mutually exclusive with Tenant)
	Tenant    string              // tenant name to revoke (mutually exclusive with Node)
	EvictMode PoolRevokeEvictMode // how to evict active handles
}

// PoolRevokeClientResp contains the result of a revoke-client operation.
type PoolRevokeClientResp struct {
	PoolUUID       uuid.UUID
	CN             string    // the revoked CN (with prefix)
	Watermark      time.Time // certs at or before this NotBefore are revoked
	HandlesEvicted int32     // number of active handles evicted
	EvictScope     string    // "machine" | "pool" | "none"
}

// PoolRevokeClient advances the pool's per-CN revocation watermark.
func PoolRevokeClient(ctx context.Context, rpcClient UnaryInvoker, req *PoolRevokeClientReq) (*PoolRevokeClientResp, error) {
	if req == nil {
		return nil, errors.New("nil PoolRevokeClientReq")
	}
	if (req.Node == "") == (req.Tenant == "") {
		return nil, errors.New("specify exactly one of Node or Tenant")
	}

	cn := security.CertCNPrefixNode + req.Node
	if req.Tenant != "" {
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

	poolUUID, err := respPoolUUID(pbResp.GetPoolUuid())
	if err != nil {
		return nil, err
	}
	return &PoolRevokeClientResp{
		PoolUUID:       poolUUID,
		CN:             cn,
		Watermark:      watermark.UTC(),
		HandlesEvicted: pbResp.GetHandlesEvictedCount(),
		EvictScope:     pbResp.GetEvictScope(),
	}, nil
}
