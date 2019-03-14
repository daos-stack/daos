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

package drpc

import (
	"fmt"
	"net"

	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"
)

// DomainSocketClient is the interface to a dRPC client communicating over a
// Unix Domain Socket
type DomainSocketClient interface {
	IsConnected() bool
	Connect() error
	Close() error
	SendMsg(call *Call) (*Response, error)
}

// domainSocketConn is an interface representing a connection to a Unix Domain
// Socket. Should play nicely with net.UnixConn
type domainSocketConn interface {
	ReadMsgUnix(b, oob []byte) (n, oobn, flags int, addr *net.UnixAddr, err error)
	WriteMsgUnix(b, oob []byte, addr *net.UnixAddr) (n, oobn int, err error)
	Close() error
}

// domainSocketDialer is an interface that connects to a Unix Domain Socket
type domainSocketDialer interface {
	dial(socketPath string) (domainSocketConn, error)
}

// ClientConnection represents a client connection to a dRPC server
type ClientConnection struct {
	socketPath string             // Filesystem location of dRPC socket
	dialer     domainSocketDialer // Interface to connect to the socket
	conn       domainSocketConn   // UDS connection
	sequence   int64              // Increment each time we send
}

// IsConnected indicates whether the client connection is currently active
func (c *ClientConnection) IsConnected() bool {
	return c.conn != nil
}

// Connect opens a connection to the internal Unix Domain Socket path
func (c *ClientConnection) Connect() error {
	if c.IsConnected() {
		// Nothing to do
		return nil
	}

	conn, err := c.dialer.dial(c.socketPath)
	if err != nil {
		return fmt.Errorf("dRPC connect: %v", err)
	}

	c.conn = conn
	c.sequence = 0 // reset message sequence number on connect
	return nil
}

// Close shuts down the connection to the Unix Domain Socket
func (c *ClientConnection) Close() error {
	if !c.IsConnected() {
		// Nothing to do
		return nil
	}

	err := c.conn.Close()
	if err != nil {
		return fmt.Errorf("dRPC close: %v", err)
	}

	c.conn = nil
	return nil
}

func (c *ClientConnection) sendCall(msg *Call) error {
	// increment sequence every call, always nonzero
	c.sequence++
	msg.Sequence = c.sequence

	callBytes, err := proto.Marshal(msg)
	if err != nil {
		return fmt.Errorf("failed to marshall dRPC call: %v", err)
	}

	_, _, err = c.conn.WriteMsgUnix(callBytes, nil, nil)
	if err != nil {
		return errors.Wrap(err, "dRPC send")
	}

	return nil
}

func (c *ClientConnection) recvResponse() (*Response, error) {
	respBytes := make([]byte, MAXMSGSIZE)
	numBytes, _, _, _, err := c.conn.ReadMsgUnix(respBytes, nil)
	if err != nil {
		return nil, errors.Wrap(err, "dRPC recv")
	}

	resp := &Response{}
	err = proto.Unmarshal(respBytes[:numBytes], resp)
	if err != nil {
		return nil, fmt.Errorf("failed to unmarshal dRPC response: %v",
			err)
	}

	return resp, nil
}

// SendMsg sends a message to the connected dRPC server, and returns the
// response to the caller.
func (c *ClientConnection) SendMsg(msg *Call) (*Response, error) {
	if !c.IsConnected() {
		return nil, fmt.Errorf("dRPC not connected")
	}

	if msg == nil {
		return nil, fmt.Errorf("invalid dRPC call")
	}

	err := c.sendCall(msg)
	if err != nil {
		return nil, errors.WithStack(err)
	}

	return c.recvResponse()
}

// NewClientConnection creates a new dRPC client
func NewClientConnection(socket string) *ClientConnection {
	return &ClientConnection{
		socketPath: socket,
		dialer:     &clientDialer{},
	}
}

// clientDialer is the concrete implementation of the domainSocketDialer
// interface for dRPC clients
type clientDialer struct {
}

// dial connects to the real unix domain socket located at socketPath
func (c *clientDialer) dial(socketPath string) (domainSocketConn, error) {
	addr := &net.UnixAddr{
		Net:  "unixpacket",
		Name: socketPath,
	}
	return net.DialUnix("unixpacket", nil, addr)
}
