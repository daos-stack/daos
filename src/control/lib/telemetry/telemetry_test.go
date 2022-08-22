//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package telemetry

import (
	"context"
	"fmt"
	"sync"
	"testing"
	"time"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
)

func TestTelemetry_Init(t *testing.T) {
	producerID := NextTestID()
	InitTestMetricsProducer(t, producerID, 2048)
	defer CleanupTestMetricsProducer(t)

	for name, tc := range map[string]struct {
		id     int
		expErr error
	}{
		"bad ID": {
			id:     producerID + 1,
			expErr: errors.New("no shared memory segment"),
		},
		"success": {
			id: producerID,
		},
	} {
		t.Run(name, func(t *testing.T) {
			ctx, cancelCtx := context.WithCancel(context.Background())
			defer cancelCtx()

			newCtx, err := Init(ctx, uint32(tc.id))
			if newCtx != nil {
				defer Detach(newCtx)
			}

			test.CmpErr(t, tc.expErr, err)

			if tc.expErr == nil { // success
				// Verify the initialized context
				hdl, err := getHandle(newCtx)
				if err != nil {
					t.Fatalf("can't get handle from result ctx: %v", err)
				}

				test.AssertEqual(t, uint32(producerID), hdl.idx, "handle.idx doesn't match shmem ID")

				hdl.RLock()
				defer hdl.RUnlock()
				test.AssertTrue(t, hdl.isValid(), "handle is not valid")
			}
		})
	}
}

func TestTelemetry_Detach(t *testing.T) {
	producerID := NextTestID()
	InitTestMetricsProducer(t, producerID, 2048)
	defer CleanupTestMetricsProducer(t)

	// Invalid context
	Detach(context.Background())

	// success
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	newCtx, err := Init(ctx, uint32(producerID))
	if err != nil {
		t.Fatalf("Init failed: %v", err)
	}
	Detach(newCtx)

	hdl, err := getHandle(newCtx)
	if err != nil {
		t.Fatalf("can't get handle from result ctx: %v", err)
	}

	hdl.RLock()
	test.AssertFalse(t, hdl.isValid(), "handle should be invalidated")
	hdl.RUnlock()

	// previously invalidated handle/context
	Detach(newCtx)
	hdl.RLock()
	test.AssertFalse(t, hdl.isValid(), "handle should still be invalid")
	hdl.RUnlock()
}

func TestTelemetry_GetAPIVersion(t *testing.T) {
	ver := GetAPIVersion()

	test.AssertEqual(t, ver, 1, "wrong API version")
}

func initCtxReal(t *testing.T, id uint32) context.Context {
	t.Helper()

	ctx, err := Init(context.Background(), id)
	if err != nil {
		t.Fatalf("Init failed: %v", err)
	}

	return ctx
}

func teardownCtxReal(_ *testing.T, ctx context.Context) {
	Detach(ctx)
}

func TestTelemetry_GetRank(t *testing.T) {
	rankHdl := &handle{
		rank: new(uint32),
	}
	*rankHdl.rank = 6

	for name, tc := range map[string]struct {
		metrics     TestMetricsMap
		setupCtx    func(t *testing.T, id uint32) context.Context
		teardownCtx func(t *testing.T, ctx context.Context)
		expResult   uint32
		expErr      error
	}{
		"nil handle": {
			setupCtx: func(_ *testing.T, _ uint32) context.Context {
				return context.WithValue(context.Background(), handleKey, nil)
			},
			teardownCtx: func(_ *testing.T, _ context.Context) {},
			expErr:      errors.New("no handle"),
		},
		"handle has rank": {
			setupCtx: func(_ *testing.T, _ uint32) context.Context {
				return context.WithValue(context.Background(), handleKey, rankHdl)
			},
			teardownCtx: func(_ *testing.T, _ context.Context) {},
			expResult:   *rankHdl.rank,
		},
		"no rank metric": {
			expErr: errors.New("unable to find metric named \"/rank\""),
		},
		"fetched rank metric": {
			metrics: TestMetricsMap{
				MetricTypeGauge: {
					Name: "rank",
					Cur:  4,
				},
			},
			expResult: 4,
		},
	} {
		t.Run(name, func(t *testing.T) {
			producerID := NextTestID()
			InitTestMetricsProducer(t, producerID, 2048)
			defer CleanupTestMetricsProducer(t)

			if tc.metrics != nil {
				AddTestMetrics(t, tc.metrics)
			}

			if tc.setupCtx == nil {
				tc.setupCtx = initCtxReal
			}
			if tc.teardownCtx == nil {
				tc.teardownCtx = teardownCtxReal
			}

			ctx := tc.setupCtx(t, uint32(producerID))
			defer tc.teardownCtx(t, ctx)

			result, err := GetRank(ctx)

			test.CmpErr(t, tc.expErr, err)
			test.AssertEqual(t, tc.expResult, result, "rank result")
		})
	}
}

func TestTelemetry_CollectMetrics(t *testing.T) {
	testMetrics := TestMetricsMap{
		MetricTypeCounter: &TestMetric{
			Name: "collect_test/my_counter",
			Cur:  12,
		},
		MetricTypeGauge: &TestMetric{
			Name: "collect_test/my_gauge",
			Cur:  2020,
		},
		MetricTypeStatsGauge: &TestMetric{
			Name: "collect_test/my_stats_gauge",
			Cur:  4242,
			min:  10,
			max:  5000,
		},
		MetricTypeTimestamp: &TestMetric{
			Name: "ts",
		},
		MetricTypeSnapshot: &TestMetric{
			Name: "snapshot",
		},
		MetricTypeDuration: &TestMetric{
			Name: "dur",
			Cur:  float64(time.Millisecond),
		},
	}

	for name, tc := range map[string]struct {
		metrics     TestMetricsMap
		setupCtx    func(t *testing.T, id uint32) context.Context
		teardownCtx func(t *testing.T, ctx context.Context)
		expMetrics  TestMetricsMap
		expErr      error
	}{
		"no handle": {
			setupCtx: func(_ *testing.T, _ uint32) context.Context {
				return context.Background()
			},
			teardownCtx: func(_ *testing.T, _ context.Context) {},
			expErr:      errors.New("no handle"),
		},
		"nil invalid handle": {
			setupCtx: func(t *testing.T, id uint32) context.Context {
				t.Helper()

				ctx := initCtxReal(t, id)

				// clear the root node in the real handle
				hdl, err := getHandle(ctx)
				if err != nil {
					t.Fatalf("failed to get handle: %v", err)
				}
				hdl.Lock()
				defer hdl.Unlock()
				hdl.root = nil

				return ctx
			},
			expErr: errors.New("invalid handle"),
		},
		"no metrics": {
			expMetrics: TestMetricsMap{},
		},
		"success": {
			metrics:    testMetrics,
			expMetrics: testMetrics,
		},
	} {
		t.Run(name, func(t *testing.T) {
			producerID := NextTestID()
			InitTestMetricsProducer(t, producerID, 2048)
			defer CleanupTestMetricsProducer(t)

			if tc.metrics != nil {
				AddTestMetrics(t, tc.metrics)
			}

			if tc.setupCtx == nil {
				tc.setupCtx = initCtxReal
			}
			if tc.teardownCtx == nil {
				tc.teardownCtx = teardownCtxReal
			}

			ctx := tc.setupCtx(t, uint32(producerID))
			defer tc.teardownCtx(t, ctx)

			ch := make(chan Metric)

			// poll in parallel to avoid blocking the channel
			gotMetrics := make([]Metric, 0)
			var wg sync.WaitGroup
			wg.Add(1)
			go func() {
				for metric := range ch {
					gotMetrics = append(gotMetrics, metric)
				}
				wg.Done()
			}()

			s := NewSchema()
			err := CollectMetrics(ctx, s, ch)

			test.CmpErr(t, tc.expErr, err)

			wg.Wait()
			test.AssertEqual(t, len(tc.expMetrics), len(gotMetrics), "got wrong number of metrics")

			for _, exp := range tc.expMetrics {
				expPath := fmt.Sprintf("ID: %d/%s", producerID, exp.Name)

				found := false
				for _, m := range gotMetrics {
					fullPath := fmt.Sprintf("%s/%s", m.Path(), m.Name())
					if fullPath == expPath {
						found = true
						break
					}
				}

				if !found {
					t.Errorf("expected Metric %q not found", expPath)
				}
			}
		})
	}
}

func TestTelemetry_garbageCollection(t *testing.T) {
	validCtx, _ := setupTestMetrics(t)
	defer cleanupTestMetrics(validCtx, t)

	invalidCtx := context.WithValue(context.Background(), handleKey, &handle{})
	testTimeout := 5 * time.Second
	loopInterval := time.Millisecond

	for name, tc := range map[string]struct {
		ctx         context.Context
		cancelAfter time.Duration
	}{
		"no handle": {
			ctx: context.Background(),
		},
		"handle invalid": {
			ctx: invalidCtx,
		},
		"canceled": {
			ctx:         validCtx,
			cancelAfter: time.Microsecond,
		},
		"at least one run": {
			ctx:         validCtx,
			cancelAfter: loopInterval * 3,
		},
	} {
		t.Run(name, func(t *testing.T) {
			ctx, cancel := context.WithTimeout(tc.ctx, testTimeout)
			if tc.cancelAfter != 0 {
				go func(cancelFunc context.CancelFunc, after time.Duration) {
					time.Sleep(after)
					cancelFunc()
				}(cancel, tc.cancelAfter)
			}

			start := time.Now()
			collectGarbageLoop(ctx, time.NewTicker(loopInterval))
			end := time.Now()

			cancel()

			elapsed := end.Sub(start)
			if elapsed >= testTimeout {
				t.Fatalf("expected immediate return, took %v", elapsed)
			}
		})
	}
}
