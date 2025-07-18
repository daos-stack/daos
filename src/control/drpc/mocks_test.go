//
// (C) Copyright 2019-2022 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package drpc

import (
	"context"
	"fmt"
	"net"
	"sync"
	"testing"
	"time"

	"github.com/pkg/errors"
)

type mockMethod struct {
	id     int32
	module int32
}

func (mm *mockMethod) ID() int32 {
	return mm.id
}

func (mm *mockMethod) Module() int32 {
	return mm.module
}

func (mm *mockMethod) String() string {
	return fmt.Sprintf("mock method %d", mm.id)
}

// mockModule is a mock of the Module interface
type mockModule struct {
	HandleCallResponse []byte
	HandleCallErr      error
	GetMethodErr       error
	IDValue            int32
}

func (m *mockModule) HandleCall(_ context.Context, session *Session, method Method, input []byte) ([]byte, error) {
	return m.HandleCallResponse, m.HandleCallErr
}

func (m *mockModule) ID() int32 {
	return m.IDValue
}

func (m *mockModule) String() string {
	return "mock module"
}

func (m *mockModule) GetMethod(id int32) (Method, error) {
	return &mockMethod{
		id:     id,
		module: m.IDValue,
	}, m.GetMethodErr
}

func newTestModule(ID int32) *mockModule {
	return &mockModule{
		IDValue: ID,
	}
}

// mockConn is a mock of the net.Conn interface
type mockConn struct {
	sync.Mutex
	ReadCallCount       int
	ReadInputBytes      []byte
	ReadOutputNumBytes  int
	ReadOutputError     error
	ReadOutputBytes     []byte // Bytes copied into buffer
	WriteCallCount      int
	WriteOutputNumBytes int
	WriteOutputError    error
	WriteInputBytes     []byte
	CloseCallCount      int // Number of times called
	CloseOutputError    error
}

func (m *mockConn) Read(b []byte) (n int, err error) {
	m.Lock()
	defer m.Unlock()
	m.ReadCallCount++
	m.ReadInputBytes = b
	copy(b, m.ReadOutputBytes)
	return m.ReadOutputNumBytes, m.ReadOutputError
}

func (m *mockConn) Write(b []byte) (n int, err error) {
	m.Lock()
	defer m.Unlock()
	m.WriteCallCount++
	m.WriteInputBytes = b
	return m.WriteOutputNumBytes, m.WriteOutputError
}

func (m *mockConn) Close() error {
	m.Lock()
	defer m.Unlock()
	m.CloseCallCount++
	return m.CloseOutputError
}

// WithLock can be used to safely read or write the mockConn's fields in a closure.
func (m *mockConn) WithLock(f func(m *mockConn)) {
	m.Lock()
	defer m.Unlock()
	f(m)
}

// TODO: implement other net.Conn methods
func (m *mockConn) LocalAddr() net.Addr {
	return nil
}

func (m *mockConn) RemoteAddr() net.Addr {
	return nil
}

func (m *mockConn) SetDeadline(t time.Time) error {
	return nil
}

func (m *mockConn) SetReadDeadline(t time.Time) error {
	return nil
}

func (m *mockConn) SetWriteDeadline(t time.Time) error {
	return nil
}

func newMockConn() *mockConn {
	return &mockConn{}
}

func (m *mockConn) SetReadOutputBytesToResponse(t *testing.T, resp *Response) {
	t.Helper()

	rawRespBytes := marshallResponseToBytes(t, resp)
	m.ReadOutputNumBytes = len(rawRespBytes)

	// The result from Read() will be MaxMsgSize since we have no way to
	// know the size of a read before we read it
	m.ReadOutputBytes = make([]byte, MaxMsgSize)
	copy(m.ReadOutputBytes, rawRespBytes)
}

func (m *mockConn) SetWriteOutputBytesForCall(t *testing.T, call *Call) []byte {
	t.Helper()

	callBytes := marshallCallToBytes(t, call)

	return callBytes
}

// mockListener is a mock of the net.Listener interface
type mockListener struct {
	acceptNumConns  int // accept a certain number of connections before failing
	acceptErr       error
	acceptCallCount int
	closeErr        error
	closeCallCount  int
}

func (l *mockListener) Accept() (net.Conn, error) {
	l.acceptCallCount++
	if l.acceptCallCount > l.acceptNumConns {
		return nil, l.acceptErr
	}
	return newMockConn(), nil
}

func (l *mockListener) Close() error {
	l.closeCallCount++
	return l.closeErr
}

// TODO: implement when needed
func (l *mockListener) Addr() net.Addr {
	return nil
}

func (l *mockListener) setNumConnsToAccept(n int) {
	l.acceptNumConns = n
	l.acceptErr = errors.New("mock done accepting connections")
}

func newMockListener() *mockListener {
	return &mockListener{
		acceptNumConns: -1,
	}
}

// ctxMockListener is a mock of the net.Listener interface that blocks
// on Accept until the context is canceled.
type ctxMockListener struct {
	ctx    context.Context
	closed chan bool
}

func (l *ctxMockListener) Accept() (net.Conn, error) {
	<-l.ctx.Done()
	return nil, l.ctx.Err()
}

func (l *ctxMockListener) Close() error {
	l.closed <- true
	return nil
}

func (l *ctxMockListener) Addr() net.Addr {
	return nil
}

func newCtxMockListener(ctx context.Context) *ctxMockListener {
	c := make(chan bool)
	return &ctxMockListener{
		ctx:    ctx,
		closed: c,
	}
}
