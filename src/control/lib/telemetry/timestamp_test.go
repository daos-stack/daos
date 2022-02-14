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

	"github.com/daos-stack/daos/src/control/common"
	"github.com/pkg/errors"
)

func TestTelemetry_GetTimestamp(t *testing.T) {
	testCtx, testMetrics := setupTestMetrics(t)
	defer cleanupTestMetrics(testCtx, t)

	realTime, ok := testMetrics[MetricTypeTimestamp]
	if !ok {
		t.Fatal("real timestamp not in metrics set")
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
			expErr:     errors.New("not a timestamp"),
		},
		"success": {
			ctx:        testCtx,
			metricName: timeName,
			expResult:  realTime,
		},
	} {
		t.Run(name, func(t *testing.T) {
			result, err := GetTimestamp(tc.ctx, tc.metricName)

			common.CmpErr(t, tc.expErr, err)

			if tc.expResult != nil {
				if result == nil {
					t.Fatalf("expected non-nil result matching %+v", tc.expResult)
				}

				testMetricBasics(t, tc.expResult, result)
				common.AssertEqual(t, result.Type(), MetricTypeTimestamp, "bad type")

				// guarantee it's in a reasonable range
				val := result.Value()
				createTime := time.Unix(int64(tc.expResult.Cur), 0)
				common.AssertTrue(t, val.Equal(createTime) || val.After(createTime), fmt.Sprintf("value %v too early", val))
				common.AssertTrue(t, val.Equal(time.Now()) || val.Before(time.Now()), fmt.Sprintf("value %v too late", val))

				common.AssertEqual(t, float64(val.Unix()), result.FloatValue(), "expected float value and time value equal")
			} else {
				if result != nil {
					t.Fatalf("expected nil result, got %+v", result)
				}
			}
		})
	}
}
