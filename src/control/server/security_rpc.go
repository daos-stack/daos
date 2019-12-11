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
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/security/auth"
)

// SecurityModule is the security drpc module struct
type SecurityModule struct {
	config *security.TransportConfig
}

// NewSecurityModule creates a new security module with a transport config
func NewSecurityModule(tc *security.TransportConfig) *SecurityModule {
	mod := &SecurityModule{
		config: tc,
	}
	return mod
}

func (m *SecurityModule) processValidateCredentials(body []byte) ([]byte, error) {
	var key crypto.PublicKey
	credential := &auth.Credential{}
	err := proto.Unmarshal(body, credential)
	if err != nil {
		return nil, drpc.UnmarshalingPayloadFailure()
	}

	if m.config.AllowInsecure {
		key = nil
	} else {
		certName := fmt.Sprintf("%s.%s", credential.Origin, "crt")
		certPath := filepath.Join(m.config.ClientCertDir, certName)
		cert, err := security.LoadCertificate(certPath)
		if err != nil {
			return nil, errors.Wrapf(err, "loading certificate %s failed:", certPath)
		}
		key = cert.PublicKey
	}

	// Check our verifier
	err = auth.VerifyToken(key, credential.GetToken(), credential.GetVerifier().GetData())
	if err != nil {
		return nil, errors.Wrapf(err, "credential verification failed for verifier %s", hex.Dump(credential.GetVerifier().GetData()))
	}

	responseBytes, err := proto.Marshal(credential.Token)
	if err != nil {
		return nil, drpc.MarshalingFailure()
	}
	return responseBytes, nil
}

// HandleCall is the handler for calls to the SecurityModule
func (m *SecurityModule) HandleCall(session *drpc.Session, method int32, body []byte) ([]byte, error) {
	if method != drpc.MethodValidateCredentials {
		return nil, drpc.UnknownMethodFailure()
	}

	responseBytes, err := m.processValidateCredentials(body)
	return responseBytes, err
}

// ID will return Security module ID
func (m *SecurityModule) ID() int32 {
	return drpc.ModuleSecurity
}
