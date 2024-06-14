//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package promexp

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/telemetry"
)

func TestPromExp_sanitizeMetricName(t *testing.T) {
	for input, tc := range map[string]struct {
		expOutput string
	}{
		"": {
			expOutput: "",
		},
		"azAZ09": {
			expOutput: "azAZ09",
		},
		"/a-z A-Z 0-9/": {
			expOutput: "a_z_A_Z_0_9_",
		},
	} {
		t.Run(input, func(t *testing.T) {
			got := sanitizeMetricName(input)
			if got != tc.expOutput {
				t.Errorf("sanitizeMetricName(%q) = %q, want %q", input, got, tc.expOutput)
			}
		})
	}
}

func TestPromExp_getMetricStats(t *testing.T) {
	segID := telemetry.NextTestID(telemetry.PromexpIDBase)
	telemetry.InitTestMetricsProducer(t, segID, 4096)
	defer telemetry.CleanupTestMetricsProducer(t)
	testValues := []uint64{1, 2, 3, 4, 5}

	ctx, err := telemetry.Init(test.Context(t), uint32(segID))
	if err != nil {
		t.Fatalf("Init: %v", err)
	}

	for name, tc := range map[string]struct {
		baseName string
		metric   *telemetry.TestMetric
		expStats []*metricStat
	}{
		"non-stats gauge": {
			baseName: "gauge",
			metric: &telemetry.TestMetric{
				Name: "gauge",
				Type: telemetry.MetricTypeGauge,
				Cur:  1.0,
			},
			expStats: []*metricStat{},
		},
		"stats gauge": {
			baseName: "stats_gauge",
			metric: &telemetry.TestMetric{
				Name:   "stats_gauge",
				Type:   telemetry.MetricTypeStatsGauge,
				Values: testValues,
			},
			expStats: []*metricStat{
				{
					name:  "stats_gauge_min",
					desc:  " (min value)",
					value: 1.0,
				},
				{
					name:  "stats_gauge_max",
					desc:  " (max value)",
					value: 5.0,
				},
				{
					name:  "stats_gauge_mean",
					desc:  " (mean)",
					value: 3.0,
				},
				{
					name:  "stats_gauge_sum",
					desc:  " (sum)",
					value: 15.0,
				},
				{
					name:      "stats_gauge_samples",
					desc:      " (samples)",
					value:     5,
					isCounter: true,
				},
				{
					name:  "stats_gauge_stddev",
					desc:  " (std dev)",
					value: 1.58113883,
				},
				{
					name:  "stats_gauge_sumsquares",
					desc:  " (sum of squares)",
					value: 55,
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			telemetry.AddTestMetric(t, tc.metric)

			m, err := tc.metric.GetMetric(ctx)
			if err != nil {
				t.Fatalf("GetMetric: %v", err)
			}

			got := getMetricStats(tc.baseName, m)
			cmpOpts := cmp.Options{
				cmp.AllowUnexported(metricStat{}),
				cmpopts.EquateApprox(0.000000001, 0.0),
				cmpopts.SortSlices(func(a, b *metricStat) bool {
					return a.name < b.name
				}),
			}
			if diff := cmp.Diff(got, tc.expStats, cmpOpts...); diff != "" {
				t.Fatalf("(-want, +got)\n%s", diff)
			}
		})
	}
}
