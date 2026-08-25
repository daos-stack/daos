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
	"fmt"
	"path/filepath"
	"sync"
	"time"

	"github.com/google/uuid"
	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/security/auth"
)

// handleCacheMaxSize bounds the replay cache
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

	poolUUID, err := uuid.FromBytes(req.GetPoolUuid())
	if err != nil {
		return m.rejectNodeCert(req, daos.InvalidInput,
			fmt.Sprintf("invalid pool UUID (%d bytes)", len(req.GetPoolUuid())))
	}

	daosCA, err := security.LoadCertificate(m.config.CARootPath)
	if err != nil {
		return m.rejectNodeCert(req, daos.NoCert,
			fmt.Sprintf("failed to load DAOS CA: %v", err))
	}

	p := &security.NodeCertPresentation{
		Root:        daosCA,
		PoolCA:      req.PoolCa,
		Cert:        req.NodeCert,
		PoPSig:      req.PopSig,
		PoPPayload:  req.PopPayload,
		PoolUUID:    poolUUID,
		MachineName: req.MachineName,
		Watermarks:  req.CertWatermarks,
		MaxSkew:     m.maxClockSkew,
	}
	payload, err := p.Validate()
	if err != nil {
		return m.rejectNodeCert(req, validationStatus(err), err.Error())
	}

	// Replay check keyed on the payload's (pool, handle) binding.
	replayKey := payload.ReplayKey()
	m.handleCacheMu.Lock()
	if expiry, found := m.handleCache[replayKey]; found && time.Now().Before(expiry) {
		m.handleCacheMu.Unlock()
		return m.rejectNodeCert(req, daos.NoPermission,
			fmt.Sprintf("replay detected: handle %s already seen", replayKey))
	}
	if len(m.handleCache) >= handleCacheMaxSize {
		now := time.Now()
		for k, v := range m.handleCache {
			if now.After(v) {
				delete(m.handleCache, k)
			}
		}
		// Evicting a live entry would let its proof be replayed; fail
		// closed instead.
		if len(m.handleCache) >= handleCacheMaxSize {
			m.handleCacheMu.Unlock()
			return m.rejectNodeCert(req, daos.TryAgain,
				"replay cache full of unexpired entries; possible replay flood")
		}
	}
	m.handleCache[replayKey] = time.Now().Add(m.maxClockSkew * 2)
	m.handleCacheMu.Unlock()

	m.log.Debugf("node cert validated: pool=%s, handle=%s", poolUUID, payload.HandleID())

	return drpc.Marshal(&auth.ValidateNodeCertResp{Status: 0})
}

// validationStatus maps the security package's sentinel errors to wire statuses.
func validationStatus(err error) daos.Status {
	switch {
	case errors.Is(err, security.ErrInvalidInput):
		return daos.InvalidInput
	case errors.Is(err, security.ErrCertInvalid), errors.Is(err, security.ErrCertRevoked):
		return daos.BadCert
	case errors.Is(err, security.ErrPoPInvalid), errors.Is(err, security.ErrPoPStale):
		return daos.NoPermission
	default:
		return daos.MiscError
	}
}

// rejectNodeCert logs and returns a ValidateNodeCertResp carrying status + detail.
func (m *SecurityModule) rejectNodeCert(req *auth.ValidateNodeCertReq, status daos.Status, detail string) ([]byte, error) {
	m.log.Errorf("node cert rejected (pool=%x, status=%s): %s",
		req.GetPoolUuid(), status, detail)
	return drpc.Marshal(&auth.ValidateNodeCertResp{
		Status: int32(status),
		Detail: detail,
	})
}
