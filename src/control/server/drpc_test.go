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

package server

import (
	"context"
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestCheckDrpcClientSocketPath_Empty(t *testing.T) {
	err := checkDrpcClientSocketPath("")

	if err == nil {
		t.Fatal("Expected an error, got nil")
	}
}

func TestCheckDrpcClientSocketPath_BadPath(t *testing.T) {
	err := checkDrpcClientSocketPath("/not/a/real/path")

	if err == nil {
		t.Fatal("Expected an error, got nil")
	}
}

func TestCheckDrpcClientSocketPath_DirNotSocket(t *testing.T) {
	tmpDir, tmpCleanup := common.CreateTestDir(t)
	defer tmpCleanup()

	path := filepath.Join(tmpDir, "drpc_test.sock")
	err := os.Mkdir(path, 0755)
	if err != nil {
		t.Fatalf("Failed to create directory: %v", err)
	}
	defer os.Remove(path)

	err = checkDrpcClientSocketPath(path)

	if err == nil {
		t.Fatal("Expected an error, got nil")
	}
}

func TestCheckDrpcClientSocketPath_FileNotSocket(t *testing.T) {
	tmpDir, tmpCleanup := common.CreateTestDir(t)
	defer tmpCleanup()

	path := filepath.Join(tmpDir, "drpc_test.sock")
	f, err := os.Create(path)
	if err != nil {
		t.Fatalf("Failed to create temp file: %v", err)
	}
	f.Close()
	defer os.Remove(path)

	err = checkDrpcClientSocketPath(path)

	if err == nil {
		t.Fatal("Expected an error, got nil")
	}
}

func TestCheckDrpcClientSocketPath_Success(t *testing.T) {
	tmpDir, tmpCleanup := common.CreateTestDir(t)
	defer tmpCleanup()

	path := filepath.Join(tmpDir, "drpc_test.sock")
	_, cleanup := common.CreateTestSocket(t, path)
	defer cleanup()

	err := checkDrpcClientSocketPath(path)

	if err != nil {
		t.Fatalf("Expected no error, got error: %v", err)
	}
}

func TestGetDrpcServerSocketPath_EmptyString(t *testing.T) {
	expectedPath := "daos_server.sock"

	path := getDrpcServerSocketPath("")

	if path != expectedPath {
		t.Errorf("Expected %q, got %q", expectedPath, path)
	}
}

func TestGetDrpcServerSocketPath(t *testing.T) {
	dirPath := "/some/server/dir"
	expectedPath := filepath.Join(dirPath, "daos_server.sock")

	path := getDrpcServerSocketPath(dirPath)

	if path != expectedPath {
		t.Errorf("Expected %q, got %q", expectedPath, path)
	}
}

func TestDrpcCleanup_BadSocketDir(t *testing.T) {
	badDir := "/some/fake/path"

	err := drpcCleanup(badDir)

	if err == nil {
		t.Fatal("Expected error, got nil")
	}
}

func TestDrpcCleanup_EmptyDir(t *testing.T) {
	tmpDir, tmpCleanup := common.CreateTestDir(t)
	defer tmpCleanup()

	err := drpcCleanup(tmpDir)

	if err != nil {
		t.Fatalf("Expected no error, got: %v", err)
	}
}

func expectDoesNotExist(t *testing.T, path string) {
	if _, err := os.Stat(path); err == nil || !os.IsNotExist(err) {
		t.Errorf("expected %q to no longer exist, but got error: %v",
			path, err)
	}
}

func TestDrpcCleanup_Single(t *testing.T) {
	tmpDir, tmpCleanup := common.CreateTestDir(t)
	defer tmpCleanup()

	for _, sockName := range []string{
		"daos_server.sock",
		"daos_io_server.sock",
		"daos_io_server0.sock",
		"daos_io_server_2345.sock",
	} {
		sockPath := filepath.Join(tmpDir, sockName)
		_, cleanup := common.CreateTestSocket(t, sockPath)
		defer cleanup()

		err := drpcCleanup(tmpDir)

		if err != nil {
			t.Fatalf("%q: Expected no error, got: %v", sockPath, err)
		}

		expectDoesNotExist(t, sockPath)
	}
}

func TestDrpcCleanup_DoesNotDeleteNonDaosSocketFiles(t *testing.T) {
	tmpDir, tmpCleanup := common.CreateTestDir(t)
	defer tmpCleanup()

	for _, sockName := range []string{
		"someone_else.sock",
		"12345.sock",
		"myfile",
		"daos_server",
		"daos_io_server",
	} {
		sockPath := filepath.Join(tmpDir, sockName)
		_, cleanup := common.CreateTestSocket(t, sockPath)
		defer cleanup()

		err := drpcCleanup(tmpDir)

		if err != nil {
			t.Fatalf("%q: Expected no error, got: %v", sockPath, err)
		}

		if _, err := os.Stat(sockPath); err != nil {
			t.Errorf("expected %q to exist, but got error: %v",
				sockPath, err)
		}
	}
}

func TestDrpcCleanup_Multiple(t *testing.T) {
	tmpDir, tmpCleanup := common.CreateTestDir(t)
	defer tmpCleanup()

	sockNames := []string{
		"daos_server.sock",
		"daos_io_server.sock",
		"daos_io_server12.sock",
		"daos_io_serverF.sock",
		"daos_io_server_5678.sock",
		"daos_io_server_256.sock",
		"daos_io_server_abc.sock",
	}

	var sockPaths []string

	for _, sockName := range sockNames {
		path := filepath.Join(tmpDir, sockName)
		sockPaths = append(sockPaths, path)

		_, cleanup := common.CreateTestSocket(t, path)
		defer cleanup()
	}

	err := drpcCleanup(tmpDir)

	if err != nil {
		t.Fatalf("Expected no error, got: %v", err)
	}

	for _, path := range sockPaths {
		expectDoesNotExist(t, path)
	}
}

func TestDrpc_Errors(t *testing.T) {
	for name, tc := range map[string]struct {
		notReady     bool
		connectError error
		sendError    error
		resp         *drpc.Response
		expErr       error
	}{
		"connect fails": {
			connectError: errors.New("connect"),
			expErr:       errors.New("connect"),
		},
		"send msg fails": {
			sendError: errors.New("send"),
			expErr:    errors.New("send"),
		},
		"nil resp": {
			expErr: errors.New("no response"),
		},
		"failed status": {
			resp: &drpc.Response{
				Status: drpc.Status_FAILURE,
			},
			expErr: errors.New("status: FAILURE"),
		},
		"success": {
			resp: &drpc.Response{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			cfg := &mockDrpcClientConfig{
				SendMsgError:    tc.sendError,
				SendMsgResponse: tc.resp,
				ConnectError:    tc.connectError,
			}
			mc := newMockDrpcClient(cfg)

			_, err := makeDrpcCall(context.TODO(), log,
				mc, drpc.MethodPoolCreate,
				&mgmtpb.PoolCreateReq{})
			common.CmpErr(t, tc.expErr, err)
		})
	}
}

func TestServer_DrpcRetryCancel(t *testing.T) {
	for name, tc := range map[string]struct {
		req          proto.Message
		resp         proto.Message
		method       drpc.Method
		timeout      time.Duration
		shouldCancel bool
		expErr       error
	}{
		"retries exceed deadline": {
			req: &retryableDrpcReq{
				Message:    &mgmtpb.PoolDestroyReq{},
				RetryAfter: 1 * time.Microsecond,
				RetryableStatuses: []drpc.DaosStatus{
					drpc.DaosBusy,
				},
			},
			resp: &mgmtpb.PoolDestroyResp{
				Status: int32(drpc.DaosBusy),
			},
			method:  drpc.MethodPoolDestroy,
			timeout: 10 * time.Microsecond,
			expErr:  context.DeadlineExceeded,
		},
		"canceled request": {
			req: &retryableDrpcReq{
				Message:    &mgmtpb.PoolDestroyReq{},
				RetryAfter: 1 * time.Microsecond,
				RetryableStatuses: []drpc.DaosStatus{
					drpc.DaosBusy,
				},
			},
			resp: &mgmtpb.PoolDestroyResp{
				Status: int32(drpc.DaosBusy),
			},
			method:       drpc.MethodPoolDestroy,
			timeout:      1 * time.Second,
			shouldCancel: true,
			expErr:       context.Canceled,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			body, err := proto.Marshal(tc.resp)
			if err != nil {
				t.Fatal(err)
			}
			cfg := &mockDrpcClientConfig{
				SendMsgResponse: &drpc.Response{
					Body: body,
				},
			}
			mc := newMockDrpcClient(cfg)

			ctx, cancel := context.WithTimeout(context.Background(), tc.timeout)
			defer cancel()
			if tc.shouldCancel {
				cancel()
			}

			_, err = makeDrpcCall(ctx, log, mc, tc.method, tc.req)
			common.CmpErr(t, tc.expErr, err)
		})
	}
}
