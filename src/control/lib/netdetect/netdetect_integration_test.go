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

// +build concurrency

package netdetect

import (
	"context"
	"fmt"
	"math/rand"
	"sync"
	"syscall"
	"testing"
	"time"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
)

const maxConcurrent = 1000

func getSocketID(t *testing.T, pid int32, wg *sync.WaitGroup) {
	defer wg.Done()

	netCtx, err := Init(context.Background())
	defer CleanUp(netCtx)
	common.AssertEqual(t, err, nil, fmt.Sprintf("Failed to initialize NetDetectContext: %v", err))

	numaNode, err := GetNUMASocketIDForPid(netCtx, int32(pid))
	common.AssertEqual(t, err, nil, fmt.Sprintf("GetNUMASocketIDForPid error on NUMA %d / pid %d", numaNode, pid))
}

// TestConcurrentGetNUMASocket launches maxConcurrent go routines
// that concurrently access the hwloc topology.  This test verifies that there are
// no race conditions when the topology is initialized on each access
func TestConcurrentGetNUMASocket(t *testing.T) {
	_, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	var wg sync.WaitGroup
	pid := syscall.Getpid()
	rand.Seed(time.Now().UnixNano())
	for i := 0; i < maxConcurrent; i++ {
		wg.Add(1)
		go func(n int32) {
			time.Sleep(time.Duration(rand.Intn(100)) * time.Microsecond)
			getSocketID(t, n, &wg)
		}(int32(pid))
	}
	wg.Wait()
}

func getSocketIDWithContext(t *testing.T, pid int32, wg *sync.WaitGroup, ctx context.Context) {
	defer wg.Done()
	numaNode, err := GetNUMASocketIDForPid(ctx, int32(pid))
	common.AssertEqual(t, err, nil, fmt.Sprintf("GetNUMASocketIDForPid error on NUMA %d / pid %d", numaNode, pid))
}

// TestConcurrentGetNUMASocket launches maxConcurrent go routines
// that concurrently access the hwloc topology.  This test verifies that there are
// no race conditions when using a shared topology pointer
func TestConcurrentGetNUMASocketWithContext(t *testing.T) {
	_, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	netCtx, err := Init(context.Background())
	defer CleanUp(netCtx)
	common.AssertEqual(t, err, nil, fmt.Sprintf("Failed to initialize NetDetectContext: %v", err))

	var wg sync.WaitGroup
	pid := syscall.Getpid()
	rand.Seed(time.Now().UnixNano())
	for i := 0; i < maxConcurrent; i++ {
		wg.Add(1)
		go func(n int32, ctx context.Context) {
			time.Sleep(time.Duration(rand.Intn(100)) * time.Microsecond)
			getSocketIDWithContext(t, n, &wg, ctx)
		}(int32(pid), netCtx)
	}
	wg.Wait()
}
