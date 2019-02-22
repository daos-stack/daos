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

package drpc

import (
	"errors"
	"fmt"
	"net"
	"os"
	"syscall"
)

// MAXMSGSIZE is the maximum drpc message size that may be sent.
// Using a packetsocket over the unix domain socket means that we receive
// a whole message at a time without knowing its size. So for this reason
// we need to restrict the maximum message size so we can preallocate a
// buffer to put all of the information in. Corresponding C definition is
// found in include/daos/drpc.h
//
const MAXMSGSIZE = 16384

// DomainSocketServer is the object representing the socket server which
// contains the socket path and service handlers for processing incoming
// messages.
type DomainSocketServer struct {
	sockFile string
	quit     chan bool
	listener *net.UnixListener
	service  *Service
	clients  map[*net.UnixConn]*Client
}

// Client is the encapsulation of all information needed to process messages
// from an individual endpoint that has dialed into the socket server over
// the Unix domain socket.
type Client struct {
	Conn    *net.UnixConn
	Service *Service
}

// RPCHandler is the go routine used to process incoming messages
// from a given client. It will have an instance of this function for
// each Client that is dialed into the server.
func rpcHandler(client *Client) {
	buffer := make([]byte, MAXMSGSIZE)

	for {

		bytesRead, _, _, _, err := client.Conn.ReadMsgUnix(buffer, nil)
		if err != nil {
			// This indicates that we have reached a bad state
			// for the connection and we need to terminate the handler.
			client.Conn.Close()
			break
		}

		response, err := client.Service.ProcessMessage(client, buffer[:bytesRead])
		if err != nil {
			// The only way we hit here is if buffer[:bytesRead] does not
			// represent a valid protobuf serialized structure. If the call
			// is referencing a function/module that does not exist then it
			// will return a valid protobuf reflecting that.
			client.Conn.Close()
			break
		}

		_, _, err = client.Conn.WriteMsgUnix(response, nil, nil)
		if err != nil {
			// This should only happen if we're shutting down while
			// trying to send our response but we close the
			// connection anyway in case we failed for another
			// reason.
			client.Conn.Close()
			break
		}

	}
}

// ConnReceiver is the go routine started when the server is
// started to listen on the unix domain socket for connections
// and to kick off the client handling process in a separate
// go routine.
func ConnReceiver(d *DomainSocketServer) error {
	for {
		conn, err := d.listener.AcceptUnix()
		if err != nil {
			select {
			case <-d.quit:
				for clientCon := range d.clients {
					clientCon.Close()
					delete(d.clients, clientCon)
				}
				return nil
			default:
				return fmt.Errorf("Unable to accept connection on unix socket %s: %s", d.sockFile, err)
			}
		}

		c := &Client{conn, d.service}
		d.clients[conn] = c
		go rpcHandler(c)
	}
}

// Start transitions the DomainSocketServer to the started state
// where it has created the unix domain socket and listens on it in a separate
// go routine.
func (d *DomainSocketServer) Start() error {

	addr := &net.UnixAddr{d.sockFile, "unixpacket"}

	// Setup our unix domain socket for our socket server to listen on
	err := syscall.Unlink(d.sockFile)
	if err != nil && !os.IsNotExist(err) {
		return fmt.Errorf("Unable to unlink %s: %s", d.sockFile, err)
	}

	lis, err := net.ListenUnix("unixpacket", addr)
	if err != nil {
		return fmt.Errorf("Unable to listen on unix socket %s: %s", d.sockFile, err)
	}

	err = os.Chmod(d.sockFile, 0777)
	if err != nil {
		return fmt.Errorf("Unable to set permissions on %s: %s", d.sockFile, err)
	}

	d.listener = lis
	go ConnReceiver(d)
	return nil
}

// Shutdown places the state of the server to shutdown which terminates the
// ConnReceiver go routine and starts the cleanup of all open connections.
func (d *DomainSocketServer) Shutdown() {
	close(d.quit)
	d.listener.Close()
}

// RegisterRPCModule takes an rpcModule type and associates it with the
// given DomainSocketServer so it can be used in RPCHandler to process incoming
// dRPC calls.
func (d *DomainSocketServer) RegisterRPCModule(mod Module) {
	d.service.RegisterModule(mod)
}

// NewDomainSocketServer returns a new unstarted instance of a
// DomainSocketServer for the specified unix domain socket path.
func NewDomainSocketServer(sock string) (*DomainSocketServer, error) {
	if sock == "" {
		return nil, errors.New("Missing Argument: sockFile")
	}
	service := NewRPCService()
	quit := make(chan bool)
	clients := make(map[*net.UnixConn]*Client)
	return &DomainSocketServer{sock, quit, nil, service, clients}, nil
}
