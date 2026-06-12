//
// (C) Copyright 2019-2022 Intel Corporation.
// (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"bytes"
	"context"
	"crypto"
	"crypto/x509"
	"encoding/binary"
	"encoding/hex"
	"encoding/pem"
	"fmt"
	"math"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"github.com/google/uuid"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/security/auth"
)

// handleCacheMaxSize bounds the replay cache; reaching it is an attack signal.
const handleCacheMaxSize = 65536

// SecurityModule is the security drpc module struct
type SecurityModule struct {
	log          logging.Logger
	config       *security.TransportConfig
	maxClockSkew time.Duration
	// handleCache: (pool, handle) -> insert time; entries expire after 2*maxClockSkew.
	handleCache   map[string]time.Time
	handleCacheMu sync.Mutex
}

// NewSecurityModule creates a new security module with a transport config
func NewSecurityModule(log logging.Logger, tc *security.TransportConfig) *SecurityModule {
	maxClockSkew := security.NotBeforeSkewTolerance
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

func (m *SecurityModule) processValidateNodeCert(body []byte) ([]byte, error) {
	req := &auth.ValidateNodeCertReq{}
	if err := proto.Unmarshal(body, req); err != nil {
		return nil, drpc.UnmarshalingPayloadFailure()
	}

	nodeCert, err := parsePEMCert(req.NodeCert)
	if err != nil {
		return m.rejectNodeCert(req, daos.BadCert,
			fmt.Sprintf("failed to parse node certificate: %v", err))
	}
	m.log.Tracef("validating node cert: pool=%s, CN=%s, issuer=%s, expires=%s",
		req.PoolId, nodeCert.Subject.CommonName, nodeCert.Issuer.CommonName,
		nodeCert.NotAfter.Format(time.RFC3339))

	intermediates := x509.NewCertPool()
	rest := req.PoolCa
	nCAs := 0
	for len(rest) > 0 {
		var block *pem.Block
		block, rest = pem.Decode(rest)
		if block == nil {
			break
		}
		if block.Type != "CERTIFICATE" {
			return m.rejectNodeCert(req, daos.BadCert,
				fmt.Sprintf("unexpected PEM block type %q in pool CA bundle", block.Type))
		}
		caCert, err := x509.ParseCertificate(block.Bytes)
		if err != nil {
			return m.rejectNodeCert(req, daos.BadCert,
				fmt.Sprintf("malformed cert in pool CA bundle: %v", err))
		}
		intermediates.AddCert(caCert)
		nCAs++
		m.log.Tracef("loaded pool CA cert: CN=%s, issuer=%s, expires=%s",
			caCert.Subject.CommonName, caCert.Issuer.CommonName,
			caCert.NotAfter.Format(time.RFC3339))
	}
	if nCAs == 0 {
		return m.rejectNodeCert(req, daos.BadCert, "pool CA bundle contains no certificates")
	}

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

	cn := nodeCert.Subject.CommonName
	switch {
	case strings.HasPrefix(cn, security.CertCNPrefixTenant):
		// Tenant key compromise on any node = full tenant compromise;
		// containment is via watermark revocation.
	case strings.HasPrefix(cn, security.CertCNPrefixNode):
		// Defense-in-depth: reject empty machine name explicitly even
		// though the engine's get_sec_origin_for_token also rejects it.
		if req.MachineName == "" {
			return m.rejectNodeCert(req, daos.InvalidInput,
				fmt.Sprintf("cert CN %q is node-scoped but credential machine name is empty",
					cn))
		}
		nodeName := strings.TrimPrefix(cn, security.CertCNPrefixNode)
		if nodeName != req.MachineName {
			return m.rejectNodeCert(req, daos.BadCert,
				fmt.Sprintf("cert CN %q does not match credential machine name %q",
					cn, req.MachineName))
		}
	default:
		return m.rejectNodeCert(req, daos.BadCert,
			fmt.Sprintf("cert CN %q has no recognized prefix", cn))
	}

	if len(req.CertWatermarks) > 0 {
		watermarks, err := security.DecodeCertWatermarks(req.CertWatermarks)
		if err != nil {
			return m.rejectNodeCert(req, daos.BadCert,
				fmt.Sprintf("failed to parse cert watermarks: %v", err))
		}
		if wm, ok := watermarks[cn]; ok && !nodeCert.NotBefore.After(wm) {
			return m.rejectNodeCert(req, daos.BadCert,
				fmt.Sprintf("cert for %q revoked (NotBefore=%s, watermark=%s)",
					cn, nodeCert.NotBefore.Format(time.RFC3339),
					wm.Format(time.RFC3339)))
		}
	}

	if len(req.NodeCertPayload) != security.PoPPayloadLen {
		return m.rejectNodeCert(req, daos.InvalidInput,
			fmt.Sprintf("invalid PoP payload length: %d (expected %d)",
				len(req.NodeCertPayload), security.PoPPayloadLen))
	}

	// Cross-pool replay defense: a node cert shared across pools must not let a PoP
	// captured for pool A authenticate a connect to pool B.
	reqPoolUUID, err := uuid.Parse(req.PoolId)
	if err != nil {
		return m.rejectNodeCert(req, daos.InvalidInput,
			fmt.Sprintf("invalid pool UUID: %v", err))
	}
	if !bytes.Equal(req.NodeCertPayload[0:16], reqPoolUUID[:]) {
		return m.rejectNodeCert(req, daos.NoPermission,
			"PoP payload pool UUID does not match request pool ID")
	}

	ts := int64(binary.BigEndian.Uint64(req.NodeCertPayload[32:40]))
	skew := time.Duration(math.Abs(float64(time.Now().Unix()-ts))) * time.Second
	if skew > m.maxClockSkew {
		return m.rejectNodeCert(req, daos.NoPermission,
			fmt.Sprintf("PoP timestamp skew too large: %v (max %v)",
				skew, m.maxClockSkew))
	}

	if err := security.VerifyPoP(nodeCert.PublicKey, req.NodeCertPayload, req.NodeCertPop); err != nil {
		return m.rejectNodeCert(req, daos.NoPermission,
			fmt.Sprintf("PoP signature verification failed: %v", err))
	}

	// Replay check keyed on (pool, handle) to match the PoP payload binding.
	handleKey := hex.EncodeToString(req.NodeCertPayload[0:32])
	m.handleCacheMu.Lock()
	if expiry, found := m.handleCache[handleKey]; found && time.Now().Before(expiry) {
		m.handleCacheMu.Unlock()
		return m.rejectNodeCert(req, daos.NoPermission,
			fmt.Sprintf("replay detected: handle %s already seen", handleKey))
	}
	if len(m.handleCache) >= handleCacheMaxSize {
		now := time.Now()
		for k, v := range m.handleCache {
			if now.After(v) {
				delete(m.handleCache, k)
			}
		}
		if len(m.handleCache) >= handleCacheMaxSize {
			for k := range m.handleCache {
				delete(m.handleCache, k)
				break
			}
		}
	}
	m.handleCache[handleKey] = time.Now().Add(m.maxClockSkew * 2)
	m.handleCacheMu.Unlock()

	m.log.Debugf("node cert validated: pool=%s, CN=%s, issuer=%s, skew=%v",
		req.PoolId, nodeCert.Subject.CommonName, nodeCert.Issuer.CommonName, skew)

	return drpc.Marshal(&auth.ValidateNodeCertResp{Status: 0})
}

// rejectNodeCert logs and returns a ValidateNodeCertResp carrying status + detail.
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
