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
)

const (
	MockUUID = "00000000-1111-2222-3333-444444444444"
)

// MockMessage implements the proto.Message
// interface, and can be used for test mocks.
type MockMessage struct{}

func (mm *MockMessage) Reset()         {}
func (mm *MockMessage) String() string { return "mock" }
func (mm *MockMessage) ProtoMessage()  {}

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

// MockMSResponse creates a synthetic Management Service response
// from the supplied HostResponse values.
func MockMSResponse(hostAddr string, hostErr error, hostMsg proto.Message) *UnaryResponse {
	return &UnaryResponse{
		fromMS: true,
		Responses: []*HostResponse{
			{
				Addr:    hostAddr,
				Error:   hostErr,
				Message: hostMsg,
			},
		},
	}
}

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

	// If the config didn't define a response, just dummy one up for
	// tests that don't care.
	return &UnaryResponse{
		fromMS: uReq.isMSRequest(),
		Responses: []*HostResponse{
			{
				Addr:    "dummy",
				Message: &MockMessage{},
			},
		},
	}, nil
}

func (mi *MockInvoker) InvokeUnaryRPCAsync(_ context.Context, _ UnaryRequest) (HostResponseChan, error) {
	return mi.cfg.HostResponses, mi.cfg.UnaryError
}

func (mi *MockInvoker) SetConfig(_ *Config) {}

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
