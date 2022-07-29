//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package drpc

import (
	"fmt"
	"net"
	"testing"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"
)

// testSockPath is an arbitrary path string to use for testing. These tests
// don't touch the real filesystem.
const testSockPath string = "/my/test/socket.sock"

// mockDialer is a mock DomainSocketDialer for testing
type mockDialer struct {
	OutputConn    *mockConn
	OutputErr     error
	InputSockPath string
	DialCallCount int
}

// Dial is a mock that saves off its inputs and returns the mock output in the
// mockDialer struct
func (m *mockDialer) dial(socketPath string) (net.Conn, error) {
	m.InputSockPath = socketPath
	m.DialCallCount++
	return m.OutputConn, m.OutputErr
}

func (m *mockDialer) SetError(errStr string) {
	m.OutputErr = fmt.Errorf(errStr)
	m.OutputConn = nil
}

func newMockDialer() *mockDialer {
	return &mockDialer{
		OutputConn: newMockConn(),
		OutputErr:  nil,
	}
}

// newTestClientConnection lets us bypass the connection flow for testing and
// start in the connected state
func newTestClientConnection(dialer *mockDialer, conn *mockConn) *ClientConnection {
	client := &ClientConnection{
		socketPath: testSockPath,
		dialer:     dialer,
	}
	if conn != nil {
		client.conn = conn
	}
	return client
}

func TestNewClientConnection(t *testing.T) {
	client := NewClientConnection(testSockPath)

	if client == nil {
		t.Fatal("Expected a real client")
		return
	}
	test.AssertEqual(t, client.socketPath, testSockPath,
		"Should match the path we passed in")
	test.AssertFalse(t, client.IsConnected(), "Shouldn't be connected yet")

	// Dialer should be the private implementation type
	_ = client.dialer.(*clientDialer)
}

func TestClient_Connect_Success(t *testing.T) {
	dialer := newMockDialer()
	client := newTestClientConnection(dialer, nil)
	client.sequence = 10

	err := client.Connect()

	test.AssertTrue(t, err == nil, "Expected no error")
	test.AssertTrue(t, client.IsConnected(), "Should be connected")
	test.AssertEqual(t, client.conn, dialer.OutputConn,
		"Expected conn returned from the mock dialer")
	test.AssertEqual(t, dialer.InputSockPath, testSockPath,
		"Should be using passed-in socket path")
	test.AssertEqual(t, client.sequence, int64(0),
		"Expected sequence number to have been reset")
}

func TestClient_Connect_Error(t *testing.T) {
	dialer := newMockDialer()
	dialer.SetError("mock dialer failure")
	client := newTestClientConnection(dialer, nil)

	err := client.Connect()

	test.CmpErr(t, dialer.OutputErr, err)
	test.AssertFalse(t, client.IsConnected(), "Should not be connected")
	test.AssertTrue(t, client.conn == nil, "Expected no connection")
}

func TestClient_Connect_AlreadyConnected(t *testing.T) {
	originalConn := newMockConn()
	dialer := newMockDialer()
	client := newTestClientConnection(dialer, originalConn)

	err := client.Connect()

	test.AssertTrue(t, err == nil, "Expected no error")
	test.AssertEqual(t, client.conn, originalConn,
		"Connection should be unchanged")
	test.AssertEqual(t, dialer.DialCallCount, 0,
		"Should not have tried to connect")
}

func TestClient_Close_Success(t *testing.T) {
	conn := newMockConn()
	client := newTestClientConnection(newMockDialer(), conn)

	err := client.Close()

	test.AssertTrue(t, err == nil, "Expected no error")
	test.AssertEqual(t, conn.CloseCallCount, 1,
		"Expected conn.Close() to be called")
	test.AssertFalse(t, client.IsConnected(), "Should not be connected")
}

func TestClient_Close_Error(t *testing.T) {
	conn := newMockConn()
	conn.CloseOutputError = errors.New("mock close failure")
	client := newTestClientConnection(newMockDialer(), conn)

	err := client.Close()

	test.CmpErr(t, conn.CloseOutputError, err)
	test.AssertEqual(t, conn.CloseCallCount, 1,
		"Expected conn.Close() to be called")
	test.AssertTrue(t, client.IsConnected(),
		"Should have left the connection alone")
}

func TestClient_Close_NotConnected(t *testing.T) {
	client := newTestClientConnection(newMockDialer(), nil)

	err := client.Close()

	// Effectively a no-op, but no point reporting failure on a double close
	test.AssertTrue(t, err == nil, "Should report success")
	test.AssertFalse(t, client.IsConnected(), "Should not be connected")
}

func TestClient_SendMsg_NilInput(t *testing.T) {
	conn := newMockConn()
	client := newTestClientConnection(newMockDialer(), conn)

	response, err := client.SendMsg(nil)

	test.AssertTrue(t, response == nil, "Expected no response")
	test.ExpectError(t, err, "invalid dRPC call", "Expect error on nil input")
}

func marshallCallToBytes(t *testing.T, call *Call) []byte {
	t.Helper()
	callBytes, protoErr := proto.Marshal(call)
	test.AssertTrue(t, protoErr == nil, "Failed to marshal Call to bytes")
	return callBytes
}

func marshallResponseToBytes(t *testing.T, resp *Response) []byte {
	t.Helper()
	respBytes, protoErr := proto.Marshal(resp)
	test.AssertTrue(t, protoErr == nil, "Failed to marshal Response to bytes")
	return respBytes
}

func newTestCall() *Call {
	return &Call{
		Module:   1,
		Method:   2,
		Sequence: 3,
	}
}

func newTestResponse(sequence int64) *Response {
	return &Response{
		Sequence: sequence,
		Status:   Status_SUCCESS,
	}
}

func TestClient_SendMsg_Success(t *testing.T) {
	conn := newMockConn()
	client := newTestClientConnection(newMockDialer(), conn)
	client.sequence = 2 // ClientConnection keeps track of sequence

	call := newTestCall()
	callBytes := conn.SetWriteOutputBytesForCall(t, call)

	expectedResp := newTestResponse(client.sequence + 1)
	conn.SetReadOutputBytesToResponse(t, expectedResp)
	expectedRespBytes := conn.ReadOutputBytes

	response, err := client.SendMsg(call)

	test.AssertTrue(t, err == nil, "Expected no error")
	if response == nil {
		t.Fatal("Expected a real response")
		return
	}
	test.AssertEqual(t, response.Sequence, expectedResp.Sequence,
		"Response should match expected")
	test.AssertEqual(t, response.Status, expectedResp.Status,
		"Response should match expected")
	test.AssertEqual(t, response.Body, expectedResp.Body,
		"Response should match expected")
	test.AssertEqual(t, conn.WriteInputBytes, callBytes,
		"Expected call to be marshalled and passed to write")
	test.AssertTrue(t, conn.ReadInputBytes != nil,
		"Expected read called with buffer")
	test.AssertEqual(t, conn.ReadInputBytes, expectedRespBytes,
		"Marshalled response should be copied into the buffer")
}

func TestClient_SendMsg_NotConnected(t *testing.T) {
	client := newTestClientConnection(newMockDialer(), nil)

	response, err := client.SendMsg(newTestCall())

	test.AssertTrue(t, response == nil, "Expected no response")
	test.ExpectError(t, err, "dRPC not connected",
		"Expected error for unconnected client")
}

func TestClient_SendMsg_WriteError(t *testing.T) {
	conn := newMockConn()
	client := newTestClientConnection(newMockDialer(), conn)

	call := newTestCall()
	conn.WriteOutputError = errors.New("mock write failure")

	response, err := client.SendMsg(call)

	test.AssertTrue(t, response == nil, "Expected no response")
	test.CmpErr(t, conn.WriteOutputError, err)
}

func TestClient_SendMsg_ReadError(t *testing.T) {
	conn := newMockConn()
	client := newTestClientConnection(newMockDialer(), conn)

	call := newTestCall()
	conn.SetWriteOutputBytesForCall(t, call)

	conn.ReadOutputNumBytes = 0
	conn.ReadOutputError = errors.New("mock read failure")

	response, err := client.SendMsg(call)

	test.AssertTrue(t, response == nil, "Expected no response")
	test.CmpErr(t, conn.ReadOutputError, err)
}

func TestClient_SendMsg_UnmarshalResponseFailure(t *testing.T) {
	conn := newMockConn()
	client := newTestClientConnection(newMockDialer(), conn)

	call := newTestCall()
	conn.SetWriteOutputBytesForCall(t, call)

	// Invalid response - fails to unmarshal
	conn.ReadOutputBytes = make([]byte, 1024)
	for i := 0; i < len(conn.ReadOutputBytes); i++ {
		conn.ReadOutputBytes[i] = 0xFF
	}
	conn.ReadOutputNumBytes = len(conn.ReadOutputBytes)

	response, err := client.SendMsg(call)

	expectedErr := errors.New("failed to unmarshal dRPC response")
	test.AssertTrue(t, response == nil, "Expected no response")
	test.CmpErr(t, expectedErr, err)
}
