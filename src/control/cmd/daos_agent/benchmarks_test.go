//
// (C) Copyright 2020 Intel Corporation.
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

package main

import (
	"context"
	"fmt"
	"os"
	"syscall"
	"testing"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/cmd/daos_agent/benchmarker"
	"github.com/daos-stack/daos/src/control/common"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/logging"
)

const (
	childModeVar         = "GO_TESTING_CHILD_MODE"
	childModeAppBench    = "MODE_APP_BENCH"
	agentSockVar         = "DAOS_AGENT_DRPC_DIR"
	agentDisableCacheVar = "DAOS_AGENT_DISABLE_CACHE"
	clientLogFileVar     = "D_LOG_FILE"
)

var mockMSResponse *control.UnaryResponse

type noOpInvoker struct {
	log logging.Logger
}

func (noi *noOpInvoker) InvokeUnaryRPC(ctx context.Context, req control.UnaryRequest) (*control.UnaryResponse, error) {
	return mockMSResponse, nil
}

func (noi *noOpInvoker) InvokeUnaryRPCAsync(ctx context.Context, req control.UnaryRequest) (control.HostResponseChan, error) {
	return nil, nil
}

func (noi *noOpInvoker) Debug(msg string) {
	noi.log.Debug(msg)
}

func (noi *noOpInvoker) Debugf(msg string, args ...interface{}) {
	noi.log.Debugf(msg, args...)
}

func (noi noOpInvoker) SetConfig(_ *control.Config) {}

func startAgent(ctx context.Context, log logging.Logger, cfg *Config) {
	cmd := &startCmd{}
	cmd.setLog(log)
	cmd.setConfig(cfg)
	cmd.setContext(ctx)
	cmd.setInvoker(&noOpInvoker{
		log: log,
	})

	go func() {
		if err := cmd.Execute(nil); err != nil {
			log.Errorf("startCmd exited: %s", err)
		}
	}()
}

func childErrExit(err error) {
	if err == nil {
		err = errors.New("unknown error")
	}
	fmt.Fprintf(os.Stderr, "CHILD ERROR: %s\n", err)
	os.Exit(1)
}

func TestMain(m *testing.M) {
	mode := os.Getenv(childModeVar)
	switch mode {
	case "":
		// default; run the test binary
		os.Exit(m.Run())
	case childModeAppBench:
		benchmarker.BenchMain()
	default:
		childErrExit(errors.Errorf("Unknown child mode: %q", mode))
	}
}

func setupBenchmark(ctx context.Context, b *testing.B) ([]string, func()) {
	log, buf := logging.NewTestLogger(b.Name())
	testDir, cleanTd := common.CreateTestDir(&testing.T{})

	aCfg := DefaultConfig()
	aCfg.RuntimeDir = testDir
	aCfg.TransportConfig.AllowInsecure = true

	startAgent(ctx, log, aCfg)

	mockMSResponse = control.MockMSResponse("", nil, &mgmtpb.GetAttachInfoResp{
		Psrs: []*mgmtpb.GetAttachInfoResp_Psr{
			{
				Rank: 0,
				Uri:  "ofi+sockets://127.0.0.1:12345",
			},
		},
		Provider: "ofi+sockets",
	})

	env := []string{
		clientLogFileVar + "=/dev/null",
		"LD_LIBRARY_PATH=" + os.Getenv("LD_LIBRARY_PATH"),
		childModeVar + "=" + childModeAppBench,
		agentSockVar + "=" + testDir,
	}

	return env, func() {
		cleanTd()
		if b.Failed() {
			b.Log(buf.String())
		}
	}
}

func runApp(b *testing.B, env []string) {
	b.StopTimer()
	var ws syscall.WaitStatus
	args := os.Args[0:1]
	pid, err := syscall.ForkExec(os.Args[0], args, &syscall.ProcAttr{
		Env: env,
		Files: []uintptr{
			os.Stdin.Fd(),
			os.Stdout.Fd(),
			os.Stderr.Fd(),
		},
	})
	if err != nil {
		b.Fatal(err)
	}
	b.StartTimer()
	if _, err := syscall.Wait4(pid, &ws, 0, nil); err != nil {
		b.Fatal(err)
	}
	if ws.ExitStatus() != 0 {
		b.Fatalf("exited with %d", ws.ExitStatus())
	}
}

func BenchmarkAppSerial_CacheDisabled(b *testing.B) {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	env, cleanup := setupBenchmark(ctx, b)
	defer cleanup()

	env = append(env, agentDisableCacheVar+"="+"true")
	b.ResetTimer()
	for n := 0; n < b.N; n++ {
		runApp(b, env)
	}
}

func BenchmarkAppSerial(b *testing.B) {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	env, cleanup := setupBenchmark(ctx, b)
	defer cleanup()

	b.ResetTimer()
	for n := 0; n < b.N; n++ {
		runApp(b, env)
	}
}

func BenchmarkAppParallel(b *testing.B) {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	env, cleanup := setupBenchmark(ctx, b)
	defer cleanup()

	b.ResetTimer()
	b.RunParallel(func(pb *testing.PB) {
		for pb.Next() {
			runApp(b, env)
		}
	})
}

func BenchmarkAppParallel_CacheDisabled(b *testing.B) {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	env, cleanup := setupBenchmark(ctx, b)
	defer cleanup()

	env = append(env, agentDisableCacheVar+"="+"true")
	b.ResetTimer()
	b.RunParallel(func(pb *testing.PB) {
		for pb.Next() {
			runApp(b, env)
		}
	})
}
