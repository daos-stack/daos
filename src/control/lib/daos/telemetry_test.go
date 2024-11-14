//
// (C) Copyright 2021-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package daos

import (
	"encoding/json"
	"math"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common/test"
)

func TestDaos_Metric_JSON(t *testing.T) {
	testLabelMap := map[string]string{
		"label1": "val1",
		"label2": "val2",
	}

	for name, tc := range map[string]struct {
		metric Metric
	}{
		"nil": {},
		"simple": {
			metric: newSimpleMetric(testLabelMap, 123),
		},
		"summary": {
			metric: &SummaryMetric{
				Labels:      testLabelMap,
				SampleSum:   5678.9,
				SampleCount: 42,
				Quantiles: QuantileMap{
					0.25: 50,
					0.5:  42,
				},
			},
		},
		"histogram": {
			metric: &HistogramMetric{
				Labels:      testLabelMap,
				SampleSum:   9876,
				SampleCount: 120,
				Buckets: []*MetricBucket{
					{
						CumulativeCount: 55,
						UpperBound:      500,
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			marshaled, err := json.Marshal(tc.metric)
			if err != nil {
				t.Fatalf("expected to marshal, got %q", err)
			}

			var unmarshaled Metric
			switch tc.metric.(type) {
			case *SimpleMetric:
				unmarshaled = new(SimpleMetric)
			case *SummaryMetric:
				unmarshaled = new(SummaryMetric)
			case *HistogramMetric:
				unmarshaled = new(HistogramMetric)
			default:
				unmarshaled = new(SimpleMetric)
			}

			err = json.Unmarshal(marshaled, unmarshaled)
			if err != nil {
				t.Fatalf("expected to unmarshal, got %q", err)
			}

			expResult := tc.metric
			if tc.metric == nil {
				expResult = &SimpleMetric{}
			}

			if diff := cmp.Diff(expResult, unmarshaled); diff != "" {
				t.Fatalf("unmarshaled different from original (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestDaos_metricTypeFromString(t *testing.T) {
	for name, tc := range map[string]struct {
		input   string
		expType MetricType
	}{
		"empty": {
			expType: MetricTypeUnknown,
		},
		"counter": {
			input:   "counter",
			expType: MetricTypeCounter,
		},
		"gauge": {
			input:   "gauge",
			expType: MetricTypeGauge,
		},
		"summary": {
			input:   "summary",
			expType: MetricTypeSummary,
		},
		"histogram": {
			input:   "histogram",
			expType: MetricTypeHistogram,
		},
		"generic": {
			input:   "generic",
			expType: MetricTypeGeneric,
		},
		"invalid": {
			input:   "some garbage text",
			expType: MetricTypeUnknown,
		},
		"weird capitalization": {
			input:   "CoUnTeR",
			expType: MetricTypeCounter,
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotType := metricTypeFromString(tc.input)

			test.AssertEqual(t, tc.expType, gotType, "")
		})
	}
}

func TestDaos_MetricSet_JSON(t *testing.T) {
	for name, tc := range map[string]struct {
		set *MetricSet
	}{
		"nil": {},
		"generic type": {
			set: &MetricSet{
				Name:        "timespan",
				Description: "It's been a while",
				Type:        MetricTypeGeneric,
				Metrics: []Metric{
					newSimpleMetric(map[string]string{
						"units": "nanoseconds",
					}, float64(time.Second)),
				},
			},
		},
		"counter type": {
			set: &MetricSet{
				Name:        "one_ring",
				Description: "Precious...",
				Type:        MetricTypeCounter,
				Metrics: []Metric{
					newSimpleMetric(map[string]string{
						"owner": "frodo",
					}, 1),
				},
			},
		},
		"gauge type": {
			set: &MetricSet{
				Name:        "funny_hats",
				Description: "Hilarious headgear in inventory",
				Type:        MetricTypeGauge,
				Metrics: []Metric{
					newSimpleMetric(map[string]string{
						"type": "tophat",
					}, 1),
					newSimpleMetric(map[string]string{
						"type": "cowboy",
					}, 6),
					newSimpleMetric(map[string]string{
						"type": "jester",
					}, 0),
				},
			},
		},
		"summary type": {
			set: &MetricSet{
				Name:        "alpha",
				Description: "The first letter! Everybody's favorite!",
				Type:        MetricTypeSummary,
				Metrics: []Metric{
					&SummaryMetric{
						Labels:      map[string]string{"beta": "b"},
						SampleCount: 3,
						SampleSum:   42,
						Quantiles:   map[float64]float64{0.5: 2.2},
					},
				},
			},
		},
		"histogram type": {
			set: &MetricSet{
				Name:        "my_histogram",
				Description: "This is a histogram",
				Type:        MetricTypeHistogram,
				Metrics: []Metric{
					&HistogramMetric{
						Labels:      map[string]string{"owner": "me"},
						SampleCount: 1024,
						SampleSum:   12344,
						Buckets: []*MetricBucket{
							{
								CumulativeCount: 789,
								UpperBound:      500,
							},
							{
								CumulativeCount: 456,
								UpperBound:      1000,
							},
						},
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			marshaled, err := json.Marshal(tc.set)
			if err != nil {
				t.Fatalf("expected to marshal, got %q", err)
			}

			unmarshaled := new(MetricSet)
			err = json.Unmarshal(marshaled, unmarshaled)
			if err != nil {
				t.Fatalf("expected to unmarshal, got %q", err)
			}

			expResult := tc.set
			if tc.set == nil {
				expResult = &MetricSet{}
			}

			if diff := cmp.Diff(expResult, unmarshaled); diff != "" {
				t.Fatalf("unmarshaled different from original (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestDaos_MetricBucket_JSON(t *testing.T) {
	for name, tc := range map[string]struct {
		bucket          *MetricBucket
		expUpperBound   float64
		expMarshalErr   error
		expUnmarshalErr error
	}{
		"+Inf": {
			bucket: &MetricBucket{
				UpperBound: math.Inf(1),
			},
			expUpperBound: math.Inf(1),
		},
		"-Inf": {
			bucket: &MetricBucket{
				UpperBound: math.Inf(-1),
			},
			expUpperBound: math.Inf(-1),
		},
		"NaN": {
			bucket: &MetricBucket{
				UpperBound: math.NaN(),
			},
			expUpperBound: math.NaN(),
		},
		"42.42": {
			bucket: &MetricBucket{
				UpperBound: 42.42,
			},
			expUpperBound: 42.42,
		},
		"0.000": {
			bucket: &MetricBucket{
				UpperBound: 0.000,
			},
			expUpperBound: 0.000,
		},
	} {
		t.Run(name, func(t *testing.T) {
			data, gotErr := json.Marshal(tc.bucket)
			test.CmpErr(t, tc.expMarshalErr, gotErr)
			if tc.expMarshalErr != nil {
				return
			}

			var gotBucket MetricBucket
			gotErr = json.Unmarshal(data, &gotBucket)
			test.CmpErr(t, tc.expUnmarshalErr, gotErr)
			if tc.expUnmarshalErr != nil {
				return
			}

			if math.IsNaN(tc.expUpperBound) {
				if !math.IsNaN(gotBucket.UpperBound) {
					t.Fatalf("UpperBound NaN value did not survive Marshal/Unmarshal (got %f)", gotBucket.UpperBound)
				}
			} else {
				if diff := cmp.Diff(tc.expUpperBound, gotBucket.UpperBound); diff != "" {
					t.Fatalf("Bucket UpperBound value did not survive Marshal/Unmarshal (-want, +got): %s", diff)
				}
			}
		})
	}
}
