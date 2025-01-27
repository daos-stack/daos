//
// (C) Copyright 2019-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"os"
	"path/filepath"

	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/system/raft"
)

func getDrpcServerSocketPath(sockDir string) string {
	return filepath.Join(sockDir, "daos_server.sock")
}

func checkDrpcClientSocketPath(socketPath string) error {
	if socketPath == "" {
		return errors.New("socket path empty")
	}

	f, err := os.Stat(socketPath)
	if err != nil {
		return errors.Wrapf(err, "socket path %q could not be accessed", socketPath)
	}

	if (f.Mode() & os.ModeSocket) == 0 {
		return errors.Errorf("path %q is not a socket", socketPath)
	}

	return nil
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

type drpcServerSetupReq struct {
	log     logging.Logger
	sockDir string
	engines []Engine
	tc      *security.TransportConfig
	sysdb   *raft.Database
	events  *events.PubSub
}

// drpcServerSetup specifies socket path and starts drpc server.
func drpcServerSetup(ctx context.Context, req *drpcServerSetupReq) error {
	// Clean up any previous execution's sockets before we create any new sockets
	if err := drpcCleanup(req.sockDir); err != nil {
		return err
	}

	sockPath := getDrpcServerSocketPath(req.sockDir)

	// Server socket file to be readable and writable by user. daos_server should receive
	// messages from daos_engine and both processes will be run by the same user.
	drpcServer, err := drpc.NewDomainSocketServer(req.log, sockPath, 0600)
	if err != nil {
		return errors.Wrap(err, "unable to create socket server")
	}

	// Create and add our modules
	drpcServer.RegisterRPCModule(NewSecurityModule(req.log, req.tc))
	drpcServer.RegisterRPCModule(newMgmtModule())
	drpcServer.RegisterRPCModule(newSrvModule(req.log, req.sysdb, req.sysdb, req.engines, req.events))

	if err := drpcServer.Start(ctx); err != nil {
		return errors.Wrapf(err, "unable to start socket server on %s", sockPath)
	}

	return nil
}

// drpcCleanup deletes any DAOS sockets in the socket directory
func drpcCleanup(sockDir string) error {
	if err := checkSocketDir(sockDir); err != nil {
		return err
	}

	srvSock := getDrpcServerSocketPath(sockDir)
	os.Remove(srvSock)

	pattern := filepath.Join(sockDir, "daos_engine*.sock")
	engineSocks, err := filepath.Glob(pattern)
	if err != nil {
		return errors.WithMessage(err, "couldn't get list of engine sockets")
	}

	for _, s := range engineSocks {
		os.Remove(s)
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

// newDrpcCall creates a new drpc Call instance for specified with
// the protobuf message marshalled in the body
func newDrpcCall(method drpc.Method, bodyMessage proto.Message) (*drpc.Call, error) {
	var bodyBytes []byte
	if bodyMessage != nil {
		var err error
		bodyBytes, err = proto.Marshal(bodyMessage)
		if err != nil {
			return nil, err
		}
	}

	return &drpc.Call{
		Module: method.Module().ID(),
		Method: method.ID(),
		Body:   bodyBytes,
	}, nil
}

// makeDrpcCall opens a drpc connection, sends a message with the
// protobuf message marshalled in the body, and closes the connection.
// drpc response is returned after basic checks.
func makeDrpcCall(ctx context.Context, log logging.Logger, client drpc.DomainSocketClient, method drpc.Method, msg proto.Message) (drpcResp *drpc.Response, err error) {
	client.Lock()
	defer client.Unlock()

	drpcCall, err := newDrpcCall(method, msg)
	if err != nil {
		return nil, errors.Wrap(err, "build drpc call")
	}

	// Forward the request to the I/O Engine via dRPC
	if err = client.Connect(ctx); err != nil {
		if te, ok := errors.Cause(err).(interface{ Temporary() bool }); ok {
			if !te.Temporary() {
				return nil, FaultDataPlaneNotStarted
			}
		}
		return nil, errors.Wrap(err, "connect to client")
	}
	defer func() {
		if err := client.Close(); err != nil {
			log.Errorf("error closing dRPC client: %s", err)
		}
	}()

	if drpcResp, err = client.SendMsg(ctx, drpcCall); err != nil {
		return nil, errors.Wrapf(err, "failed to send %dB message", proto.Size(msg))
	}

	if err = checkDrpcResponse(drpcResp); err != nil {
		return nil, errors.Wrap(err, "validate response")
	}

	return drpcResp, nil
}
