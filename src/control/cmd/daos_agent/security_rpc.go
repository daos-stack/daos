//
// (C) Copyright 2018-2024 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"fmt"
	"net"
	"os/user"
	"time"

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
		nodeCertDir string
	}

	// SecurityModule is the security drpc module struct
	SecurityModule struct {
		log            logging.Logger
		signCredential credSignerFn
		credCache      *credentialCache

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

	return &SecurityModule{
		log:            log,
		signCredential: credSigner,
		credCache:      credCache,
		config:         cfg,
	}
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

	var req auth.GetCredReq
	if len(body) > 0 {
		if err := proto.Unmarshal(body, &req); err != nil {
			m.log.Errorf("failed to unmarshal GetCredReq: %s", err)
			// Fall through with empty req for backward compat
		}
	}

	return m.getCredential(ctx, session, req.GetPoolId(), req.GetHandleUuid())
}

// getCredential generates a signed user credential based on the data attached to
// the Unix Domain Socket. If poolID is non-empty and pool node cert dir is
// configured, also looks up a node certificate for the pool and signs a
// proof-of-possession payload.
func (m *SecurityModule) getCredential(ctx context.Context, session *drpc.Session, poolID string, handleUUID []byte) ([]byte, error) {
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
		return m.credRespWithStatus(daos.MiscError)
	}

	signingKey, err := m.config.transport.PrivateKey()
	if err != nil {
		m.log.Errorf("%s: failed to get signing key: %s", info, err)
		// something is wrong with the cert config
		return m.credRespWithStatus(daos.BadCert)
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
			return m.credRespWithStatus(daos.MiscError)
		}
	}

	resp := &auth.GetCredResp{Cred: cred}

	// If pool ID provided and cert dir configured, look up node cert and sign PoP
	if poolID != "" && m.config.nodeCertDir != "" {
		cached, certPEM, pop, payload, err := getNodeCertAndPoPWithMeta(m.log, m.config.nodeCertDir, poolID, handleUUID)
		if err != nil {
			// Log but don't fail — server decides if cert is required
			m.log.Tracef("no node cert for pool %s: %s", poolID, err)
		} else {
			m.log.Tracef("attaching node cert for pool %s (CN=%s, expires=%s, %d bytes PoP)",
				poolID, cached.cert.Subject.CommonName,
				cached.cert.NotAfter.Format("2006-01-02"), len(pop))
			resp.NodeCert = certPEM
			resp.NodeCertPop = pop
			resp.NodeCertPayload = payload
		}
	}

	return drpc.Marshal(resp)
}

func (m *SecurityModule) credRespWithStatus(status daos.Status) ([]byte, error) {
	resp := &auth.GetCredResp{Status: int32(status)}
	return drpc.Marshal(resp)
}

// ID will return Security module ID
func (m *SecurityModule) ID() int32 {
	return daos.ModuleSecurityAgent
}
