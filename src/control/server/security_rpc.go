//
// (C) Copyright 2019 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package server

import (
	"crypto"
	"encoding/hex"
	"fmt"
	"path/filepath"

	"github.com/golang/protobuf/proto"

	"github.com/daos-stack/daos/src/control/drpc"
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
	mod := &SecurityModule{
		log:    log,
		config: tc,
	}
	return mod
}

func (m *SecurityModule) processValidateCredentials(body []byte) ([]byte, error) {
	req := &auth.ValidateCredReq{}
	err := proto.Unmarshal(body, req)
	if err != nil {
		return nil, drpc.UnmarshalingPayloadFailure()
	}

	cred := req.Cred
	if cred == nil || cred.GetToken() == nil || cred.GetVerifier() == nil {
		m.log.Errorf("Invalid credential: %+v", cred)
		return m.validateRespWithStatus(drpc.DaosInvalidInput)
	}

	var key crypto.PublicKey
	if m.config.AllowInsecure {
		key = nil
	} else {
		certName := fmt.Sprintf("%s.%s", cred.Origin, "crt")
		certPath := filepath.Join(m.config.ClientCertDir, certName)
		cert, err := security.LoadCertificate(certPath)
		if err != nil {
			m.log.Errorf("loading certificate %s failed: %v", certPath, err)
			return m.validateRespWithStatus(drpc.DaosBadPath)
		}
		key = cert.PublicKey
	}

	// Check our verifier
	err = auth.VerifyToken(key, cred.GetToken(), cred.GetVerifier().GetData())
	if err != nil {
		m.log.Errorf("cred verification failed: %v for verifier %s", err, hex.Dump(cred.GetVerifier().GetData()))
		return m.validateRespWithStatus(drpc.DaosNoPermission)
	}

	resp := &auth.ValidateCredResp{Token: cred.Token}
	responseBytes, err := proto.Marshal(resp)
	if err != nil {
		return nil, drpc.MarshalingFailure()
	}
	return responseBytes, nil
}

func (m *SecurityModule) validateRespWithStatus(status drpc.DaosStatus) ([]byte, error) {
	resp := &auth.ValidateCredResp{Status: int32(status)}
	return drpc.Marshal(resp)
}

// HandleCall is the handler for calls to the SecurityModule
func (m *SecurityModule) HandleCall(session *drpc.Session, method *drpc.Method, body []byte) ([]byte, error) {
	if method.ID() != drpc.MethodValidateCredentials {
		return nil, drpc.UnknownMethodFailure()
	}

	return m.processValidateCredentials(body)
}

// ID will return Security module ID
func (m *SecurityModule) ID() int32 {
	return drpc.ModuleSecurity
}
