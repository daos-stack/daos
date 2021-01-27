//
// (C) Copyright 2018-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package hello

import (
	"fmt"

	"github.com/golang/protobuf/proto"

	"github.com/daos-stack/daos/src/control/drpc"
)

// helloMethod is a type alias for a drpc agent hello method.
type helloMethod int32

func (hm helloMethod) String() string {
	return "hello"
}

// Module returns the module that the method belongs to.
func (hm helloMethod) Module() drpc.ModuleID {
	return HelloModule{}.ID()
}

func (hm helloMethod) ID() int32 {
	return int32(hm)
}

func (hm helloMethod) IsValid() bool {
	return true
}

const (
	methodGreeting helloMethod = helloMethod(Function_GREETING)
)

//HelloModule is the RPC Handler for the Hello Module
type HelloModule struct{}

//HandleCall is the handler for calls to the Hello module
func (m HelloModule) HandleCall(session *drpc.Session, method drpc.Method, body []byte) ([]byte, error) {
	if method != methodGreeting {
		return nil, fmt.Errorf("Attempt to call unregistered function")
	}

	helloMsg := &Hello{}
	if err := proto.Unmarshal(body, helloMsg); err != nil {
		return nil, err
	}

	greeting := fmt.Sprintf("Hello %s", helloMsg.Name)

	response := HelloResponse{
		Greeting: greeting,
	}

	responseBytes, mErr := proto.Marshal(&response)
	if mErr != nil {
		return nil, mErr
	}
	return responseBytes, nil
}

//ID will return Module_HELLO as a ModuleID type
func (m HelloModule) ID() drpc.ModuleID {
	return drpc.ModuleID(Module_HELLO)
}
