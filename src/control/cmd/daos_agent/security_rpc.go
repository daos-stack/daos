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
	"slices"
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
	credSignerFn func(context.Context, logging.Logger, auth.CredentialRequest) (*auth.Credential, error)

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
		infoCache   *InfoCache
		sys         string
	}

	// SecurityModule is the security drpc module struct
	SecurityModule struct {
		log            logging.Logger
		signCredential credSignerFn
		credCache      *credentialCache
		config         *securityConfig
		infoCache      *InfoCache
	}
)

var _ cache.ExpirableItem = (*cachedCredential)(nil)

func getCredReq(reqb []byte) (*auth.GetCredReq, error) {
	credReq := new(auth.GetCredReq)

	reqbSize := len(reqb)
	if reqbSize == 0 {
		credReq.Flavor = auth.AuthSysCredentialFactory{}.GetAuthFlavor()
	} else {
		if err := proto.Unmarshal(reqb, credReq); err != nil {
			return nil, drpc.UnmarshalingPayloadFailure()
		}
	}

	return credReq, nil
}

// Wrapper function, helps with fulfilling cache interface.
func credentialRequestGetSigned(ctx context.Context, log logging.Logger, req auth.CredentialRequest) (*auth.Credential, error) {
	return req.GetSignedCredential(log, ctx)
}

// NewSecurityModule creates a new module with the given initialized TransportConfig.
func NewSecurityModule(log logging.Logger, cfg *securityConfig) *SecurityModule {
	var credCache *credentialCache
	credSigner := credentialRequestGetSigned
	if cfg.credentials.CacheExpiration > 0 {
		credCache = &credentialCache{
			log:          log,
			cache:        cache.NewItemCache(log),
			credLifetime: cfg.credentials.CacheExpiration,
			cacheMissFn:  credentialRequestGetSigned,
		}
		credSigner = credCache.getSignedCredential
		log.Noticef("credential cache enabled (entry lifetime: %s)", cfg.credentials.CacheExpiration)
	}

	return &SecurityModule{
		log:            log,
		signCredential: credSigner,
		credCache:      credCache,
		config:         cfg,
		infoCache:      cfg.infoCache,
	}
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

func (cc *credentialCache) getSignedCredential(ctx context.Context, log logging.Logger, req auth.CredentialRequest) (*auth.Credential, error) {
	key := req.GetKey()

	createItem := func() (cache.Item, error) {
		cc.log.Tracef("cache miss for %s", key)
		cred, err := cc.cacheMissFn(ctx, log, req)
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

// HandleCall is the handler for calls to the SecurityModule
func (m *SecurityModule) HandleCall(ctx context.Context, session *drpc.Session, method drpc.Method, reqb []byte) ([]byte, error) {
	credReq, err := getCredReq(reqb)
	if err != nil {
		return nil, errors.Wrap(err, "failed to parse request body")
	}

	switch method {
	case daos.MethodRequestCredentials:
		validAuthFlavors, err := m.retrieveAuthFromServer(ctx)
		if err != nil || len(validAuthFlavors) == 0 {
			return nil, errors.Wrap(err, "error in retrieving auth flavors from server")
		}

		if !slices.Contains(validAuthFlavors, credReq.Flavor) {
			return nil, errors.Errorf("invalid authentication method: the method requested is not allowed by the server configuration.")
		}
		return m.getCredential(ctx, session, credReq)
	case daos.MethodRequestValidFlavors:
		return m.getValidAuthFlavors(ctx)
	}

	return nil, drpc.UnknownMethodFailure()
}

func (m *SecurityModule) retrieveAuthFromServer(ctx context.Context) ([]auth.Flavor, error) {
	resp, err := m.infoCache.GetAttachInfo(ctx, m.config.sys)
	if err != nil {
		return nil, errors.Wrap(err, "failed to get attach info")
	}

	validAuthFlavors := make([]auth.Flavor, len(resp.ValidAuthFlavors))
	for i := 0; i < len(resp.ValidAuthFlavors); i++ {
		validAuthFlavors[i] = auth.Flavor(resp.ValidAuthFlavors[i])
	}

	if len(validAuthFlavors) == 0 {
		return nil, errors.Errorf("failed to receive valid authentication flavors from server.")
	}

	return validAuthFlavors, nil
}

// getCredentials generates a signed user credential based on the authentication method requested.
func (m *SecurityModule) getCredential(ctx context.Context, session *drpc.Session, credReq *auth.GetCredReq) ([]byte, error) {
	signingKey, err := m.config.transport.PrivateKey()
	if err != nil {
		m.log.Errorf("failed to get signing key: %s", err)
		// something is wrong with the cert config
		return m.credRespWithStatus(daos.BadCert)
	}

	req, err := auth.FlavorToFactory[credReq.Flavor].Init(m.log, m.config.credentials, session, credReq.Data, signingKey)
	if err != nil {
		if errors.Is(err, daos.MiscError) {
			return m.credRespWithStatus(err.(daos.Status))
		}
		m.log.Errorf("Unable to get credentials for client socket: %s", err)
		return nil, err
	}

	cred, err := m.signCredential(ctx, m.log, req)
	if err != nil {
		m.log.Errorf("failed to get user credential: %s", err)
		return m.credRespWithStatus(daos.FailedSign)
	}

	resp := &auth.GetCredResp{Cred: cred}
	return drpc.Marshal(resp)
}

func (m *SecurityModule) credRespWithStatus(status daos.Status) ([]byte, error) {
	resp := &auth.GetCredResp{Status: int32(status)}
	return drpc.Marshal(resp)
}

func (m *SecurityModule) getValidAuthFlavors(ctx context.Context) ([]byte, error) {
	validAuthFlavors, err := m.retrieveAuthFromServer(ctx)
	if err != nil {
		return nil, errors.Wrap(err, "error in retrieving auth flavors from server")
	}

	resp := &auth.GetValidFlavorsResp{ValidAuthFlavors: validAuthFlavors}
	return drpc.Marshal(resp)
}

// GetMethod gets the corresponding Method for a method ID.
func (m *SecurityModule) GetMethod(id int32) (drpc.Method, error) {
	if id == daos.MethodRequestCredentials.ID() {
		return daos.MethodRequestCredentials, nil
	} else if id == daos.MethodRequestValidFlavors.ID() {
		return daos.MethodRequestValidFlavors, nil
	}

	return nil, fmt.Errorf("invalid method ID %d for module %s", id, m.String())
}

func (m *SecurityModule) String() string {
	return "agent_security"
}

// ID will return Security module ID
func (m *SecurityModule) ID() int32 {
	return daos.ModuleSecurityAgent
}
