//
// (C) Copyright 2019-2022 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"crypto"
	"fmt"
	"path/filepath"

	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/security/auth"
)

// SecurityModule is the security drpc module struct
type SecurityModule struct {
	log    logging.Logger
	config *security.TransportConfig
}

// NewSecurityModule creates a new security module with a transport config
func NewSecurityModule(log logging.Logger, tc *security.TransportConfig) *SecurityModule {
	return &SecurityModule{
		log:    log,
		config: tc,
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
	if method != daos.MethodValidateCredentials {
		return nil, drpc.UnknownMethodFailure()
	}

	return m.processValidateCredentials(body)
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
	default:
		return nil, fmt.Errorf("invalid method ID %d for module %s", id, m.String())
	}
}
