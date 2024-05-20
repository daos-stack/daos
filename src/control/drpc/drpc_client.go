//
// (C) Copyright 2019-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package drpc

import (
	"context"
	"net"
	"sync"

	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"
)

// DomainSocketClient is the interface to a dRPC client communicating over a
// Unix Domain Socket
type DomainSocketClient interface {
	sync.Locker
	IsConnected() bool
	Connect(context.Context) error
	Close() error
	SendMsg(context.Context, *Call) (*Response, error)
	GetSocketPath() string
}

// domainSocketDialer is an interface that connects to a Unix Domain Socket
type domainSocketDialer interface {
	dial(ctx context.Context, socketPath string) (net.Conn, error)
}

// ClientConnection represents a client connection to a dRPC server
type ClientConnection struct {
	sync.Mutex
	socketPath string             // Filesystem location of dRPC socket
	dialer     domainSocketDialer // Interface to connect to the socket
	conn       net.Conn           // Connection to socket
	connMu     sync.RWMutex       // Connection lock
	sequence   int64              // Increment each time we send
}

func (c *ClientConnection) isConnected() bool {
	return c.conn != nil
}

// IsConnected indicates whether the client connection is currently active
func (c *ClientConnection) IsConnected() bool {
	c.connMu.RLock()
	defer c.connMu.RUnlock()

	return c.isConnected()
}

// Connect opens a connection to the internal Unix Domain Socket path
func (c *ClientConnection) Connect(ctx context.Context) error {
	c.connMu.Lock()
	defer c.connMu.Unlock()

	if c.isConnected() {
		// Nothing to do
		return nil
	}

	conn, err := c.dialer.dial(ctx, c.socketPath)
	if err != nil {
		return errors.Wrap(err, "dRPC connect")
	}

	c.conn = conn
	c.sequence = 0 // reset message sequence number on connect
	return nil
}

// Close shuts down the connection to the Unix Domain Socket
func (c *ClientConnection) Close() error {
	c.connMu.Lock()
	defer c.connMu.Unlock()
	return c.close()
}

func (c *ClientConnection) close() error {
	if !c.isConnected() {
		// Nothing to do
		return nil
	}

	if err := c.conn.Close(); err != nil && !errors.Is(err, net.ErrClosed) {
		return errors.Wrap(err, "dRPC close")
	}

	c.conn = nil
	return nil
}

func (c *ClientConnection) sendCall(ctx context.Context, msg *Call) error {
	// increment sequence every call, always nonzero
	c.sequence++
	msg.Sequence = c.sequence

	callBytes, err := proto.Marshal(msg)
	if err != nil {
		return errors.Wrap(err, "failed to marshal dRPC request")
	}

	callWrite := make(chan struct{})
	defer close(callWrite)
	go func(closeConn func() error) {
		select {
		case <-ctx.Done():
			closeConn()
		case <-callWrite:
		}
	}(c.conn.Close)

	if _, err := c.conn.Write(callBytes); err != nil {
		if ctx.Err() != nil {
			err = ctx.Err()
			if cErr := c.close(); cErr != nil {
				err = errors.Wrapf(err, "failed to close dRPC connection: %v", cErr)
			}
		}
		return errors.Wrap(err, "dRPC send")
	}

	return nil
}

func (c *ClientConnection) recvResponse(ctx context.Context) (*Response, error) {
	respRead := make(chan struct{})
	defer close(respRead)
	go func(closeConn func() error) {
		select {
		case <-ctx.Done():
			closeConn()
		case <-respRead:
		}
	}(c.conn.Close)

	respBytes := make([]byte, MaxMsgSize)
	numBytes, err := c.conn.Read(respBytes)
	if err != nil {
		if ctx.Err() != nil {
			err = ctx.Err()
			if cErr := c.close(); cErr != nil {
				err = errors.Wrapf(err, "failed to close dRPC connection: %v", cErr)
			}
		}
		return nil, errors.Wrap(err, "dRPC recv")
	}

	resp := &Response{}
	err = proto.Unmarshal(respBytes[:numBytes], resp)
	if err != nil {
		return nil, errors.Wrap(err, "failed to unmarshal dRPC response")
	}

	return resp, nil
}

// SendMsg sends a message to the connected dRPC server, and returns the
// response to the caller.
func (c *ClientConnection) SendMsg(ctx context.Context, msg *Call) (*Response, error) {
	c.connMu.Lock()
	defer c.connMu.Unlock()
	if !c.isConnected() {
		return nil, errors.Errorf("dRPC not connected")
	}

	if msg == nil {
		return nil, errors.Errorf("invalid dRPC call")
	}

	err := c.sendCall(ctx, msg)
	if err != nil {
		return nil, errors.WithStack(err)
	}

	return c.recvResponse(ctx)
}

// GetSocketPath returns client dRPC socket file path.
func (c *ClientConnection) GetSocketPath() string {
	return c.socketPath
}

// NewClientConnection creates a new dRPC client
func NewClientConnection(socket string) DomainSocketClient {
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
func (c *clientDialer) dial(ctx context.Context, socketPath string) (net.Conn, error) {
	var d net.Dialer
	addr := net.UnixAddr{
		Net:  "unixpacket",
		Name: socketPath,
	}
	return d.DialContext(ctx, "unixpacket", addr.String())
}
