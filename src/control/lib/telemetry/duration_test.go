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

	"github.com/daos-stack/daos/src/control/common"
)

func TestTelemetry_GetDuration(t *testing.T) {
	testCtx, testMetrics := setupTestMetrics(t)
	defer cleanupTestMetrics(testCtx, t)

	realDur, ok := testMetrics[MetricTypeDuration]
	if !ok {
		t.Fatal("real duration not in metrics set")
	}
	durName := realDur.FullPath()

	for name, tc := range map[string]struct {
		ctx        context.Context
		metricName string
		expResult  *TestMetric
		expErr     error
	}{
		"non-handle ctx": {
			ctx:        context.TODO(),
			metricName: durName,
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
			expErr:     errors.New("not a duration"),
		},
		"success": {
			ctx:        testCtx,
			metricName: durName,
			expResult:  realDur,
		},
	} {
		t.Run(name, func(t *testing.T) {
			result, err := GetDuration(tc.ctx, tc.metricName)

			common.CmpErr(t, tc.expErr, err)

			if tc.expResult != nil {
				if result == nil {
					t.Fatalf("expected non-nil result matching %+v", tc.expResult)
				}

				testMetricBasics(t, tc.expResult, result)
				common.AssertEqual(t, result.Type(), MetricTypeDuration, "bad type")

				common.AssertTrue(t, result.Value() > time.Duration(tc.expResult.Cur), "value smaller than actual")
				common.AssertTrue(t, result.FloatValue() > tc.expResult.Cur, "float value smaller than actual")

				diff := result.Value() - time.Duration(tc.expResult.Cur)
				maxDiff := time.Second // very generous, in the case of slow running systems
				common.AssertTrue(t, diff < maxDiff,
					fmt.Sprintf("difference %d too big (expected less than %d)", diff, maxDiff))

				// Just make sure the stats were set to something
				common.AssertTrue(t, result.FloatMin() > 0, "FloatMin() failed")
				common.AssertTrue(t, result.FloatMax() > 0, "FloatMax() failed")
				common.AssertTrue(t, result.FloatSum() > 0, "FloatSum() failed")
				common.AssertTrue(t, result.Mean() > 0, "Mean() failed")

				common.AssertEqual(t, 0.0, result.StdDev(), "StdDev() failed")
				common.AssertEqual(t, uint64(1), result.SampleSize(), "SampleSize() failed")
			} else {
				if result != nil {
					t.Fatalf("expected nil result, got %+v", result)
				}
			}
		})
	}
}
