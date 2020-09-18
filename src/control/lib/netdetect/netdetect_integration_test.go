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

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
)

const maxConcurrent = 1000

func getSocketID(ctx context.Context, t *testing.T, pid int32, wg *sync.WaitGroup) error {
	defer wg.Done()
	numaNode, err := GetNUMASocketIDForPid(ctx, int32(pid))
	if err != nil {
		return errors.Errorf("GetNUMASocketIDForPid error on NUMA %d / pid %d / error: %v", numaNode, pid, err)
	}
	return nil
}

// TestConcurrentGetNUMASocket launches go routines
// that concurrently call GetNUMASocketIDForPid() by itself to verify that there are
// no race conditions
func TestConcurrentGetNUMASocket(t *testing.T) {
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
		go func(ctx context.Context, n int32) {
			time.Sleep(time.Duration(rand.Intn(100)) * time.Microsecond)
			err := getSocketID(ctx, t, n, &wg)
			common.AssertEqual(t, err, nil, fmt.Sprintf("getSocketIDWithContext error: %v", err))

		}(netCtx, int32(pid))
	}
	wg.Wait()
}

func getSocketAndScanFabric(ctx context.Context, t *testing.T, pid int32, wg *sync.WaitGroup) error {
	defer wg.Done()
	numaNode, err := GetNUMASocketIDForPid(ctx, int32(pid))
	if err != nil {
		return errors.Errorf("GetNUMASocketIDForPid error on NUMA %d / pid %d / error: %v", numaNode, pid, err)
	}

	provider := ""
	results, err := ScanFabric(ctx, provider)
	if err != nil {
		return err
	}

	if len(results) == 0 {
		return errors.Errorf("len was 0")
	}
	return err
}

// TestConcurrentGetNUMASocketAndScanFabric launches go routines
// that concurrently call GetNUMASocketIDForPid() and ScanFabric() together to verify that there are
// no race conditions when called in this sequence
func TestConcurrentGetNUMASocketAndScanFabric(t *testing.T) {
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
		go func(ctx context.Context, n int32) {
			time.Sleep(time.Duration(rand.Intn(100)) * time.Microsecond)
			err := getSocketAndScanFabric(ctx, t, n, &wg)
			common.AssertEqual(t, err, nil, fmt.Sprintf("getSocketIDWithContext error: %v", err))

		}(netCtx, int32(pid))
	}
	wg.Wait()
}

func getScanFabric(ctx context.Context, t *testing.T, wg *sync.WaitGroup) error {
	defer wg.Done()
	provider := ""
	results, err := ScanFabric(ctx, provider)
	if err != nil {
		return err
	}

	if len(results) == 0 {
		return errors.Errorf("len was 0")
	}
	return err
}

// TestConcurrentScanFabric launches go routines
// that concurrently call ScanFabric() by itself to verify that there are
// no race conditions
func TestConcurrentScanFabric(t *testing.T) {
	_, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	netCtx, err := Init(context.Background())
	defer CleanUp(netCtx)
	common.AssertEqual(t, err, nil, fmt.Sprintf("Failed to initialize NetDetectContext: %v", err))

	var wg sync.WaitGroup
	rand.Seed(time.Now().UnixNano())
	r := rand.Intn(1500)
	for i := 0; i < 2*maxConcurrent+r; i++ {
		wg.Add(1)
		go func(ctx context.Context) {
			time.Sleep(time.Duration(rand.Intn(100)) * time.Microsecond)
			err := getScanFabric(ctx, t, &wg)
			common.AssertEqual(t, err, nil, fmt.Sprintf("ScanFabric failure: %v", err))

		}(netCtx)
	}
	wg.Wait()
}
