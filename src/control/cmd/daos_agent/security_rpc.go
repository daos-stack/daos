//
// (C) Copyright 2018-2020 Intel Corporation.
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

package main

import (
	"net"

	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/security/auth"
)

// SecurityModule is the security drpc module struct
type SecurityModule struct {
	log    logging.Logger
	ext    auth.UserExt
	config *security.TransportConfig
}

//NewSecurityModule creates a new module with the given initialized TransportConfig
func NewSecurityModule(log logging.Logger, tc *security.TransportConfig) *SecurityModule {
	mod := SecurityModule{
		log:    log,
		config: tc,
	}
	mod.ext = &auth.External{}
	return &mod
}

// HandleCall is the handler for calls to the SecurityModule
func (m *SecurityModule) HandleCall(session *drpc.Session, method drpc.Method, body []byte) ([]byte, error) {
	if method != drpc.MethodRequestCredentials {
		return nil, drpc.UnknownMethodFailure()
	}

	return m.getCredential(session)
}

// getCredentials generates a signed user credential based on the data attached to
// the Unix Domain Socket.
func (m *SecurityModule) getCredential(session *drpc.Session) ([]byte, error) {
	uConn, ok := session.Conn.(*net.UnixConn)
	if !ok {
		return nil, drpc.NewFailureWithMessage("connection is not a unix socket")
	}

	info, err := security.DomainInfoFromUnixConn(m.log, uConn)
	if err != nil {
		m.log.Errorf("Unable to get credentials for client socket: %s", err)
		return m.credRespWithStatus(drpc.DaosMiscError)
	}

	signingKey, err := m.config.PrivateKey()
	if err != nil {
		m.log.Error(err.Error())
		// something is wrong with the cert config
		return m.credRespWithStatus(drpc.DaosInvalidInput)
	}

	cred, err := auth.AuthSysRequestFromCreds(m.ext, info, signingKey)
	if err != nil {
		m.log.Errorf("Failed to get AuthSys struct: %s", err)
		return m.credRespWithStatus(drpc.DaosMiscError)
	}

	resp := &auth.GetCredResp{Cred: cred}
	return drpc.Marshal(resp)
}

func (m *SecurityModule) credRespWithStatus(status drpc.DaosStatus) ([]byte, error) {
	resp := &auth.GetCredResp{Status: int32(status)}
	return drpc.Marshal(resp)
}

// ID will return Security module ID
func (m *SecurityModule) ID() drpc.ModuleID {
	return drpc.ModuleSecurityAgent
}
