//
// (C) Copyright 2020 Intel Corporation.
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

package control

import (
	"context"

	"github.com/golang/protobuf/proto"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
)

const (
	MockUUID = "00000000-1111-2222-3333-444444444444"
)

type (
	// MockInvokerConfig defines the configured responses
	// for a MockInvoker.
	MockInvokerConfig struct {
		UnaryError    error
		UnaryResponse *UnaryResponse
		HostResponses HostResponseChan
	}

	// MockInvoker implements the Invoker interface in order
	// to enable unit testing of API functions.
	MockInvoker struct {
		log debugLogger
		cfg MockInvokerConfig
	}
)

func (mi *MockInvoker) Debug(msg string) {
	mi.log.Debug(msg)
}

func (mi *MockInvoker) Debugf(fmtStr string, args ...interface{}) {
	mi.log.Debugf(fmtStr, args...)
}

func (mi *MockInvoker) InvokeUnaryRPC(_ context.Context, uReq UnaryRequest) (*UnaryResponse, error) {
	if mi.cfg.UnaryResponse != nil || mi.cfg.UnaryError != nil {
		return mi.cfg.UnaryResponse, mi.cfg.UnaryError
	}

	// This is a little gross, but allows us to keep the complexity here rather
	// than in the tests. In particular, the dmg CLI surface tests don't really
	// need to know all of the details.
	addMSResp := func(msg proto.Message) []*HostResponse {
		return []*HostResponse{
			&HostResponse{
				Message: msg,
			},
		}
	}
	uResp := &UnaryResponse{
		fromMS:    uReq.isMSRequest(),
		Responses: []*HostResponse{},
	}
	switch uReq.(type) {
	case *PoolCreateReq:
		uResp.Responses = addMSResp(&mgmtpb.PoolCreateResp{})
	case *PoolDestroyReq:
		uResp.Responses = addMSResp(&mgmtpb.PoolDestroyResp{})
	}
	return uResp, nil
}

func (mi *MockInvoker) InvokeUnaryRPCAsync(_ context.Context, _ UnaryRequest) (HostResponseChan, error) {
	return mi.cfg.HostResponses, mi.cfg.UnaryError
}

func (mi *MockInvoker) SetClientConfig(_ *ClientConfig) {}

// DefaultMockInvokerConfig returns the default MockInvoker
// configuration.
func DefaultMockInvokerConfig() *MockInvokerConfig {
	return &MockInvokerConfig{}
}

// NewMockInvoker returns a configured MockInvoker. If
// a nil config is supplied, the default config is used.
func NewMockInvoker(log debugLogger, cfg *MockInvokerConfig) *MockInvoker {
	if cfg == nil {
		cfg = DefaultMockInvokerConfig()
	}

	if log == nil {
		log = defaultLogger
	}

	return &MockInvoker{
		log: log,
		cfg: *cfg,
	}
}

// DefaultMockInvoker returns a MockInvoker that uses
// the default configuration.
func DefaultMockInvoker(log debugLogger) *MockInvoker {
	return NewMockInvoker(log, nil)
}
