//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
package pbin_test

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io/ioutil"
	"os"
	"testing"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/pbin"
)

func reqRes() {
	conn := pbin.NewStdioConn("child", "parent", os.Stdin, os.Stdout)

	readBuf, err := pbin.ReadMessage(conn)
	if err != nil {
		childErrExit(err)
	}

	var req pbin.Request
	if err = json.Unmarshal(readBuf, &req); err != nil {
		childErrExit(err)
	}

	var writeBuf []byte
	switch req.Method {
	case "garbage":
		writeBuf = []byte(`junk`)
	case "oversize":
		writeBuf = generatePayload(pbin.MaxMessageSize + 1)
	default:
		res := pbin.Response{
			Payload: req.Payload,
		}

		if req.Method == "Ping" {
			childVersion := os.Getenv(childVersionEnvVar)
			res.Payload, err = json.Marshal(pbin.PingResp{Version: childVersion})
			if err != nil {
				childErrExit(err)
			}
		}

		writeBuf, err = json.Marshal(&res)
		if err != nil {
			childErrExit(err)
		}
	}

	if _, err = conn.Write(writeBuf); err != nil {
		childErrExit(err)
	}

	if err = conn.CloseWrite(); err != nil {
		childErrExit(err)
	}
}

func generatePayload(size int) []byte {
	var buf bytes.Buffer
	buf.WriteString(`{"reply":"`)
	post := `"}`
	adjSize := size - len(post)

	for buf.Len() < adjSize {
		buf.WriteString("0123456789abcdefghijklmnopqrstuvxyz")
	}

	ret := buf.Bytes()
	if len(ret) > adjSize {
		ret = ret[:adjSize]
	}
	ret = append(ret, []byte(post)...)

	return ret
}

func loadJSONPayload(t *testing.T, name string) []byte {
	loadBuf, err := ioutil.ReadFile(fmt.Sprintf("testdata/%s.json", name))
	if err != nil {
		t.Fatal(err)
	}

	// make sure that the expected payload is formatted the
	// same as the test payload
	var outBuf bytes.Buffer
	if err := json.Compact(&outBuf, loadBuf); err != nil {
		t.Fatal(err)
	}
	return outBuf.Bytes()
}

func TestPbin_Exec(t *testing.T) {
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
		},
		"giant payload": {
			req: &pbin.Request{
				Method:  "ping",
				Payload: generatePayload((pbin.MessageBufferSize * 512) + 1),
			},
		},
		"DAOS-4185 scan payload": {
			req: &pbin.Request{
				Method:  "scan",
				Payload: loadJSONPayload(t, "boro-84-storage_scan"),
			},
		},
		"oversize response payload": {
			req: &pbin.Request{
				Method:  "oversize",
				Payload: []byte(`{"reply":"pong"}`),
			},
			expErr: errors.New("size exceeded"),
		},
		"garbage response": {
			req: &pbin.Request{
				Method:  "garbage",
				Payload: []byte(`{"reply":"garbage"}`),
			},
			expErr: errors.New("decode response"),
		},
		"malformed payload (shouldn't hang)": {
			req: &pbin.Request{
				Method:  "ping",
				Payload: []byte(`pong`),
			},
			expErr: errors.New("error calling MarshalJSON"),
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
			if tc.binPath == "" {
				tc.binPath = os.Args[0]
			}

			ctx := context.Background()
			res, err := pbin.ExecReq(ctx, log, tc.binPath, tc.req)

			common.CmpErr(t, tc.expErr, err)

			if err == nil && !bytes.Equal(tc.req.Payload, res.Payload) {
				t.Fatalf("payloads differ: %q != %q", tc.req.Payload, res.Payload)
			}
		})
	}
}
