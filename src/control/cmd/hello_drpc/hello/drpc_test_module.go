//
// (C) Copyright 2018-2022 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package hello

import (
	"context"
	"fmt"

	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/logging"
)

// helloMethod is a type alias for a drpc agent hello method.
type helloMethod int32

func (hm helloMethod) String() string {
	return "hello"
}

// Module returns the module that the method belongs to.
func (hm helloMethod) Module() int32 {
	return HelloModule{}.ID()
}

func (hm helloMethod) ID() int32 {
	return int32(hm)
}

func (hm helloMethod) IsValid() bool {
	return true
}

const (
	MethodGreeting helloMethod = helloMethod(Function_GREETING)
)

// HelloModule is the RPC Handler for the Hello Module
type HelloModule struct{}

// HandleCall is the handler for calls to the Hello module
func (m HelloModule) HandleCall(ctx context.Context, session *drpc.Session, method drpc.Method, body []byte) ([]byte, error) {
	logging.FromContext(ctx).Debugf("received message for method %s (%d)", method, method)

	if method.ID() != MethodGreeting.ID() {
		return nil, drpc.UnknownMethodFailure()
	}

	logging.FromContext(ctx).Debug("unmarshaling message")
	helloMsg := &Hello{}
	if err := proto.Unmarshal(body, helloMsg); err != nil {
		return nil, err
	}

	logging.FromContext(ctx).Debugf("generating greeting for name %q", helloMsg.Name)
	greeting := fmt.Sprintf("Hello %s", helloMsg.Name)

	response := HelloResponse{
		Greeting: greeting,
	}

	logging.FromContext(ctx).Debug("marshaling response")
	responseBytes, mErr := proto.Marshal(&response)
	if mErr != nil {
		return nil, mErr
	}
	return responseBytes, nil
}

// ID will return Module_HELLO as a helpful int
func (m HelloModule) ID() int32 {
	return int32(Module_HELLO)
}

// GetMethod fetches the corresponding Method for the method ID.
func (m HelloModule) GetMethod(id int32) (drpc.Method, error) {
	switch id {
	case MethodGreeting.ID():
		return MethodGreeting, nil
	default:
		return nil, fmt.Errorf("method ID %d does not exist in HelloModule", id)
	}
}

func (m HelloModule) String() string {
	return "hello"
}
