//
// (C) Copyright 2019-2020 Intel Corporation.
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
package pbin

import (
	"fmt"
	"io"
	"net"
	"sync"
	"time"

	"github.com/pkg/errors"
)

// StdioAddr implements net.Addr to provide an emulated network
// address for use with StdioConn.
type StdioAddr struct {
	id string
}

func (s *StdioAddr) Network() string {
	return "stdio"
}

func (s *StdioAddr) String() string {
	return s.id
}

func NewStdioConn(localID, remoteID string, in io.ReadCloser, out io.WriteCloser) *StdioConn {
	return &StdioConn{
		local:  &StdioAddr{localID},
		remote: &StdioAddr{remoteID},
		in:     in,
		out:    out,
	}
}

// StdioConn implements net.Conn to provide an emulated network
// connection between two processes over stdin/stdout.
type StdioConn struct {
	sync.RWMutex
	readClosed  bool
	writeClosed bool

	local  *StdioAddr
	remote *StdioAddr
	in     io.ReadCloser
	out    io.WriteCloser
}

func (sc *StdioConn) String() string {
	return fmt.Sprintf("%s<->%s", sc.LocalAddr(), sc.RemoteAddr())
}

func (sc *StdioConn) isClosed() bool {
	sc.RLock()
	defer sc.RUnlock()
	return sc.writeClosed && sc.readClosed
}

func (sc *StdioConn) isReadClosed() bool {
	sc.RLock()
	defer sc.RUnlock()
	return sc.readClosed
}

func (sc *StdioConn) isWriteClosed() bool {
	sc.RLock()
	defer sc.RUnlock()
	return sc.writeClosed
}

func (sc *StdioConn) Read(b []byte) (int, error) {
	if sc.isReadClosed() {
		return 0, errors.New("read on closed conn")
	}
	return sc.in.Read(b)
}

func (sc *StdioConn) Write(b []byte) (int, error) {
	if sc.isWriteClosed() {
		return 0, errors.New("write on closed conn")
	}
	return sc.out.Write(b)
}

func (sc *StdioConn) CloseRead() error {
	sc.Lock()
	defer sc.Unlock()
	sc.readClosed = true
	return sc.in.Close()
}

func (sc *StdioConn) CloseWrite() error {
	sc.Lock()
	defer sc.Unlock()
	sc.writeClosed = true
	return sc.out.Close()
}

func (sc *StdioConn) Close() error {
	if err := sc.CloseRead(); err != nil {
		return err
	}

	return sc.CloseWrite()
}

func (sc *StdioConn) LocalAddr() net.Addr {
	return sc.local
}

func (sc *StdioConn) RemoteAddr() net.Addr {
	return sc.remote
}

func (sc *StdioConn) SetDeadline(t time.Time) error {
	return nil
}

func (sc *StdioConn) SetReadDeadline(t time.Time) error {
	return nil
}

func (sc *StdioConn) SetWriteDeadline(t time.Time) error {
	return nil
}

func NewStdioListener(conn *StdioConn) *StdioListener {
	sl := &StdioListener{
		ready: make(chan *StdioConn),
		conn:  conn,
	}
	sl.Close()
	return sl
}

// StdioListener wraps a *StdioConn to implement net.Listener.
type StdioListener struct {
	ready chan *StdioConn
	conn  *StdioConn
}

func (sl *StdioListener) Accept() (net.Conn, error) {
	if sl.conn.isClosed() {
		return nil, errors.New("accept on closed conn")
	}
	return <-sl.ready, nil
}

func (sl *StdioListener) Addr() net.Addr {
	return sl.conn.LocalAddr()
}

func (sl *StdioListener) Close() error {
	if sl.conn.isClosed() {
		return nil
	}

	go func() {
		sl.ready <- sl.conn
	}()
	return nil
}
