//
// (C) Copyright 2018-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"net"
	"os/user"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/security/auth"
)

type (
	credSignerFn func(*auth.CredentialRequest) (*auth.Credential, error)

	// securityConfig defines configuration parameters for SecurityModule.
	securityConfig struct {
		credentials *security.CredentialConfig
		transport   *security.TransportConfig
	}

	// SecurityModule is the security drpc module struct
	SecurityModule struct {
		log            logging.Logger
		signCredential credSignerFn

		config *securityConfig
	}
)

// NewSecurityModule creates a new module with the given initialized TransportConfig
func NewSecurityModule(log logging.Logger, cfg *securityConfig) *SecurityModule {
	return &SecurityModule{
		log:            log,
		signCredential: auth.GetSignedCredential,
		config:         cfg,
	}
}

// HandleCall is the handler for calls to the SecurityModule
func (m *SecurityModule) HandleCall(_ context.Context, session *drpc.Session, method drpc.Method, body []byte) ([]byte, error) {
	if method != drpc.MethodRequestCredentials {
		return nil, drpc.UnknownMethodFailure()
	}

	return m.getCredential(session)
}

// getCredentials generates a signed user credential based on the data attached to
// the Unix Domain Socket.
func (m *SecurityModule) getCredential(session *drpc.Session) ([]byte, error) {
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
	cred, err := m.signCredential(req)
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
			cred, err = m.signCredential(req)
			if err != nil {
				return err
			}

			return nil
		}(); err != nil {
			m.log.Errorf("%s: failed to get user credential: %s", info, err)
			return m.credRespWithStatus(daos.MiscError)
		}
	}

	m.log.Tracef("%s: successfully signed credential", info)
	resp := &auth.GetCredResp{Cred: cred}
	return drpc.Marshal(resp)
}

func (m *SecurityModule) credRespWithStatus(status daos.Status) ([]byte, error) {
	resp := &auth.GetCredResp{Status: int32(status)}
	return drpc.Marshal(resp)
}

// ID will return Security module ID
func (m *SecurityModule) ID() drpc.ModuleID {
	return drpc.ModuleSecurityAgent
}
