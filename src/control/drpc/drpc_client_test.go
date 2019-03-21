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
	"os"
	"testing"

	. "github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/log"
	"github.com/golang/protobuf/proto"
)

// testSockPath is an arbitrary path string to use for testing. These tests
// don't touch the real filesystem.
const testSockPath string = "/my/test/socket.sock"

func init() {
	log.NewDefaultLogger(log.Error, "drpc_client_test: ", os.Stderr)
}

// mockConn is a mock of the net.Conn interface, for testing
type mockConn struct {
	ReadOutputNumBytes  int
	ReadOutputError     error
	ReadOutputBytes     []byte // Bytes copied into buffer
	ReadInputBytes      []byte
	WriteOutputNumBytes int
	WriteOutputError    error
	WriteInputBytes     []byte
	CloseCallCount      int // Number of times called
	CloseOutputError    error
}

func (m *mockConn) ReadMsgUnix(b, oob []byte) (n, oobn, flags int, addr *net.UnixAddr, err error) {
	copy(b, m.ReadOutputBytes)
	m.ReadInputBytes = b
	return m.ReadOutputNumBytes, 0, 0, nil, m.ReadOutputError
}

func (m *mockConn) WriteMsgUnix(b, oob []byte, addr *net.UnixAddr) (n, oobn int, err error) {
	m.WriteInputBytes = b
	return m.WriteOutputNumBytes, 0, m.WriteOutputError
}

func (m *mockConn) Close() error {
	m.CloseCallCount++
	return m.CloseOutputError
}

func (m *mockConn) SetReadOutputBytesToResponse(t *testing.T, resp *Response) {
	rawRespBytes := marshallResponseToBytes(t, resp)
	m.ReadOutputNumBytes = len(rawRespBytes)

	// The result from Read() will be MAXMSGSIZE since we have no way to
	// know the size of a read before we read it
	m.ReadOutputBytes = make([]byte, MAXMSGSIZE)
	copy(m.ReadOutputBytes, rawRespBytes)
}

func (m *mockConn) SetWriteOutputBytesForCall(t *testing.T, call *Call) []byte {
	callBytes := marshallCallToBytes(t, call)
	m.WriteOutputNumBytes = len(callBytes)

	return callBytes
}

func newMockConn() *mockConn {
	return &mockConn{}
}

// mockDialer is a mock DomainSocketDialer for testing
type mockDialer struct {
	OutputConn    *mockConn
	OutputErr     error
	InputSockPath string
	DialCallCount int
}

// Dial is a mock that saves off its inputs and returns the mock output in the
// mockDialer struct
func (m *mockDialer) dial(socketPath string) (domainSocketConn, error) {
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

	AssertTrue(t, client != nil, "Expected a real client")
	AssertEqual(t, client.socketPath, testSockPath,
		"Should match the path we passed in")
	AssertFalse(t, client.IsConnected(), "Shouldn't be connected yet")

	// Dialer should be the private implementation type
	_ = client.dialer.(*clientDialer)
}

func TestConnect_Success(t *testing.T) {
	dialer := newMockDialer()
	client := newTestClientConnection(dialer, nil)
	client.sequence = 10

	err := client.Connect()

	AssertTrue(t, err == nil, "Expected no error")
	AssertTrue(t, client.IsConnected(), "Should be connected")
	AssertEqual(t, client.conn, dialer.OutputConn,
		"Expected conn returned from the mock dialer")
	AssertEqual(t, dialer.InputSockPath, testSockPath,
		"Should be using passed-in socket path")
	AssertEqual(t, client.sequence, int64(0),
		"Expected sequence number to have been reset")
}

func TestConnect_Error(t *testing.T) {
	dialer := newMockDialer()
	errStr := "a strange error occurred"
	dialer.SetError(errStr)
	client := newTestClientConnection(dialer, nil)

	err := client.Connect()

	ExpectError(t, err, fmt.Sprintf("dRPC connect: %s", errStr),
		"Expected error from mock dialer")
	AssertFalse(t, client.IsConnected(), "Should not be connected")
	AssertTrue(t, client.conn == nil, "Expected no connection")
}

func TestConnect_AlreadyConnected(t *testing.T) {
	originalConn := newMockConn()
	dialer := newMockDialer()
	client := newTestClientConnection(dialer, originalConn)

	err := client.Connect()

	AssertTrue(t, err == nil, "Expected no error")
	AssertEqual(t, client.conn, originalConn,
		"Connection should be unchanged")
	AssertEqual(t, dialer.DialCallCount, 0,
		"Should not have tried to connect")
}

func TestClose_Success(t *testing.T) {
	conn := newMockConn()
	client := newTestClientConnection(newMockDialer(), conn)

	err := client.Close()

	AssertTrue(t, err == nil, "Expected no error")
	AssertEqual(t, conn.CloseCallCount, 1,
		"Expected conn.Close() to be called")
	AssertFalse(t, client.IsConnected(), "Should not be connected")
}

func TestClose_Error(t *testing.T) {
	conn := newMockConn()
	expectedErr := "conn.Close() failed"
	conn.CloseOutputError = fmt.Errorf(expectedErr)
	client := newTestClientConnection(newMockDialer(), conn)

	err := client.Close()

	ExpectError(t, err,
		fmt.Sprintf("dRPC close: %s", expectedErr),
		"Expected the error from conn.Close()")
	AssertEqual(t, conn.CloseCallCount, 1,
		"Expected conn.Close() to be called")
	AssertTrue(t, client.IsConnected(),
		"Should have left the connection alone")
}

func TestClose_NotConnected(t *testing.T) {
	client := newTestClientConnection(newMockDialer(), nil)

	err := client.Close()

	// Effectively a no-op, but no point reporting failure on a double close
	AssertTrue(t, err == nil, "Should report success")
	AssertFalse(t, client.IsConnected(), "Should not be connected")
}

func TestSendMsg_NilInput(t *testing.T) {
	conn := newMockConn()
	client := newTestClientConnection(newMockDialer(), conn)

	response, err := client.SendMsg(nil)

	AssertTrue(t, response == nil, "Expected no response")
	ExpectError(t, err, "invalid dRPC call", "Expect error on nil input")
}

func marshallCallToBytes(t *testing.T, call *Call) []byte {
	callBytes, protoErr := proto.Marshal(call)
	AssertTrue(t, protoErr == nil, "Failed to marshal Call to bytes")
	return callBytes
}

func marshallResponseToBytes(t *testing.T, resp *Response) []byte {
	respBytes, protoErr := proto.Marshal(resp)
	AssertTrue(t, protoErr == nil, "Failed to marshal Response to bytes")
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

func TestSendMsg_Success(t *testing.T) {
	conn := newMockConn()
	client := newTestClientConnection(newMockDialer(), conn)
	client.sequence = 2 // ClientConnection keeps track of sequence

	call := newTestCall()
	callBytes := conn.SetWriteOutputBytesForCall(t, call)

	expectedResp := newTestResponse(client.sequence + 1)
	conn.SetReadOutputBytesToResponse(t, expectedResp)
	expectedRespBytes := conn.ReadOutputBytes

	response, err := client.SendMsg(call)

	AssertTrue(t, err == nil, "Expected no error")
	AssertTrue(t, response != nil, "Expected a real response")
	AssertEqual(t, response.Sequence, expectedResp.Sequence,
		"Response should match expected")
	AssertEqual(t, response.Status, expectedResp.Status,
		"Response should match expected")
	AssertEqual(t, response.Body, expectedResp.Body,
		"Response should match expected")
	AssertEqual(t, conn.WriteInputBytes, callBytes,
		"Expected call to be marshalled and passed to write")
	AssertTrue(t, conn.ReadInputBytes != nil,
		"Expected read called with buffer")
	AssertEqual(t, conn.ReadInputBytes, expectedRespBytes,
		"Marshalled response should be copied into the buffer")
}

func TestSendMsg_NotConnected(t *testing.T) {
	client := newTestClientConnection(newMockDialer(), nil)

	response, err := client.SendMsg(newTestCall())

	AssertTrue(t, response == nil, "Expected no response")
	ExpectError(t, err, "dRPC not connected",
		"Expected error for unconnected client")
}

func TestSendMsg_WriteError(t *testing.T) {
	conn := newMockConn()
	client := newTestClientConnection(newMockDialer(), conn)

	call := newTestCall()
	conn.WriteOutputNumBytes = 0
	expectedErr := "failed to write message"
	conn.WriteOutputError = fmt.Errorf(expectedErr)

	response, err := client.SendMsg(call)

	AssertTrue(t, response == nil, "Expected no response")
	ExpectError(t, err, fmt.Sprintf("dRPC send: %s", expectedErr),
		"Expected conn.Write() error")
}

func TestSendMsg_ReadError(t *testing.T) {
	conn := newMockConn()
	client := newTestClientConnection(newMockDialer(), conn)

	call := newTestCall()
	conn.SetWriteOutputBytesForCall(t, call)

	conn.ReadOutputNumBytes = 0
	expectedErr := "failed to read response"
	conn.ReadOutputError = fmt.Errorf(expectedErr)

	response, err := client.SendMsg(call)

	AssertTrue(t, response == nil, "Expected no response")
	ExpectError(t, err, fmt.Sprintf("dRPC recv: %s", expectedErr),
		"Expected conn.Read() error")
}

func TestSendMsg_UnmarshalResponseFailure(t *testing.T) {
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

	expectedErr := "failed to unmarshal dRPC response: unexpected EOF"
	AssertTrue(t, response == nil, "Expected no response")
	ExpectError(t, err, expectedErr, "Expected protobuf error")
}
