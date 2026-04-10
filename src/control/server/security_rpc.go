//
// (C) Copyright 2019-2022 Intel Corporation.
// (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"crypto"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rsa"
	"crypto/sha256"
	"crypto/sha512"
	"crypto/x509"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"encoding/pem"
	"fmt"
	"math"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/security/auth"
)

// SecurityModule is the security drpc module struct
type SecurityModule struct {
	log          logging.Logger
	config       *security.TransportConfig
	maxClockSkew time.Duration
	// handleCache tracks recently-seen handle UUIDs for replay protection.
	handleCache   map[string]time.Time
	handleCacheMu sync.Mutex
}

// NewSecurityModule creates a new security module with a transport config
func NewSecurityModule(log logging.Logger, tc *security.TransportConfig) *SecurityModule {
	maxClockSkew := defaultMaxClockSkew
	if tc != nil && tc.PoolCertMaxClockSkew > 0 {
		maxClockSkew = tc.PoolCertMaxClockSkew
	}
	return &SecurityModule{
		log:          log,
		config:       tc,
		maxClockSkew: maxClockSkew,
		handleCache:  make(map[string]time.Time),
	}
}

func (m *SecurityModule) processValidateCredentials(body []byte) ([]byte, error) {
	req := &auth.ValidateCredReq{}
	err := proto.Unmarshal(body, req)
	if err != nil {
		return nil, drpc.UnmarshalingPayloadFailure()
	}

	cred := req.Cred
	if cred == nil || cred.GetToken() == nil || cred.GetVerifier() == nil {
		m.log.Error("malformed credential")
		return m.validateRespWithStatus(daos.InvalidInput)
	}

	var key crypto.PublicKey
	if m.config.AllowInsecure {
		key = nil
	} else {
		certName := fmt.Sprintf("%s.crt", cred.Origin)
		certPath := filepath.Join(m.config.ClientCertDir, certName)
		cert, err := security.LoadCertificate(certPath)
		if err != nil {
			m.log.Errorf("loading certificate %s failed: %v", certPath, err)
			return m.validateRespWithStatus(daos.NoCert)
		}
		key = cert.PublicKey
	}

	// Check our verifier
	err = auth.VerifyToken(key, cred.GetToken(), cred.GetVerifier().GetData())
	if err != nil {
		m.log.Errorf("cred verification failed: %v", err)
		return m.validateRespWithStatus(daos.NoPermission)
	}

	resp := &auth.ValidateCredResp{Token: cred.Token}
	responseBytes, err := proto.Marshal(resp)
	if err != nil {
		return nil, drpc.MarshalingFailure()
	}
	return responseBytes, nil
}

func (m *SecurityModule) validateRespWithStatus(status daos.Status) ([]byte, error) {
	return drpc.Marshal(&auth.ValidateCredResp{Status: int32(status)})
}

// HandleCall is the handler for calls to the SecurityModule
func (m *SecurityModule) HandleCall(_ context.Context, session *drpc.Session, method drpc.Method, body []byte) ([]byte, error) {
	switch method {
	case daos.MethodValidateCredentials:
		return m.processValidateCredentials(body)
	case daos.MethodValidateNodeCert:
		return m.processValidateNodeCert(body)
	default:
		return nil, drpc.UnknownMethodFailure()
	}
}

// ID will return Security module ID
func (m *SecurityModule) ID() int32 {
	return daos.ModuleSecurity
}

func (m *SecurityModule) String() string {
	return "server_security"
}

// GetMethod returns a helpful representation of the method matching the ID.
func (m *SecurityModule) GetMethod(id int32) (drpc.Method, error) {
	switch id {
	case daos.MethodValidateCredentials.ID():
		return daos.MethodValidateCredentials, nil
	case daos.MethodValidateNodeCert.ID():
		return daos.MethodValidateNodeCert, nil
	default:
		return nil, fmt.Errorf("invalid method ID %d for module %s", id, m.String())
	}
}

const (
	// popPayloadLen is the expected length of the PoP payload:
	// pool_uuid(16) + handle_uuid(16) + timestamp(8) = 40 bytes
	popPayloadLen = 40
	// defaultMaxClockSkew is the default maximum allowed clock skew for PoP timestamps.
	defaultMaxClockSkew = 5 * time.Minute
)

func (m *SecurityModule) processValidateNodeCert(body []byte) ([]byte, error) {
	req := &auth.ValidateNodeCertReq{}
	if err := proto.Unmarshal(body, req); err != nil {
		return nil, drpc.UnmarshalingPayloadFailure()
	}

	// Parse PEM node cert
	nodeCert, err := parsePEMCert(req.NodeCert)
	if err != nil {
		return m.rejectNodeCert(req, daos.BadCert,
			fmt.Sprintf("failed to parse node certificate: %v", err))
	}
	m.log.Tracef("validating node cert: pool=%s, CN=%s, issuer=%s, expires=%s",
		req.PoolId, nodeCert.Subject.CommonName, nodeCert.Issuer.CommonName,
		nodeCert.NotAfter.Format(time.RFC3339))

	// Parse pool CA bundle (one or more PEM-encoded CA certs)
	intermediates := x509.NewCertPool()
	rest := req.PoolCa
	nCAs := 0
	for len(rest) > 0 {
		var block *pem.Block
		block, rest = pem.Decode(rest)
		if block == nil {
			break
		}
		caCert, err := x509.ParseCertificate(block.Bytes)
		if err != nil {
			m.log.Errorf("failed to parse pool CA certificate in bundle: %v", err)
			continue
		}
		intermediates.AddCert(caCert)
		nCAs++
		m.log.Tracef("loaded pool CA cert: CN=%s, issuer=%s, expires=%s",
			caCert.Subject.CommonName, caCert.Issuer.CommonName,
			caCert.NotAfter.Format(time.RFC3339))
	}
	if nCAs == 0 {
		return m.rejectNodeCert(req, daos.BadCert,
			"no valid CA certificates found in pool CA bundle")
	}

	// Chain validation: node cert -> any pool CA in bundle -> DAOS CA
	daosCA, err := security.LoadCertificate(m.config.CARootPath)
	if err != nil {
		return m.rejectNodeCert(req, daos.NoCert,
			fmt.Sprintf("failed to load DAOS CA: %v", err))
	}
	m.log.Tracef("loaded DAOS CA cert: CN=%s, issuer=%s, expires=%s",
		daosCA.Subject.CommonName, daosCA.Issuer.CommonName,
		daosCA.NotAfter.Format(time.RFC3339))

	roots := x509.NewCertPool()
	roots.AddCert(daosCA)

	if _, err := nodeCert.Verify(x509.VerifyOptions{
		Roots:         roots,
		Intermediates: intermediates,
		KeyUsages:     []x509.ExtKeyUsage{x509.ExtKeyUsageClientAuth},
	}); err != nil {
		return m.rejectNodeCert(req, daos.BadCert,
			fmt.Sprintf("node cert chain validation failed: %v", err))
	}

	// Cross-validate cert CN against credential machine name.
	// Node-scoped certs (CertCNPrefixNode) must match the credential
	// machine name. Tenant-scoped certs (CertCNPrefixTenant) skip
	// this check because the CN is a tenant identifier, not a hostname.
	cn := nodeCert.Subject.CommonName
	switch {
	case strings.HasPrefix(cn, security.CertCNPrefixTenant):
		// No machine name validation for tenant certs.
	case strings.HasPrefix(cn, security.CertCNPrefixNode):
		if req.MachineName != "" {
			nodeName := strings.TrimPrefix(cn, security.CertCNPrefixNode)
			if nodeName != req.MachineName {
				return m.rejectNodeCert(req, daos.BadCert,
					fmt.Sprintf("cert CN %q does not match credential machine name %q",
						cn, req.MachineName))
			}
		}
	default:
		return m.rejectNodeCert(req, daos.BadCert,
			fmt.Sprintf("cert CN %q has no recognized prefix", cn))
	}

	// Per-CN revocation watermark check. A cert whose NotBefore is
	// strictly less than the watermark for its CN is considered revoked.
	// The watermarks blob is an opaque JSON object produced by the
	// control plane; the engine passes it through verbatim.
	if len(req.CertWatermarks) > 0 {
		watermarks, err := parseCertWatermarks(req.CertWatermarks)
		if err != nil {
			return m.rejectNodeCert(req, daos.BadCert,
				fmt.Sprintf("failed to parse cert watermarks: %v", err))
		}
		if wm, ok := watermarks[cn]; ok && nodeCert.NotBefore.Before(wm) {
			return m.rejectNodeCert(req, daos.BadCert,
				fmt.Sprintf("cert for %q has been revoked (NotBefore=%s < watermark=%s)",
					cn, nodeCert.NotBefore.Format(time.RFC3339),
					wm.Format(time.RFC3339)))
		}
	}

	// Validate PoP payload
	if len(req.NodeCertPayload) != popPayloadLen {
		return m.rejectNodeCert(req, daos.InvalidInput,
			fmt.Sprintf("invalid PoP payload length: %d (expected %d)",
				len(req.NodeCertPayload), popPayloadLen))
	}

	// Check timestamp skew
	ts := int64(binary.BigEndian.Uint64(req.NodeCertPayload[32:40]))
	skew := time.Duration(math.Abs(float64(time.Now().Unix()-ts))) * time.Second
	if skew > m.maxClockSkew {
		return m.rejectNodeCert(req, daos.NoPermission,
			fmt.Sprintf("PoP timestamp skew too large: %v (max %v)",
				skew, m.maxClockSkew))
	}

	// Verify PoP signature
	if err := verifyPoP(nodeCert.PublicKey, req.NodeCertPayload, req.NodeCertPop); err != nil {
		return m.rejectNodeCert(req, daos.NoPermission,
			fmt.Sprintf("PoP signature verification failed: %v", err))
	}

	// Check for replay (duplicate handle UUID within skew window)
	handleKey := hex.EncodeToString(req.NodeCertPayload[16:32])
	m.handleCacheMu.Lock()
	if expiry, found := m.handleCache[handleKey]; found && time.Now().Before(expiry) {
		m.handleCacheMu.Unlock()
		return m.rejectNodeCert(req, daos.NoPermission,
			fmt.Sprintf("replay detected: handle UUID %s already seen", handleKey))
	}
	m.handleCache[handleKey] = time.Now().Add(m.maxClockSkew * 2)
	// Lazy cleanup of expired entries
	for k, v := range m.handleCache {
		if time.Now().After(v) {
			delete(m.handleCache, k)
		}
	}
	m.handleCacheMu.Unlock()

	m.log.Debugf("node cert validated: pool=%s, CN=%s, issuer=%s, skew=%v",
		req.PoolId, nodeCert.Subject.CommonName, nodeCert.Issuer.CommonName, skew)

	return drpc.Marshal(&auth.ValidateNodeCertResp{Status: 0})
}

// rejectNodeCert logs the rejection reason and returns a marshaled
// ValidateNodeCertResp carrying both the status and a human-readable
// detail string the engine can surface to the caller.
func (m *SecurityModule) rejectNodeCert(req *auth.ValidateNodeCertReq, status daos.Status, detail string) ([]byte, error) {
	m.log.Errorf("node cert rejected (pool=%s, status=%s): %s",
		req.PoolId, status, detail)
	return drpc.Marshal(&auth.ValidateNodeCertResp{
		Status: int32(status),
		Detail: detail,
	})
}

func parsePEMCert(data []byte) (*x509.Certificate, error) {
	block, _ := pem.Decode(data)
	if block == nil {
		return nil, fmt.Errorf("no PEM data found")
	}
	return x509.ParseCertificate(block.Bytes)
}

// parseCertWatermarks decodes the JSON-encoded cert revocation watermarks
// blob (a flat map of CN → RFC3339 timestamp) into a CN-keyed time.Time map.
// The blob is produced and monotonically advanced by the control-plane
// revoke-client flow and shipped through the engine untouched.
func parseCertWatermarks(data []byte) (map[string]time.Time, error) {
	raw := make(map[string]string)
	if err := json.Unmarshal(data, &raw); err != nil {
		return nil, fmt.Errorf("unmarshal watermarks: %w", err)
	}
	out := make(map[string]time.Time, len(raw))
	for cn, ts := range raw {
		t, err := time.Parse(time.RFC3339, ts)
		if err != nil {
			return nil, fmt.Errorf("watermark for %q: %w", cn, err)
		}
		out[cn] = t
	}
	return out, nil
}

// verifyPoP verifies the proof-of-possession signature against the node cert's
// public key. The algorithm is determined by the key type.
func verifyPoP(pub crypto.PublicKey, payload, sig []byte) error {
	switch k := pub.(type) {
	case *rsa.PublicKey:
		h := sha512.Sum512(payload)
		return rsa.VerifyPSS(k, crypto.SHA512, h[:], sig, &rsa.PSSOptions{
			SaltLength: rsa.PSSSaltLengthEqualsHash,
			Hash:       crypto.SHA512,
		})
	case *ecdsa.PublicKey:
		var hash []byte
		switch k.Curve {
		case elliptic.P256():
			h := sha256.Sum256(payload)
			hash = h[:]
		case elliptic.P384():
			h := sha512.Sum384(payload)
			hash = h[:]
		default:
			return fmt.Errorf("unsupported ECDSA curve: %v", k.Curve.Params().Name)
		}
		if !ecdsa.VerifyASN1(k, hash, sig) {
			return fmt.Errorf("ECDSA signature verification failed")
		}
		return nil
	default:
		return fmt.Errorf("unsupported public key type: %T", pub)
	}
}
