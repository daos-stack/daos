//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build linux && amd64
// +build linux,amd64

//

package telemetry

import (
	"context"
	"testing"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
)

func TestTelemetry_GetCounter(t *testing.T) {
	testCtx, testMetrics := setupTestMetrics(t)
	defer cleanupTestMetrics(testCtx, t)

	realCounter, ok := testMetrics[MetricTypeCounter]
	if !ok {
		t.Fatal("real counter not in metrics set")
	}
	counterName := realCounter.FullPath()

	for name, tc := range map[string]struct {
		ctx        context.Context
		metricName string
		expResult  *TestMetric
		expErr     error
	}{
		"non-handle ctx": {
			ctx:        context.TODO(),
			metricName: counterName,
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
			expErr:     errors.New("not a counter"),
		},
		"success": {
			ctx:        testCtx,
			metricName: counterName,
			expResult:  realCounter,
		},
	} {
		t.Run(name, func(t *testing.T) {
			result, err := GetCounter(tc.ctx, tc.metricName)

			common.CmpErr(t, tc.expErr, err)

			if tc.expResult != nil {
				if result == nil {
					t.Fatalf("expected non-nil result matching %+v", tc.expResult)
				}

				testMetricBasics(t, tc.expResult, result)
				common.AssertEqual(t, result.Type(), MetricTypeCounter, "bad type")
				common.AssertEqual(t, result.Value(), uint64(tc.expResult.Cur), "bad value")
				common.AssertEqual(t, result.FloatValue(), tc.expResult.Cur, "bad float value")
			} else {
				if result != nil {
					t.Fatalf("expected nil result, got %+v", result)
				}
			}
		})
	}
}
