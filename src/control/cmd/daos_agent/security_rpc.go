//
// (C) Copyright 2018-2024 Intel Corporation.
// (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"fmt"
	"net"
	"os"
	"os/user"
	"time"

	"github.com/google/uuid"
	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/cache"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/security/auth"
)

type (
	// credSignerFn defines the function signature for signing credentials.
	credSignerFn func(context.Context, *auth.CredentialRequest) (*auth.Credential, error)

	// credentialCache implements a cache for signed credentials.
	credentialCache struct {
		log          logging.Logger
		cache        *cache.ItemCache
		credLifetime time.Duration
		cacheMissFn  credSignerFn
	}

	// cachedCredential wraps a cached credential and implements the cache.ExpirableItem interface.
	cachedCredential struct {
		cacheItem
		key       string
		expiredAt time.Time
		cred      *auth.Credential
	}

	// securityConfig defines configuration parameters for SecurityModule.
	securityConfig struct {
		credentials *security.CredentialConfig
		transport   *security.TransportConfig
		nodeCertDir string // resolved path; used when pool auth is enabled
	}

	// SecurityModule is the security drpc module struct
	SecurityModule struct {
		log            logging.Logger
		signCredential credSignerFn
		credCache      *credentialCache
		nodeCertLoader *security.NodeCertLoader

		config *securityConfig
	}
)

var _ cache.ExpirableItem = (*cachedCredential)(nil)

// NewSecurityModule creates a new module with the given initialized TransportConfig.
func NewSecurityModule(log logging.Logger, cfg *securityConfig) *SecurityModule {
	var credCache *credentialCache
	credSigner := auth.GetSignedCredential
	if cfg.credentials.CacheExpiration > 0 {
		credCache = &credentialCache{
			log:          log,
			cache:        cache.NewItemCache(log),
			credLifetime: cfg.credentials.CacheExpiration,
			cacheMissFn:  auth.GetSignedCredential,
		}
		credSigner = credCache.getSignedCredential
		log.Noticef("credential cache enabled (entry lifetime: %s)", cfg.credentials.CacheExpiration)
	}

	mod := &SecurityModule{
		log:            log,
		signCredential: credSigner,
		credCache:      credCache,
		config:         cfg,
	}
	if cfg.credentials.PoolAuthEnabled {
		mod.nodeCertLoader = security.NewNodeCertLoader(cfg.nodeCertDir)
	}
	return mod
}

func credReqKey(req *auth.CredentialRequest) string {
	return fmt.Sprintf("%d:%d:%s", req.DomainInfo.Uid(), req.DomainInfo.Gid(), req.DomainInfo.Ctx())
}

// Key returns the key for the cached credential.
func (cred *cachedCredential) Key() string {
	if cred == nil {
		return ""
	}

	return cred.key
}

// IsExpired returns true if the cached credential is expired.
func (cred *cachedCredential) IsExpired() bool {
	if cred == nil || cred.cred == nil || cred.expiredAt.IsZero() {
		return true
	}

	return time.Now().After(cred.expiredAt)
}

func (cc *credentialCache) getSignedCredential(ctx context.Context, req *auth.CredentialRequest) (*auth.Credential, error) {
	key := credReqKey(req)

	createItem := func() (cache.Item, error) {
		cc.log.Tracef("cache miss for %s", key)
		cred, err := cc.cacheMissFn(ctx, req)
		if err != nil {
			return nil, err
		}
		cc.log.Tracef("getting credential for %s", key)
		return newCachedCredential(key, cred, cc.credLifetime)
	}

	item, release, err := cc.cache.GetOrCreate(ctx, key, createItem)
	if err != nil {
		return nil, errors.Wrap(err, "getting cached credential from cache")
	}
	defer release()

	cachedCred, ok := item.(*cachedCredential)
	if !ok {
		return nil, errors.New("invalid cached credential")
	}

	return cachedCred.cred, nil
}

func newCachedCredential(key string, cred *auth.Credential, lifetime time.Duration) (*cachedCredential, error) {
	if cred == nil {
		return nil, errors.New("credential is nil")
	}

	return &cachedCredential{
		key:       key,
		cred:      cred,
		expiredAt: time.Now().Add(lifetime),
	}, nil
}

// GetMethod gets the corresponding Method for a method ID.
func (m *SecurityModule) GetMethod(id int32) (drpc.Method, error) {
	if id == daos.MethodRequestCredentials.ID() {
		return daos.MethodRequestCredentials, nil
	}

	return nil, fmt.Errorf("invalid method ID %d for module %s", id, m.String())
}

func (m *SecurityModule) String() string {
	return "agent_security"
}

// HandleCall is the handler for calls to the SecurityModule
func (m *SecurityModule) HandleCall(ctx context.Context, session *drpc.Session, method drpc.Method, body []byte) ([]byte, error) {
	if method != daos.MethodRequestCredentials {
		return nil, drpc.UnknownMethodFailure()
	}

	// An empty body requests a plain credential: pre-node-auth clients
	// always send one, and current clients do too outside pool connect.
	if len(body) == 0 {
		return m.getCredential(ctx, session)
	}

	var req auth.GetCredReq
	if err := proto.Unmarshal(body, &req); err != nil {
		m.log.Errorf("failed to unmarshal GetCredReq: %s", err)
		return nil, drpc.UnmarshalingPayloadFailure()
	}
	return m.getPoolCredential(ctx, session, req.GetPoolUuid(), req.GetHandleUuid())
}

// getCredential generates a signed user credential from the Unix Domain
// Socket peer data.
func (m *SecurityModule) getCredential(ctx context.Context, session *drpc.Session) ([]byte, error) {
	resp, err := m.buildCredResp(ctx, session)
	if err != nil {
		return nil, err
	}
	return drpc.Marshal(resp)
}

// getPoolCredential generates a signed user credential and, when pool auth
// is enabled, attaches the pool's node cert and proof-of-possession.
func (m *SecurityModule) getPoolCredential(ctx context.Context, session *drpc.Session, poolBytes, handleBytes []byte) ([]byte, error) {
	poolUUID, err := uuid.FromBytes(poolBytes)
	if err != nil {
		m.log.Errorf("pool credential request with invalid pool UUID (%d bytes)", len(poolBytes))
		return m.credRespWithStatus(daos.InvalidInput)
	}
	handleUUID, err := uuid.FromBytes(handleBytes)
	if err != nil {
		m.log.Errorf("pool credential request with invalid handle UUID (%d bytes)", len(handleBytes))
		return m.credRespWithStatus(daos.InvalidInput)
	}

	resp, err := m.buildCredResp(ctx, session)
	if err != nil {
		return nil, err
	}

	if resp.Status == 0 && m.config.credentials.PoolAuthEnabled {
		if err := m.attachNodeCert(resp, poolUUID, handleUUID); err != nil {
			m.log.Errorf("failed to attach node cert for pool %s: %s", poolUUID, err)
			return m.credRespWithStatus(daos.BadCert)
		}
	}

	return drpc.Marshal(resp)
}

// buildCredResp signs a credential for the socket peer. Peer, key, and
// signing failures are reported as a status-only response.
func (m *SecurityModule) buildCredResp(ctx context.Context, session *drpc.Session) (*auth.GetCredResp, error) {
	if session == nil {
		return nil, drpc.NewFailureWithMessage("session is nil")
	}

	uConn, ok := session.Conn.(*net.UnixConn)
	if !ok {
		return nil, drpc.NewFailureWithMessage("connection is not a unix socket")
	}

	info, err := security.DomainInfoFromUnixConn(m.log, uConn)
	if err != nil {
		m.log.Errorf("Unable to get credentials for client socket: %s", err)
		return &auth.GetCredResp{Status: int32(daos.MiscError)}, nil
	}

	signingKey, err := m.config.transport.PrivateKey()
	if err != nil {
		m.log.Errorf("%s: failed to get signing key: %s", info, err)
		// something is wrong with the cert config
		return &auth.GetCredResp{Status: int32(daos.BadCert)}, nil
	}

	req := auth.NewCredentialRequest(info, signingKey)
	cred, err := m.signCredential(ctx, req)
	if err != nil {
		if err := func() error {
			if !errors.Is(err, user.UnknownUserIdError(info.Uid())) {
				return err
			}

			mu := m.config.credentials.ClientUserMap.Lookup(info.Uid())
			if mu == nil {
				return user.UnknownUserIdError(info.Uid())
			}

			req.WithUserAndGroup(mu.User, mu.Group, mu.Groups...)
			cred, err = m.signCredential(ctx, req)
			if err != nil {
				return err
			}

			return nil
		}(); err != nil {
			m.log.Errorf("%s: failed to get user credential: %s", info, err)
			return &auth.GetCredResp{Status: int32(daos.MiscError)}, nil
		}
	}

	return &auth.GetCredResp{Cred: cred}, nil
}

// attachNodeCert adds the pool's node cert and proof-of-possession to the
// response. If the node cert is not available, it is not considered an error
// as we can't know whether the pool requires it until the client attempts to connect.
func (m *SecurityModule) attachNodeCert(resp *auth.GetCredResp, poolUUID, handleUUID uuid.UUID) error {
	// Must use the same source as the AUTH_SYS machine name; revoke-by-CN depends on it.
	machine, err := auth.GetMachineName()
	if err != nil {
		return errors.Wrap(err, "getting machine name")
	}

	cert, pop, payload, err := m.nodeCertLoader.CertAndPoP(m.log, poolUUID, handleUUID, machine)
	if errors.Is(err, os.ErrNotExist) {
		m.log.Tracef("no node cert for pool %s: %s", poolUUID, err)
		return nil
	} else if err != nil {
		return err
	}

	m.log.Tracef("attaching node cert for pool %s (CN=%s, expires=%s, %d bytes PoP)",
		poolUUID, cert.Cert.Subject.CommonName,
		cert.Cert.NotAfter.Format("2006-01-02"), len(pop))
	resp.NodeCert = cert.PEM
	resp.PopSig = pop
	resp.PopPayload = payload
	return nil
}

func (m *SecurityModule) credRespWithStatus(status daos.Status) ([]byte, error) {
	resp := &auth.GetCredResp{Status: int32(status)}
	return drpc.Marshal(resp)
}

// ID will return Security module ID
func (m *SecurityModule) ID() int32 {
	return daos.ModuleSecurityAgent
}
