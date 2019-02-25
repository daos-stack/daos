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

package main

import (
	"bytes"
	"fmt"

	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/security"
	pb "github.com/daos-stack/daos/src/control/security/proto"
	"github.com/golang/protobuf/proto"
)

// Module id for the Server security module
const securityModuleID int32 = 2

const (
	methodValidateCredentials int32 = 101
)

// SecurityModule is the security drpc module struct
type SecurityModule struct {
}

func processValidateCredentials(body []byte) ([]byte, error) {
	credential := &pb.SecurityCredential{}
	err := proto.Unmarshal(body, credential)
	if err != nil {
		return nil, err
	}

	// Check our verifier
	hash, err := security.HashFromToken(credential.Token)
	if bytes.Compare(hash, credential.Verifier.Data) != 0 {
		return nil, fmt.Errorf("Verifier does not match token")
	}

	responseBytes, err := proto.Marshal(credential.Token)
	if err != nil {
		return nil, err
	}
	return responseBytes, nil
}

// HandleCall is the handler for calls to the SecurityModule
func (m *SecurityModule) HandleCall(client *drpc.Client, method int32, body []byte) ([]byte, error) {
	if method != methodValidateCredentials {
		return nil, fmt.Errorf("Attempt to call unregistered function")
	}

	responseBytes, err := processValidateCredentials(body)
	return responseBytes, err
}

// InitModule is empty for this module
func (m *SecurityModule) InitModule(state drpc.ModuleState) {}

// ID will return Security module ID
func (m *SecurityModule) ID() int32 {
	return securityModuleID
}
