//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
package pbin

import (
	"context"
	"encoding/json"
	"io"
	"os"
	"os/exec"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/logging"
)

const (
	// MessageBufferSize is the starting size of the receive buffer. If
	// the buffer must be grown to accommodate larger messages, it is
	// expanded in increments of this value.
	MessageBufferSize = 1024
	// MaxMessageSize is the largest single message that can be written to
	// or read from the privileged binary.
	MaxMessageSize = MessageBufferSize * 1024
)

type (
	// Request represents a request sent to the privileged binary. The
	// payload field contains a JSON-encoded representation of the wrapped
	// request.
	Request struct {
		Method  string
		Payload json.RawMessage
	}

	// Response represents a response received from the privileged binary. The
	// payload field contains a JSON-encoded representation of the wrapped response.
	Response struct {
		Error   *fault.Fault
		Payload json.RawMessage
	}

	cmdLogger struct {
		logFn  func(string)
		prefix string
	}
)

func (cl *cmdLogger) Write(data []byte) (int, error) {
	if cl.logFn == nil {
		return 0, errors.New("no log function set in cmdLogger")
	}

	var msg string
	if cl.prefix != "" {
		msg = cl.prefix + " "
	}
	msg += string(data)
	cl.logFn(msg)
	return len(data), nil
}

func decodeResponse(resBuf []byte) (*Response, error) {
	res := new(Response)
	err := json.Unmarshal(resBuf, res)
	if err == nil {
		return res, nil
	}

	switch err.(type) {
	case *json.SyntaxError:
		// Try to pull out a valid JSON payload.
		start := strings.Index(string(resBuf), "{")
		end := strings.LastIndex(string(resBuf), "}")
		if end < 0 {
			break
		}
		end += 1
		resBuf = resBuf[start:end]
		err = json.Unmarshal(resBuf, res)
		if err == nil {
			return res, nil
		}
	}

	return nil, errors.Wrapf(err, "pbin failed to decode response data(%q)", resBuf)
}

// ReadMessage attempts to read a message from the sender and
// returns a buffer containing the message if successful. Relies
// on the writer being closed so that the reader gets an io.EOF
// to signal that the message is complete.
func ReadMessage(conn io.Reader) ([]byte, error) {
	readBuf := make([]byte, MessageBufferSize)

	startIdx := 0
	for {
		readLen, err := conn.Read(readBuf[startIdx:])
		startIdx += readLen
		if err != nil {
			if err == io.EOF && startIdx > 0 {
				break
			}
			return nil, err
		}

		if len(readBuf)+MessageBufferSize > MaxMessageSize {
			return nil, errors.New("max message size exceeded in ReadMessage()")
		}

		readBuf = append(readBuf, make([]byte, MessageBufferSize)...)
	}

	return readBuf[:startIdx], nil
}

// ExecReq executes the supplied Request by starting a child process
// to service the request. Returns a Response if successful.
func ExecReq(parent context.Context, log logging.Logger, binPath string, req *Request) (res *Response, err error) {
	if req == nil {
		return nil, errors.New("nil request")
	}

	ctx, killChild := context.WithCancel(parent)
	defer killChild()

	child := exec.CommandContext(ctx, binPath)
	child.Stderr = &cmdLogger{
		logFn:  log.Error,
		prefix: binPath,
	}
	toChild, err := child.StdinPipe()
	if err != nil {
		return nil, err
	}
	fromChild, err := child.StdoutPipe()
	if err != nil {
		return nil, err
	}
	conn := NewStdioConn("server", binPath, fromChild, toChild)

	// ensure that /usr/sbin is in $PATH
	os.Setenv("PATH", os.Getenv("PATH")+":/usr/sbin")
	child.Env = os.Environ()

	if err := child.Start(); err != nil {
		return nil, err
	}
	// make sure we reap any children on the way out
	defer func() {
		// If there was an error, kill the child so that it can't
		// hang around waiting for input.
		if err != nil {
			killChild()
			return
		}

		// Otherwise, the child should exit normally.
		err = child.Wait()
	}()

	sendData, err := json.Marshal(req)
	if err != nil {
		return nil, err
	}
	if _, err := conn.Write(sendData); err != nil {
		return nil, errors.Wrap(err, "pbin write failed")
	}
	// Signal to the receiver that we're finished.
	if err := conn.CloseWrite(); err != nil {
		return nil, errors.Wrap(err, "pbin CloseWrite failed")
	}

	maxReadAttempts := 5
	for readCount := 0; readCount < maxReadAttempts; readCount++ {
		recvData, err := ReadMessage(conn)
		if err != nil && err != io.EOF {
			return nil, errors.Wrap(err, "failed to read message from sender")
		}

		res, err = decodeResponse(recvData)
		if err != nil {
			log.Debugf("discarding garbage response %q", recvData)
			continue
		}

		break
	}

	switch {
	case res == nil:
		return nil, errors.Errorf("Unable to decode response after %d attempts", maxReadAttempts)
	case res.Error != nil:
		return nil, res.Error
	default:
		return res, nil
	}
}
