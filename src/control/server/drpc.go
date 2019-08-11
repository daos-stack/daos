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
	"os"
	"path/filepath"

	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/security"
)

const sockFileName = "daos_server.sock"

func getDrpcClientSocket(sockDir string) string {
	return filepath.Join(sockDir, "daos_io_server.sock")
}

func getDrpcClientConnection(sockDir string) *drpc.ClientConnection {
	clientSock := getDrpcClientSocket(sockDir)
	return drpc.NewClientConnection(clientSock)
}

// checkSocketDir verifies socket directory exists, has appropriate permissions
// and is a directory. SocketDir should be created during configuration management
// as locations may not be user creatable.
func checkSocketDir(sockDir string) error {
	f, err := os.Stat(sockDir)
	if err != nil {
		msg := "unexpected error locating"
		if os.IsPermission(err) {
			msg = "permissions failure accessing"
		} else if os.IsNotExist(err) {
			msg = "missing"
		}

		return errors.WithMessagef(err, "%s socket directory %s", msg, sockDir)
	}
	if !f.IsDir() {
		return errors.Errorf("path %s not a directory", sockDir)
	}

	return nil
}

// drpcSetup checks socket directory exists, specifies socket path and starts drpc server.
func drpcSetup(sockDir string, iosrv *iosrv, tc *security.TransportConfig) error {
	if err := checkSocketDir(sockDir); err != nil {
		return err
	}

	sockPath := filepath.Join(sockDir, sockFileName)
	drpcServer, err := drpc.NewDomainSocketServer(sockPath)
	if err != nil {
		return errors.Wrap(err, "unable to create socket server")
	}

	// Create and add our modules
	drpcServer.RegisterRPCModule(NewSecurityModule(tc))
	drpcServer.RegisterRPCModule(&mgmtModule{})
	drpcServer.RegisterRPCModule(&srvModule{iosrv})

	if err := drpcServer.Start(); err != nil {
		return errors.Wrapf(err, "unable to start socket server on %s", sockPath)
	}

	return nil
}

// checkDrpcResponse checks for some basic formatting errors
func checkDrpcResponse(drpcResp *drpc.Response) error {
	if drpcResp == nil {
		return errors.Errorf("dRPC returned no response")
	}

	if drpcResp.Status != drpc.Status_SUCCESS {
		return errors.Errorf("bad dRPC response status: %v",
			drpcResp.Status.String())
	}

	return nil
}

// newDrpcCall creates a new drpc Call instance for specified module, with
// the protobuf message marshalled in the body
func newDrpcCall(module int32, method int32, bodyMessage proto.Message) (*drpc.Call, error) {
	var bodyBytes []byte
	if bodyMessage != nil {
		var err error
		bodyBytes, err = proto.Marshal(bodyMessage)
		if err != nil {
			return nil, err
		}
	}

	return &drpc.Call{
		Module: module,
		Method: method,
		Body:   bodyBytes,
	}, nil
}

// makeDrpcCall opens a drpc connection, sends a message with the
// protobuf message marshalled in the body, and closes the connection.
// drpc response is returned after basic checks.
func makeDrpcCall(
	client drpc.DomainSocketClient, module int32, method int32,
	body proto.Message) (drpcResp *drpc.Response, err error) {

	drpcCall, err := newDrpcCall(module, method, body)
	if err != nil {
		return drpcResp, errors.Wrap(err, "build drpc call")
	}

	// Forward the request to the I/O server via dRPC
	if err = client.Connect(); err != nil {
		return drpcResp, errors.Wrap(err, "connect to client")
	}
	defer client.Close()

	if drpcResp, err = client.SendMsg(drpcCall); err != nil {
		return drpcResp, errors.Wrap(err, "send message")
	}

	if err = checkDrpcResponse(drpcResp); err != nil {
		return drpcResp, errors.Wrap(err, "validate response")
	}

	return
}
