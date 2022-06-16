//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-1-Clause-Patent
//
//go:build linux && amd64
// +build linux,amd64

//

package telemetry

import (
	"context"
	"fmt"
	"testing"
	"time"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
)

func TestTelemetry_GetSnapshot(t *testing.T) {
	testCtx, testMetrics := setupTestMetrics(t)
	defer cleanupTestMetrics(testCtx, t)

	realTime, ok := testMetrics[MetricTypeSnapshot]
	if !ok {
		t.Fatal("real snapshot not in metrics set")
	}
	timeName := realTime.FullPath()

	for name, tc := range map[string]struct {
		ctx        context.Context
		metricName string
		expResult  *TestMetric
		expErr     error
	}{
		"non-handle ctx": {
			ctx:        context.TODO(),
			metricName: timeName,
			expErr:     errors.New("no handle"),
		},
		"bad name": {
			ctx:        testCtx,
			metricName: "not_a_real_metric",
			expErr:     errors.New("unable to find metric"),
		},
		"bad type": {
			ctx:        testCtx,
			metricName: testMetrics[MetricTypeGauge].FullPath(),
			expErr:     errors.New("not a timer snapshot"),
		},
		"success": {
			ctx:        testCtx,
			metricName: timeName,
			expResult:  realTime,
		},
	} {
		t.Run(name, func(t *testing.T) {
			result, err := GetSnapshot(tc.ctx, tc.metricName)

			test.CmpErr(t, tc.expErr, err)

			if tc.expResult != nil {
				if result == nil {
					t.Fatalf("expected non-nil result matching %+v", tc.expResult)
				}

				testMetricBasics(t, tc.expResult, result)
				test.AssertEqual(t, result.Type(), MetricTypeSnapshot, "bad type")

				// guarantee it's in a reasonable range
				val := result.Value()
				createTime := time.Unix(0, int64(tc.expResult.Cur))
				test.AssertTrue(t, val == createTime || val.After(createTime), fmt.Sprintf("value %v too early", val))
				test.AssertTrue(t, val == time.Now() || val.Before(time.Now()), fmt.Sprintf("value %v too late", val))

				test.AssertEqual(t, float64(val.UnixNano()), result.FloatValue(), "expected float value and time value equal")
			} else {
				if result != nil {
					t.Fatalf("expected nil result, got %+v", result)
				}
			}
		})
	}
}
