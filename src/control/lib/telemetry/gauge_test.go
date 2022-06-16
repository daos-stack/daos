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

	"github.com/daos-stack/daos/src/control/common/test"
)

func TestTelemetry_GetGauge(t *testing.T) {
	testCtx, testMetrics := setupTestMetrics(t)
	defer cleanupTestMetrics(testCtx, t)

	realGauge, ok := testMetrics[MetricTypeGauge]
	if !ok {
		t.Fatal("real gauge not in metrics set")
	}
	gaugeName := realGauge.FullPath()

	for name, tc := range map[string]struct {
		ctx        context.Context
		metricName string
		expResult  *TestMetric
		expErr     error
	}{
		"non-handle ctx": {
			ctx:        context.TODO(),
			metricName: gaugeName,
			expErr:     errors.New("no handle"),
		},
		"bad name": {
			ctx:        testCtx,
			metricName: "not_a_real_metric",
			expErr:     errors.New("unable to find metric"),
		},
		"bad type": {
			ctx:        testCtx,
			metricName: testMetrics[MetricTypeCounter].FullPath(),
			expErr:     errors.New("not a gauge"),
		},
		"success": {
			ctx:        testCtx,
			metricName: gaugeName,
			expResult:  realGauge,
		},
	} {
		t.Run(name, func(t *testing.T) {
			result, err := GetGauge(tc.ctx, tc.metricName)

			test.CmpErr(t, tc.expErr, err)

			if tc.expResult != nil {
				if result == nil {
					t.Fatalf("expected non-nil result matching %+v", tc.expResult)
				}

				testMetricBasics(t, tc.expResult, result)
				test.AssertEqual(t, result.Type(), MetricTypeGauge, "bad type")
				test.AssertEqual(t, result.Value(), uint64(tc.expResult.Cur), "bad value")
				test.AssertEqual(t, result.FloatValue(), tc.expResult.Cur, "bad float value")
			} else if result != nil {
				t.Fatalf("expected nil result, got %+v", result)
			}
		})
	}
}

func TestTelemetry_GetStatsGauge(t *testing.T) {
	testCtx, testMetrics := setupTestMetrics(t)
	defer cleanupTestMetrics(testCtx, t)

	realGauge, ok := testMetrics[MetricTypeStatsGauge]
	if !ok {
		t.Fatal("real gauge not in metrics set")
	}
	gaugeName := realGauge.FullPath()

	for name, tc := range map[string]struct {
		ctx        context.Context
		metricName string
		expResult  *TestMetric
		expErr     error
	}{
		"non-handle ctx": {
			ctx:        context.TODO(),
			metricName: gaugeName,
			expErr:     errors.New("no handle"),
		},
		"bad name": {
			ctx:        testCtx,
			metricName: "not_a_real_metric",
			expErr:     errors.New("unable to find metric"),
		},
		"bad type": {
			ctx:        testCtx,
			metricName: testMetrics[MetricTypeCounter].FullPath(),
			expErr:     errors.New("not a gauge"),
		},
		"success": {
			ctx:        testCtx,
			metricName: gaugeName,
			expResult:  realGauge,
		},
	} {
		t.Run(name, func(t *testing.T) {
			result, err := GetStatsGauge(tc.ctx, tc.metricName)

			test.CmpErr(t, tc.expErr, err)

			if tc.expResult != nil {
				if result == nil {
					t.Fatalf("expected non-nil result matching %+v", tc.expResult)
				}

				testMetricBasics(t, tc.expResult, result)
				test.AssertEqual(t, result.Type(), MetricTypeStatsGauge, "bad type")
				test.AssertEqual(t, result.Value(), uint64(tc.expResult.Cur), "bad value")
				test.AssertEqual(t, result.FloatValue(), tc.expResult.Cur, "bad float value")

				test.AssertEqual(t, tc.expResult.min, result.FloatMin(), "FloatMin() failed")
				test.AssertEqual(t, tc.expResult.max, result.FloatMax(), "FloatMax() failed")
				test.AssertEqual(t, tc.expResult.sum, result.FloatSum(), "FloatSum() failed")
				test.AssertEqual(t, tc.expResult.mean, result.Mean(), "Mean() failed")
				test.AssertEqual(t, tc.expResult.stddev, result.StdDev(), "StdDev() failed")
				test.AssertEqual(t, uint64(3), result.SampleSize(), "SampleSize() failed")
			} else if result != nil {
				t.Fatalf("expected nil result, got %+v", result)
			}
		})
	}
}
