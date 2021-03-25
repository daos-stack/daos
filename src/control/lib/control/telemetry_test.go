//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"fmt"
	"math"
	"net/url"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"
	pclient "github.com/prometheus/client_model/go"
	"github.com/prometheus/common/expfmt"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/common"
)

func newTestMetricFamily(name string, help string, mType pclient.MetricType) *pclient.MetricFamily {
	fam := &pclient.MetricFamily{
		Name: new(string),
		Help: new(string),
		Type: new(pclient.MetricType),
	}
	*fam.Name = name
	*fam.Help = help
	*fam.Type = mType
	return fam
}

func addTestMetric(fam *pclient.MetricFamily) {
	metric := &pclient.Metric{}

	switch *fam.Type {
	case pclient.MetricType_GAUGE:
		metric = newTestPBGauge(0)
	case pclient.MetricType_COUNTER:
		metric = newTestPBCounter(0)
	case pclient.MetricType_SUMMARY:
		metric = newTestPBSummary(1)
	case pclient.MetricType_HISTOGRAM:
		metric = newTestPBHistogram(0)
	default:
		metric = newTestPBUntyped(0)
	}

	fam.Metric = append(fam.Metric, metric)
}

func newTestPBCounter(value float64) *pclient.Metric {
	metric := &pclient.Metric{}

	metric.Counter = new(pclient.Counter)
	metric.Counter.Value = proto.Float64(value)

	return metric
}

func newTestPBGauge(value float64) *pclient.Metric {
	metric := &pclient.Metric{}

	metric.Gauge = new(pclient.Gauge)
	metric.Gauge.Value = proto.Float64(value)

	return metric
}

func newTestPBUntyped(value float64) *pclient.Metric {
	metric := &pclient.Metric{}

	metric.Untyped = new(pclient.Untyped)
	metric.Untyped.Value = proto.Float64(value)

	return metric
}

func newTestPBSummary(numQuantiles int) *pclient.Metric {
	metric := &pclient.Metric{}

	metric.Summary = new(pclient.Summary)
	metric.Summary.SampleSum = proto.Float64(0)
	metric.Summary.SampleCount = proto.Uint64(0)
	for i := 0; i < numQuantiles; i++ {
		q := &pclient.Quantile{
			Quantile: proto.Float64(0),
			Value:    proto.Float64(0),
		}
		metric.Summary.Quantile = append(metric.Summary.Quantile, q)
	}

	return metric
}

func newTestPBHistogram(numBuckets int) *pclient.Metric {
	metric := &pclient.Metric{}

	metric.Histogram = new(pclient.Histogram)
	metric.Histogram.SampleSum = proto.Float64(0)
	metric.Histogram.SampleCount = proto.Uint64(0)
	for i := 0; i < numBuckets; i++ {
		b := &pclient.Bucket{
			CumulativeCount: proto.Uint64(0),
			UpperBound:      proto.Float64(0),
		}
		metric.Histogram.Bucket = append(metric.Histogram.Bucket, b)
	}

	return metric
}

func mockScrapeFnSuccess(t *testing.T, metricFam ...*pclient.MetricFamily) func(context.Context, *url.URL, httpGetFn) ([]byte, error) {
	t.Helper()

	return func(_ context.Context, _ *url.URL, _ httpGetFn) ([]byte, error) {
		var b strings.Builder
		for _, mf := range metricFam {
			_, err := expfmt.MetricFamilyToText(&b, mf)
			if err != nil {
				t.Fatalf("external metric parsing failed: %s", err.Error())
			}
		}
		return []byte(b.String()), nil
	}
}

func TestControl_scrapeMetrics(t *testing.T) {
	testHost := "dontcare"
	testPort := uint32(1234)

	testMetricFam := newTestMetricFamily("test_name", "This is the help text", pclient.MetricType_GAUGE)
	addTestMetric(testMetricFam)

	for name, tc := range map[string]struct {
		scrapeFn  func(context.Context, *url.URL, httpGetFn) ([]byte, error)
		expResult pbMetricMap
		expErr    error
	}{
		"check scrape params": {
			scrapeFn: func(_ context.Context, url *url.URL, getter httpGetFn) ([]byte, error) {
				common.AssertEqual(t, "http", url.Scheme, "")
				common.AssertEqual(t, fmt.Sprintf("%s:%d", testHost, testPort), url.Host, "")
				common.AssertEqual(t, "metrics", url.Path, "")

				if getter == nil {
					t.Fatal("http getter was not set")
				}
				return nil, nil
			},
			expResult: pbMetricMap{},
		},
		"HTTP scrape error": {
			scrapeFn: func(_ context.Context, _ *url.URL, _ httpGetFn) ([]byte, error) {
				return nil, errors.New("mock scrape")
			},
			expErr: errors.New("mock scrape"),
		},
		"scrape returns no content": {
			scrapeFn: func(_ context.Context, _ *url.URL, _ httpGetFn) ([]byte, error) {
				return []byte{}, nil
			},
			expResult: pbMetricMap{},
		},
		"scrape returns bad content": {
			scrapeFn: func(_ context.Context, _ *url.URL, _ httpGetFn) ([]byte, error) {
				return []byte("<h1>Hello world</h1>"), nil
			},
			expErr: errors.New("parsing error"),
		},
		"parseable content": {
			scrapeFn: mockScrapeFnSuccess(t, testMetricFam),
			expResult: pbMetricMap{
				*testMetricFam.Name: testMetricFam,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			oldScrapeFn := httpScrapeFn
			httpScrapeFn = tc.scrapeFn

			result, err := scrapeMetrics(context.TODO(), testHost, testPort)

			httpScrapeFn = oldScrapeFn
			common.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResult, result); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestControl_MetricsList(t *testing.T) {
	testMetricFam := []*pclient.MetricFamily{
		newTestMetricFamily("counter", "this is the counter help", pclient.MetricType_COUNTER),
		newTestMetricFamily("gauge", "this is the gauge help", pclient.MetricType_GAUGE),
	}

	for _, mf := range testMetricFam {
		addTestMetric(mf)
	}

	for name, tc := range map[string]struct {
		scrapeFn func(context.Context, *url.URL, httpGetFn) ([]byte, error)
		req      *MetricsListReq
		expResp  *MetricsListResp
		expErr   error
	}{
		"nil request": {
			expErr: errors.New("nil"),
		},
		"no host": {
			req:    &MetricsListReq{Port: 2525},
			expErr: errors.New("host must be specified"),
		},
		"no port": {
			req: &MetricsListReq{
				Host: "host1",
			},
			expErr: errors.New("port must be specified"),
		},
		"scrape failed": {
			req: &MetricsListReq{
				Host: "host1",
				Port: 1066,
			},
			scrapeFn: func(context.Context, *url.URL, httpGetFn) ([]byte, error) {
				return nil, errors.New("mock scrape")
			},
			expErr: errors.New("mock scrape"),
		},
		"no metrics": {
			req: &MetricsListReq{
				Host: "host1",
				Port: 8888,
			},
			scrapeFn: func(context.Context, *url.URL, httpGetFn) ([]byte, error) {
				return []byte{}, nil
			},
			expResp: &MetricsListResp{
				AvailableMetricSets: []*MetricSet{},
			},
		},
		"success": {
			req: &MetricsListReq{
				Host: "host1",
				Port: 7777,
			},
			scrapeFn: mockScrapeFnSuccess(t, testMetricFam...),
			expResp: &MetricsListResp{
				AvailableMetricSets: []*MetricSet{
					{
						Name:        "counter",
						Description: "this is the counter help",
						Type:        MetricTypeCounter,
					},
					{
						Name:        "gauge",
						Description: "this is the gauge help",
						Type:        MetricTypeGauge,
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			oldScrapeFn := httpScrapeFn
			httpScrapeFn = tc.scrapeFn
			if httpScrapeFn == nil {
				httpScrapeFn = func(context.Context, *url.URL, httpGetFn) ([]byte, error) {
					return nil, nil
				}
			}

			resp, err := MetricsList(context.TODO(), tc.req)

			httpScrapeFn = oldScrapeFn
			common.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResp, resp); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestControl_getMetricFromPrometheus(t *testing.T) {
	testLabels := LabelMap{
		"foo": "bar",
		"baz": "snafu",
	}
	testLabelPairs := make([]*pclient.LabelPair, 0)
	for key, val := range testLabels {
		testLabelPairs = append(testLabelPairs, &pclient.LabelPair{
			Name:  proto.String(key),
			Value: proto.String(val),
		})
	}

	testCounter := newTestPBCounter(6.5)
	testCounter.Label = testLabelPairs

	testGauge := newTestPBGauge(126.56)
	testGauge.Label = testLabelPairs

	testUntyped := newTestPBUntyped(42)
	testUntyped.Label = testLabelPairs

	testSummary := newTestPBSummary(4)
	*testSummary.Summary.SampleSum = 123456.5
	*testSummary.Summary.SampleCount = 508
	for i, q := range testSummary.Summary.Quantile {
		*q.Quantile = float64(i)
		*q.Value = float64(i + 1)
	}
	testSummary.Label = testLabelPairs

	testHistogram := newTestPBHistogram(3)
	*testHistogram.Histogram.SampleSum = 9876.5
	*testHistogram.Histogram.SampleCount = 128
	for i, b := range testHistogram.Histogram.Bucket {
		*b.UpperBound = 100.0 * float64(i+1)
		*b.CumulativeCount = uint64(i + 1)
	}
	testHistogram.Label = testLabelPairs

	for name, tc := range map[string]struct {
		input     *pclient.Metric
		inputType pclient.MetricType
		expResult Metric
		expErr    error
	}{
		"counter": {
			input:     testCounter,
			inputType: pclient.MetricType_COUNTER,
			expResult: newSimpleMetric(testLabels, testCounter.Counter.GetValue()),
		},
		"gauge": {
			input:     testGauge,
			inputType: pclient.MetricType_GAUGE,
			expResult: newSimpleMetric(testLabels, testGauge.Gauge.GetValue()),
		},
		"untyped": {
			input:     testUntyped,
			inputType: pclient.MetricType_UNTYPED,
			expResult: newSimpleMetric(testLabels, testUntyped.Untyped.GetValue()),
		},
		"summary": {
			input:     testSummary,
			inputType: pclient.MetricType_SUMMARY,
			expResult: &SummaryMetric{
				Labels:      testLabels,
				SampleSum:   testSummary.Summary.GetSampleSum(),
				SampleCount: testSummary.Summary.GetSampleCount(),
				Quantiles: QuantileMap{
					0: 1,
					1: 2,
					2: 3,
					3: 4,
				},
			},
		},
		"histogram": {
			input:     testHistogram,
			inputType: pclient.MetricType_HISTOGRAM,
			expResult: &HistogramMetric{
				Labels:      testLabels,
				SampleSum:   testHistogram.Histogram.GetSampleSum(),
				SampleCount: testHistogram.Histogram.GetSampleCount(),
				Buckets: []*MetricBucket{
					{
						UpperBound:      100,
						CumulativeCount: 1,
					},
					{
						UpperBound:      200,
						CumulativeCount: 2,
					},
					{
						UpperBound:      300,
						CumulativeCount: 3,
					},
				},
			},
		},
		"unknown": {
			input:     testUntyped,
			inputType: pclient.MetricType(-1),
			expErr:    errors.New("unknown metric type"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			result, err := getMetricFromPrometheus(tc.input, tc.inputType)

			common.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResult, result); diff != "" {
				t.Fatalf("unexpected result (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestControl_MetricsQuery(t *testing.T) {
	testMetricFam := []*pclient.MetricFamily{
		newTestMetricFamily("my_counter", "this is the counter help", pclient.MetricType_COUNTER),
		newTestMetricFamily("my_gauge", "this is the gauge help", pclient.MetricType_GAUGE),
		newTestMetricFamily("my_summary", "this is the summary help", pclient.MetricType_SUMMARY),
		newTestMetricFamily("my_histogram", "this is the histogram help", pclient.MetricType_HISTOGRAM),
		newTestMetricFamily("my_generic", "this is the generic help", pclient.MetricType_UNTYPED),
	}

	for _, mf := range testMetricFam {
		addTestMetric(mf)
	}

	for name, tc := range map[string]struct {
		scrapeFn func(context.Context, *url.URL, httpGetFn) ([]byte, error)
		req      *MetricsQueryReq
		expResp  *MetricsQueryResp
		expErr   error
	}{
		"nil request": {
			expErr: errors.New("nil"),
		},
		"no host": {
			req:    &MetricsQueryReq{Port: 2525},
			expErr: errors.New("host must be specified"),
		},
		"no port": {
			req: &MetricsQueryReq{
				Host: "host1",
			},
			expErr: errors.New("port must be specified"),
		},
		"scrape failed": {
			req: &MetricsQueryReq{
				Host: "host1",
				Port: 1066,
			},
			scrapeFn: func(context.Context, *url.URL, httpGetFn) ([]byte, error) {
				return nil, errors.New("mock scrape")
			},
			expErr: errors.New("mock scrape"),
		},
		"no metrics": {
			req: &MetricsQueryReq{
				Host: "host1",
				Port: 8888,
			},
			scrapeFn: func(context.Context, *url.URL, httpGetFn) ([]byte, error) {
				return []byte{}, nil
			},
			expResp: &MetricsQueryResp{
				MetricSets: []*MetricSet{},
			},
		},
		"all metrics": {
			req: &MetricsQueryReq{
				Host: "host1",
				Port: 7777,
			},
			scrapeFn: mockScrapeFnSuccess(t, testMetricFam...),
			expResp: &MetricsQueryResp{
				MetricSets: []*MetricSet{
					{
						Name:        "my_counter",
						Description: "this is the counter help",
						Type:        MetricTypeCounter,
						Metrics: []Metric{
							newSimpleMetric(map[string]string{}, 0),
						},
					},
					{
						Name:        "my_gauge",
						Description: "this is the gauge help",
						Type:        MetricTypeGauge,
						Metrics: []Metric{
							newSimpleMetric(map[string]string{}, 0),
						},
					},
					{
						Name:        "my_generic",
						Description: "this is the generic help",
						Type:        MetricTypeGeneric,
						Metrics: []Metric{
							newSimpleMetric(map[string]string{}, 0),
						},
					},
					{
						Name:        "my_histogram",
						Description: "this is the histogram help",
						Type:        MetricTypeHistogram,
						Metrics: []Metric{
							&HistogramMetric{
								Labels: LabelMap{},
								Buckets: []*MetricBucket{
									// Prometheus library parsing
									// includes inf bucket at minimum
									{UpperBound: math.Inf(0)},
								},
							},
						},
					},
					{
						Name:        "my_summary",
						Description: "this is the summary help",
						Type:        MetricTypeSummary,
						Metrics: []Metric{
							&SummaryMetric{
								Labels:    LabelMap{},
								Quantiles: QuantileMap{0: 0},
							},
						},
					},
				},
			},
		},
		"selected metrics": {
			req: &MetricsQueryReq{
				Host:        "host1",
				Port:        7777,
				MetricNames: []string{"my_generic", "my_counter"},
			},
			scrapeFn: mockScrapeFnSuccess(t, testMetricFam...),
			expResp: &MetricsQueryResp{
				MetricSets: []*MetricSet{
					{
						Name:        "my_generic",
						Description: "this is the generic help",
						Type:        MetricTypeGeneric,
						Metrics: []Metric{
							newSimpleMetric(map[string]string{}, 0),
						},
					},
					{
						Name:        "my_counter",
						Description: "this is the counter help",
						Type:        MetricTypeCounter,
						Metrics: []Metric{
							newSimpleMetric(map[string]string{}, 0),
						},
					},
				},
			},
		},
		"invalid metric name": {
			req: &MetricsQueryReq{
				Host:        "host1",
				Port:        7777,
				MetricNames: []string{"my_generic", "fake"},
			},
			scrapeFn: mockScrapeFnSuccess(t, testMetricFam...),
			expErr:   errors.New("metric \"fake\" not found"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			oldScrapeFn := httpScrapeFn
			httpScrapeFn = tc.scrapeFn
			if httpScrapeFn == nil {
				httpScrapeFn = func(context.Context, *url.URL, httpGetFn) ([]byte, error) {
					return nil, nil
				}
			}

			resp, err := MetricsQuery(context.TODO(), tc.req)

			httpScrapeFn = oldScrapeFn
			common.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResp, resp); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}
