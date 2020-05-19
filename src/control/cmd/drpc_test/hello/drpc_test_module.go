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

package hello

import (
	"fmt"

	"github.com/golang/protobuf/proto"

	"github.com/daos-stack/daos/src/control/drpc"
)

// HelloMethod is a type alias for a drpc agent hello method.
type HelloMethod int32

func (hm HelloMethod) ID() int32 {
	return int32(hm)
}

func (hm HelloMethod) String() string {
	return ""
}

// Module returns the module that the method belongs to.
func (hm HelloMethod) ModuleID() drpc.ModuleID {
	return drpc.ModuleID(Module_HELLO)
}

func (hm HelloMethod) Valid() bool {
	return true
}

const (
	MethodGreeting HelloMethod = HelloMethod(Function_GREETING)
)

//HelloModule is the RPC Handler for the Hello Module
type HelloModule struct{}

//HandleCall is the handler for calls to the hello module
func (m *HelloModule) HandleCall(session *drpc.Session, method drpc.Method, body []byte) ([]byte, error) {
	var gm drpc.Method = MethodGreeting
	if method != gm {
		return nil, fmt.Errorf("Attempt to call unregistered function")
	}

	helloMsg := &Hello{}
	proto.Unmarshal(body, helloMsg)

	greeting := fmt.Sprintf("Hello %s", helloMsg.Name)

	var response HelloResponse
	response = HelloResponse{
		Greeting: greeting,
	}

	responseBytes, mErr := proto.Marshal(&response)
	if mErr != nil {
		return nil, mErr
	}
	return responseBytes, nil
}

//ID will return Module_HELLO in int32 form
func (m *HelloModule) ID() drpc.ModuleID {
	return drpc.ModuleID(Module_HELLO)
}
