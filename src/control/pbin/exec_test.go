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
package pbin_test

import (
	"bytes"
	"context"
	"encoding/json"
	"os"
	"testing"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/pbin"
)

func reqRes() {
	conn := pbin.NewStdioConn("child", "parent", os.Stdin, os.Stdout)
	readBuf := make([]byte, pbin.MaxMessageSize)

	readLen, err := conn.Read(readBuf)
	if err != nil {
		childErrExit(err)
	}

	var req pbin.Request
	err = json.Unmarshal(readBuf[:readLen], &req)
	if err != nil {
		childErrExit(err)
	}

	res := pbin.Response{
		Payload: req.Payload,
	}

	writeBuf, err := json.Marshal(&res)
	if err != nil {
		childErrExit(err)
	}

	_, err = conn.Write(writeBuf)
	if err != nil {
		childErrExit(err)
	}
}

func TestPbinExec(t *testing.T) {
	for name, tc := range map[string]struct {
		req     *pbin.Request
		binPath string
		expErr  error
	}{
		"normal exec": {
			req: &pbin.Request{
				Method:  "ping",
				Payload: []byte(`{"reply":"pong"}`),
			},
			binPath: os.Args[0],
		},
		"malformed payload (shouldn't hang)": {
			req: &pbin.Request{
				Method:  "ping",
				Payload: []byte(`pong`),
			},
			binPath: os.Args[0],
			expErr:  errors.New("error calling MarshalJSON"),
		},
		"nil request": {
			expErr: errors.New("nil request"),
		},
		"invalid binPath": {
			req:     &pbin.Request{},
			binPath: "this is not my beautiful house",
			expErr:  errors.New("executable file not found"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			os.Setenv(childModeEnvVar, childModeReqRes)

			ctx := context.Background()
			res, err := pbin.ExecReq(ctx, log, tc.binPath, tc.req)

			common.CmpErr(t, tc.expErr, err)

			if err == nil && !bytes.Equal(tc.req.Payload, res.Payload) {
				t.Fatalf("payloads differ: %q != %q", tc.req.Payload, res.Payload)
			}
		})
	}
}
