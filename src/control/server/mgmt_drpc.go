//
// (C) Copyright 2018-2019 Intel Corporation.
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
	"os"
	"path/filepath"

	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/pkg/errors"
)

var sockFileName = "daos_server.sock"

func getDrpcClientSocket(sockDir string) string {
	return filepath.Join(sockDir, "daos_io_server.sock")
}

func getDrpcClientConnection(sockDir string) *drpc.ClientConnection {
	clientSock := getDrpcClientSocket(sockDir)
	return drpc.NewClientConnection(clientSock)
}

// drpcSetup creates socket directory, specifies socket path and then
// starts drpc server.
func drpcSetup(sockDir string, iosrv *iosrv) error {
	// Create our socket directory if it doesn't exist
	_, err := os.Stat(sockDir)
	if err != nil && os.IsPermission(err) {
		return errors.Wrap(
			err, "user does not have permission to access "+sockDir)
	} else if err != nil && os.IsNotExist(err) {
		err = os.MkdirAll(sockDir, 0755)
		if err != nil {
			return errors.Wrap(
				err,
				"unable to create socket directory "+sockDir)
		}
	}

	sockPath := filepath.Join(sockDir, sockFileName)
	drpcServer, err := drpc.NewDomainSocketServer(sockPath)
	if err != nil {
		return errors.Wrap(err, "unable to create socket server")
	}

	// Create and add our modules
	srvmodule := &srvModule{iosrv}
	drpcServer.RegisterRPCModule(srvmodule)
	secmodule := &SecurityModule{}
	drpcServer.RegisterRPCModule(secmodule)

	err = drpcServer.Start()
	if err != nil {
		return errors.Wrap(
			err, "unable to start socket server on "+sockPath)
	}

	return nil
}

// srvModule represents the daos_server dRPC module. It handles dRPCs sent by
// the daos_io_server iosrv module (src/iosrv).
type srvModule struct {
	iosrv *iosrv
}

const srvModuleID = 1

// These are srvModule dRPC methods.
const (
	notifyReady int32 = iota
)

func (mod *srvModule) HandleCall(cli *drpc.Client, method int32, req []byte) ([]byte, error) {
	switch method {
	case notifyReady:
		mod.handleNotifyReady()
		return nil, nil
	default:
		return nil, errors.Errorf("unknown dRPC %d", method)
	}
}

func (mod *srvModule) InitModule(state drpc.ModuleState) {}

func (mod *srvModule) ID() int32 {
	return srvModuleID
}

func (mod *srvModule) handleNotifyReady() {
	mod.iosrv.ready <- struct{}{}
}
