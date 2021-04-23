//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package telemetry

import (
	"testing"

	"github.com/daos-stack/daos/src/control/common"
)

func TestTelemetry_Basics(t *testing.T) {
	ctx, testMetrics := setupTestMetrics(t)
	defer cleanupTestMetrics(ctx, t)

	var m Metric
	var err error

	runTests := func(t *testing.T, tm *testMetric, m Metric) {
		common.AssertEqual(t, tm.name, m.Name(), "Name() failed")
		common.AssertEqual(t, tm.desc, m.Desc(), "Desc() failed")
		common.AssertEqual(t, tm.units, m.Units(), "Units() failed")
		common.AssertEqual(t, tm.cur, m.FloatValue(), "FloatValue() failed")
		common.AssertEqual(t, tm.str, m.String(), "String() failed")

		if sm, ok := m.(StatsMetric); ok {
			common.AssertEqual(t, tm.min, sm.FloatMin(), "FloatMin() failed")
			common.AssertEqual(t, tm.max, sm.FloatMax(), "FloatMax() failed")
			common.AssertEqual(t, tm.sum, sm.FloatSum(), "FloatSum() failed")
			common.AssertEqual(t, tm.mean, sm.Mean(), "Mean() failed")
			common.AssertEqual(t, tm.stddev, sm.StdDev(), "StdDev() failed")
		}
	}

	for mt, tm := range testMetrics {
		switch mt {
		case MetricTypeGauge:
			m, err = GetGauge(ctx, tm.name)
			if err != nil {
				t.Fatal(err)
			}

			runTests(t, tm, m)
		case MetricTypeCounter:
			m, err = GetCounter(ctx, tm.name)
			if err != nil {
				t.Fatal(err)
			}

			runTests(t, tm, m)
		default:
			t.Fatalf("%d not tested", mt)
		}
	}
}
