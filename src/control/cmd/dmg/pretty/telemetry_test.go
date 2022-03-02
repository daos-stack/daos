//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pretty

import (
	"errors"
	"strings"
	"testing"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/control"
)

func TestPretty_PrintMetricsListResp(t *testing.T) {
	for name, tc := range map[string]struct {
		resp      *control.MetricsListResp
		writeErr  error
		expOutput string
		expErr    error
	}{
		"nil resp": {
			expErr: errors.New("nil response"),
		},
		"nil list": {
			resp: &control.MetricsListResp{},
		},
		"empty list": {
			resp: &control.MetricsListResp{
				AvailableMetricSets: []*control.MetricSet{},
			},
		},
		"one item": {
			resp: &control.MetricsListResp{
				AvailableMetricSets: []*control.MetricSet{
					{
						Name:        "test_metric_1",
						Description: "Test Metric",
						Type:        control.MetricTypeGeneric,
					},
				},
			},
			expOutput: `
Metric Set    Type    Description 
----------    ----    ----------- 
test_metric_1 Generic Test Metric 
`,
		},
		"multi item": {
			resp: &control.MetricsListResp{
				AvailableMetricSets: []*control.MetricSet{
					{
						Name:        "test_metric_1",
						Description: "Test metric",
						Type:        control.MetricTypeGauge,
					},
					{
						Name:        "test_metric_2",
						Description: "Another test metric",
						Type:        control.MetricTypeSummary,
					},
					{
						Name:        "funny_hats",
						Description: "Hilarious headwear",
						Type:        control.MetricTypeCounter,
					},
				},
			},
			expOutput: `
Metric Set    Type    Description         
----------    ----    -----------         
test_metric_1 Gauge   Test metric         
test_metric_2 Summary Another test metric 
funny_hats    Counter Hilarious headwear  
`,
		},
		"write failure": {
			resp: &control.MetricsListResp{
				AvailableMetricSets: []*control.MetricSet{
					{
						Name:        "test_metric_1",
						Description: "Test Metric",
					},
				},
			},
			writeErr: errors.New("mock write"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			writer := &test.MockWriter{
				WriteErr: tc.writeErr,
			}

			err := PrintMetricsListResp(writer, tc.resp)

			test.CmpErr(t, tc.expErr, err)
			test.AssertEqual(t, strings.TrimLeft(tc.expOutput, "\n"), writer.GetWritten(), "")
		})
	}
}

func TestPretty_PrintMetricsQueryResp(t *testing.T) {
	for name, tc := range map[string]struct {
		resp      *control.MetricsQueryResp
		writeErr  error
		expOutput string
		expErr    error
	}{
		"nil resp": {
			expErr: errors.New("nil response"),
		},
		"nil list": {
			resp: &control.MetricsQueryResp{},
		},
		"empty list": {
			resp: &control.MetricsQueryResp{
				MetricSets: []*control.MetricSet{},
			},
		},
		"set without values": {
			resp: &control.MetricsQueryResp{
				MetricSets: []*control.MetricSet{
					{
						Name:        "test_metric_1",
						Description: "Test Metric",
					},
				},
			},
			expOutput: `
- Metric Set: test_metric_1 (Type: Unknown)
  Test Metric
    No metrics found

`,
		},
		"untyped": {
			resp: &control.MetricsQueryResp{
				MetricSets: []*control.MetricSet{
					{
						Name:        "my_metric",
						Description: "A test metric",
						Type:        control.MetricTypeGeneric,
						Metrics: []control.Metric{
							&control.SimpleMetric{
								Labels: map[string]string{
									"foo": "bar",
								},
								Value: 2.25,
							},
							&control.SimpleMetric{
								Labels: map[string]string{
									"ring":   "one",
									"bearer": "frodo",
								},
								Value: 5,
							},
							&control.SimpleMetric{
								Labels: map[string]string{},
								Value:  125,
							},
						},
					},
				},
			},
			expOutput: `
- Metric Set: my_metric (Type: Generic)
  A test metric
    Metric  Labels                   Value 
    ------  ------                   ----- 
    Generic (foo=bar)                2.25  
    Generic (bearer=frodo, ring=one) 5     
    Generic N/A                      125   

`,
		},
		"counter type": {
			resp: &control.MetricsQueryResp{
				MetricSets: []*control.MetricSet{
					{
						Name:        "my_counter",
						Description: "A test metric",
						Type:        control.MetricTypeCounter,
						Metrics: []control.Metric{
							&control.SimpleMetric{
								Labels: map[string]string{
									"foo": "bar",
								},
								Value: 2.25,
							},
							&control.SimpleMetric{
								Labels: map[string]string{
									"ring":   "one",
									"bearer": "frodo",
								},
								Value: 5,
							},
							&control.SimpleMetric{
								Labels: map[string]string{},
								Value:  125,
							},
						},
					},
				},
			},
			expOutput: `
- Metric Set: my_counter (Type: Counter)
  A test metric
    Metric  Labels                   Value 
    ------  ------                   ----- 
    Counter (foo=bar)                2.25  
    Counter (bearer=frodo, ring=one) 5     
    Counter N/A                      125   

`,
		},
		"gauge type": {
			resp: &control.MetricsQueryResp{
				MetricSets: []*control.MetricSet{
					{
						Name:        "my_gauge",
						Description: "A test metric",
						Type:        control.MetricTypeGauge,
						Metrics: []control.Metric{
							&control.SimpleMetric{
								Labels: map[string]string{
									"foo": "bar",
								},
								Value: 2.25,
							},
							&control.SimpleMetric{
								Labels: map[string]string{
									"ring":   "one",
									"bearer": "frodo",
								},
								Value: 5,
							},
							&control.SimpleMetric{
								Labels: map[string]string{},
								Value:  125,
							},
						},
					},
				},
			},
			expOutput: `
- Metric Set: my_gauge (Type: Gauge)
  A test metric
    Metric Labels                   Value 
    ------ ------                   ----- 
    Gauge  (foo=bar)                2.25  
    Gauge  (bearer=frodo, ring=one) 5     
    Gauge  N/A                      125   

`,
		},
		"summary type": {
			resp: &control.MetricsQueryResp{
				MetricSets: []*control.MetricSet{
					{
						Name:        "my_summary",
						Description: "A test metric",
						Type:        control.MetricTypeSummary,
						Metrics: []control.Metric{
							&control.SummaryMetric{
								Labels: control.LabelMap{
									"foo": "bar",
								},
								SampleCount: 55,
								SampleSum:   6094.27,
								Quantiles: map[float64]float64{
									0.25: 2034,
									0.5:  33.333,
								},
							},
							&control.SummaryMetric{
								Labels:      control.LabelMap{},
								SampleCount: 102,
								SampleSum:   19.84,
								Quantiles: map[float64]float64{
									0.25: 4,
								},
							},
						},
					},
				},
			},
			expOutput: `
- Metric Set: my_summary (Type: Summary)
  A test metric
    Metric         Labels    Value   
    ------         ------    -----   
    Sample Count   (foo=bar) 55      
    Sample Sum     (foo=bar) 6094.27 
    Quantile(0.25) (foo=bar) 2034    
    Quantile(0.5)  (foo=bar) 33.333  
    Sample Count   N/A       102     
    Sample Sum     N/A       19.84   
    Quantile(0.25) N/A       4       

`,
		},
		"histogram type": {
			resp: &control.MetricsQueryResp{
				MetricSets: []*control.MetricSet{
					{
						Name:        "my_histogram",
						Description: "A test metric",
						Type:        control.MetricTypeHistogram,
						Metrics: []control.Metric{
							&control.HistogramMetric{
								Labels: control.LabelMap{
									"foo": "bar",
								},
								SampleCount: 55,
								SampleSum:   6094.27,
								Buckets: []*control.MetricBucket{
									{
										UpperBound:      500,
										CumulativeCount: 2,
									},
									{
										UpperBound:      100,
										CumulativeCount: 1,
									},
								},
							},
							&control.HistogramMetric{
								Labels:      control.LabelMap{},
								SampleCount: 22,
								SampleSum:   102,
							},
						},
					},
				},
			},
			expOutput: `
- Metric Set: my_histogram (Type: Histogram)
  A test metric
    Metric                     Labels    Value   
    ------                     ------    -----   
    Sample Count               (foo=bar) 55      
    Sample Sum                 (foo=bar) 6094.27 
    Bucket(0) Upper Bound      (foo=bar) 500     
    Bucket(0) Cumulative Count (foo=bar) 2       
    Bucket(1) Upper Bound      (foo=bar) 100     
    Bucket(1) Cumulative Count (foo=bar) 1       
    Sample Count               N/A       22      
    Sample Sum                 N/A       102     

`,
		},
		"multiple sets": {
			resp: &control.MetricsQueryResp{
				MetricSets: []*control.MetricSet{
					{
						Name:        "my_counter",
						Description: "A test metric",
						Type:        control.MetricTypeCounter,
						Metrics: []control.Metric{
							&control.SimpleMetric{
								Labels: map[string]string{
									"foo": "bar",
								},
								Value: 2.25,
							},
							&control.SimpleMetric{
								Labels: map[string]string{
									"ring":   "one",
									"bearer": "frodo",
								},
								Value: 5,
							},
							&control.SimpleMetric{
								Labels: map[string]string{},
								Value:  125,
							},
						},
					},
					{
						Name:        "my_summary",
						Description: "Another test metric",
						Type:        control.MetricTypeSummary,
						Metrics: []control.Metric{
							&control.SummaryMetric{
								SampleCount: 55,
								SampleSum:   6094.27,
								Quantiles: map[float64]float64{
									0.25: 2034,
									0.5:  33.333,
								},
							},
						},
					},
				},
			},
			expOutput: `
- Metric Set: my_counter (Type: Counter)
  A test metric
    Metric  Labels                   Value 
    ------  ------                   ----- 
    Counter (foo=bar)                2.25  
    Counter (bearer=frodo, ring=one) 5     
    Counter N/A                      125   

- Metric Set: my_summary (Type: Summary)
  Another test metric
    Metric         Labels Value   
    ------         ------ -----   
    Sample Count   N/A    55      
    Sample Sum     N/A    6094.27 
    Quantile(0.25) N/A    2034    
    Quantile(0.5)  N/A    33.333  

`,
		},
		"write failure": {
			resp: &control.MetricsQueryResp{
				MetricSets: []*control.MetricSet{
					{
						Name:        "test_metric_1",
						Description: "Test Metric",
					},
				},
			},
			writeErr: errors.New("mock write"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			writer := &test.MockWriter{
				WriteErr: tc.writeErr,
			}

			err := PrintMetricsQueryResp(writer, tc.resp)

			test.CmpErr(t, tc.expErr, err)
			test.AssertEqual(t, strings.TrimLeft(tc.expOutput, "\n"), writer.GetWritten(), "")
		})
	}
}
