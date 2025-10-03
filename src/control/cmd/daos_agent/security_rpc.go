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
	"time"

	"github.com/pkg/errors"

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
	}

	// SecurityModule is the security drpc module struct
	SecurityModule struct {
		log              logging.Logger
		signCredential   credSignerFn
		credCache        *credentialCache
		config           *securityConfig
		validAuthMethods AuthValidSet
	}

	authArgs struct {
		tag     auth.AuthTag
		reqBody []byte
	}

	AuthValidSet map[auth.AuthTag]bool
)

var _ cache.ExpirableItem = (*cachedCredential)(nil)

func getValidAuthMethods(cfg *securityConfig) (AuthValidSet, error) {
	var validAuthMethods = make(AuthValidSet)
	for _, authMethodString := range cfg.credentials.ValidAuthMethods {
		if len(authMethodString) != auth.AuthTagSize {
			return nil, fmt.Errorf("found authentication method `%s`, which is not the expected length %d", authMethodString, auth.AuthTagSize)
		}
		method := (auth.AuthTag)([]byte(authMethodString))
		_, found := auth.AuthRegister[method]
		if !found {
			return nil, fmt.Errorf("found authentication method `%s`, for which no implementation exists - check that `GetAuthName` is correct, and instance exists in the `CredentialRequests` structure", authMethodString)
		}
		validAuthMethods[method] = true
	}
	if len(cfg.credentials.ValidAuthMethods) == 0 {
		// Default behavior with no cfg specified is that only unix auth is valid.
		validAuthMethods[auth.CredentialRequestUnix{}.GetAuthName()] = true
	}
	return validAuthMethods, nil
}

func getAuthArgs(reqb []byte) (authArgs, error) {
	var args authArgs
	reqbSize := len(reqb)
	if reqbSize == 0 {
		args.tag = auth.CredentialRequestUnix{}.GetAuthName()
	} else if reqbSize >= auth.AuthTagSize {
		copy(args.tag[:], reqb)
		args.reqBody = reqb[auth.AuthTagSize:]
	} else {
		return args, fmt.Errorf("auth args are length %d, expected either 0 (default/unix) or >=%d", reqbSize, auth.AuthTagSize)
	}

	return args, nil
}

// NewSecurityModule creates a new module with the given initialized TransportConfig.
func NewSecurityModule(log logging.Logger, cfg *securityConfig) (*SecurityModule, error) {
	var credCache *credentialCache
	credSigner := auth.CredentialRequestGetSigned
	if cfg.credentials.CacheExpiration > 0 {
		credCache = &credentialCache{
			log:          log,
			cache:        cache.NewItemCache(log),
			credLifetime: cfg.credentials.CacheExpiration,
			cacheMissFn:  auth.CredentialRequestGetSigned,
		}
		credSigner = credCache.getSignedCredential
		log.Noticef("credential cache enabled (entry lifetime: %s)", cfg.credentials.CacheExpiration)
	}

	validAuthMethods, err := getValidAuthMethods(cfg)
	if err != nil {
		return nil, err
	}

	return &SecurityModule{
		log:              log,
		signCredential:   credSigner,
		credCache:        credCache,
		config:           cfg,
		validAuthMethods: validAuthMethods,
	}, nil
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
	key := req.CredReqKey()

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
	args, err := getAuthArgs(reqb)
	if err != nil {
		return nil, errors.Wrap(err, "failed to parse request body")
	}

	_, ok := m.validAuthMethods[args.tag]
	if !ok {
		return nil, errors.New("Invalid authentication method: the method requested is not allowed by the agent configuration.")
	}

	var req auth.CredentialRequest = auth.AuthRegister[args.tag].AllocCredentialRequest()
	switch method {
	case daos.MethodRequestCredentials :
		return m.getCredential(ctx, session, args.reqBody, req)
	}

	return nil, drpc.UnknownMethodFailure()
}

// getCredentials generates a signed user credential based on the authentication method requested.
func (m *SecurityModule) getCredential(ctx context.Context, session *drpc.Session, body []byte, req auth.CredentialRequest) ([]byte, error) {
	signingKey, err := m.config.transport.PrivateKey()
	if err != nil {
		m.log.Errorf("failed to get signing key: %s", err)
		// something is wrong with the cert config
		return m.credRespWithStatus(daos.BadCert)
	}

	err = req.InitCredentialRequest(m.log, m.config.credentials, session, body, signingKey)
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
		return m.credRespWithStatus(daos.MiscError)
	}

	resp := &auth.GetCredResp{Cred: cred}
	return drpc.Marshal(resp)
}

func (m *SecurityModule) credRespWithStatus(status daos.Status) ([]byte, error) {
	resp := &auth.GetCredResp{Status: int32(status)}
	return drpc.Marshal(resp)
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

// ID will return Security module ID
func (m *SecurityModule) ID() int32 {
	return daos.ModuleSecurityAgent
}
