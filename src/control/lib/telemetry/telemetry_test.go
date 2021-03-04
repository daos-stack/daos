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
	defer cleanupTestMetrics(t)

	var m Metric
	var err error

	runTests := func(t *testing.T, mv *testMetric, m Metric) {
		common.AssertEqual(t, mv.c.name, m.Name(), "Name() failed")
		common.AssertEqual(t, mv.c.short, m.ShortDesc(), "ShortDesc() failed")
		common.AssertEqual(t, mv.c.long, m.LongDesc(), "LongDesc() failed")
		common.AssertEqual(t, mv.c.cur, m.FloatValue(), "FloatValue() failed")
		common.AssertEqual(t, mv.c.str, m.String(), "String() failed")

		if sm, ok := m.(StatsMetric); ok {
			common.AssertEqual(t, mv.c.min, sm.FloatMin(), "FloatMin() failed")
			common.AssertEqual(t, mv.c.max, sm.FloatMax(), "FloatMax() failed")
			common.AssertEqual(t, mv.c.sum, sm.FloatSum(), "FloatSum() failed")
			common.AssertEqual(t, mv.c.mean, sm.Mean(), "Mean() failed")
			common.AssertEqual(t, mv.c.stddev, sm.StdDev(), "StdDev() failed")
		}
	}

	for mt, mv := range testMetrics {
		switch mt {
		case MetricTypeGauge:
			m, err = GetGauge(ctx, mv.c.name)
			if err != nil {
				t.Fatal(err)
			}

			runTests(t, &mv, m)
		case MetricTypeCounter:
			m, err = GetCounter(ctx, mv.c.name)
			if err != nil {
				t.Fatal(err)
			}

			runTests(t, &mv, m)
		default:
			t.Fatalf("%d not tested", mt)
		}
	}
}
