//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"fmt"
	"net/http"
	"net/url"
	"sort"
	"strings"

	"github.com/pkg/errors"
	pclient "github.com/prometheus/client_model/go"
	"github.com/prometheus/common/expfmt"
)

// httpScrapeFn is the function that is used to scrape content from an HTTP
// endpoint.
var httpScrapeFn = httpGetBody

// pbMetricMap is the map returned by the prometheus scraper.
type pbMetricMap map[string]*pclient.MetricFamily

// Keys gets the sorted keys for the pbMetricMap
func (m pbMetricMap) Keys() []string {
	keys := make([]string, 0, len(m))
	for k := range m {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	return keys
}

// scrapeMetrics fetches the metrics published by the DAOS server in the
// Prometheus-compatible endpoint.
func scrapeMetrics(ctx context.Context, host string, port uint32) (pbMetricMap, error) {
	addr := &url.URL{
		Scheme: "http",
		Host:   fmt.Sprintf("%s:%d", host, port),
		Path:   "metrics",
	}
	body, err := httpScrapeFn(ctx, addr, http.Get)
	if err != nil {
		return nil, err
	}

	parser := expfmt.TextParser{}
	reader := strings.NewReader(string(body))
	result, err := parser.TextToMetricFamilies(reader)
	if err != nil {
		return nil, err
	}
	return pbMetricMap(result), nil
}

// MetricType defines the different types of metrics.
type MetricType uint32

const (
	MetricTypeUnknown MetricType = iota
	MetricTypeGeneric
	MetricTypeCounter
	MetricTypeGauge
	MetricTypeSummary
	MetricTypeHistogram
)

func (t MetricType) String() string {
	switch t {
	case MetricTypeGeneric:
		return "Generic"
	case MetricTypeCounter:
		return "Counter"
	case MetricTypeGauge:
		return "Gauge"
	case MetricTypeSummary:
		return "Summary"
	case MetricTypeHistogram:
		return "Histogram"
	}

	return "Unknown"
}

func metricTypeFromPrometheus(pType pclient.MetricType) MetricType {
	switch pType {
	case pclient.MetricType_COUNTER:
		return MetricTypeCounter
	case pclient.MetricType_GAUGE:
		return MetricTypeGauge
	case pclient.MetricType_SUMMARY:
		return MetricTypeSummary
	case pclient.MetricType_HISTOGRAM:
		return MetricTypeHistogram
	case pclient.MetricType_UNTYPED:
		return MetricTypeGeneric
	}

	return MetricTypeUnknown
}

type (
	// Metric is an interface implemented by all metric types.
	Metric interface {
		IsMetric()
	}

	// LabelMap is the set of key-value label pairs.
	LabelMap map[string]string

	// SimpleMetric is a specific metric with a value.
	SimpleMetric struct {
		Labels LabelMap `json:"labels"`
		Value  float64  `json:"value"`
	}

	// QuantileMap is the set of quantile measurements.
	QuantileMap map[float64]float64

	// SummaryMetric represents a group of observations.
	SummaryMetric struct {
		Labels      LabelMap    `json:"labels"`
		SampleCount uint64      `json:"sample_count"`
		SampleSum   float64     `json:"sample_sum"`
		Quantiles   QuantileMap `json:"quantiles"`
	}

	// MetricBucket represents a bucket for observations to be sorted into.
	MetricBucket struct {
		CumulativeCount uint64  `json:"cumulative_count"`
		UpperBound      float64 `json:"upper_bound"`
	}

	// HistogramMetric represents a group of observations sorted into
	// buckets.
	HistogramMetric struct {
		Labels      LabelMap        `json:"labels"`
		SampleCount uint64          `json:"sample_count"`
		SampleSum   float64         `json:"sample_sum"`
		Buckets     []*MetricBucket `json:"buckets"`
	}

	// MetricSet is a group of related metrics.
	MetricSet struct {
		Name        string     `json:"name"`
		Description string     `json:"description"`
		Type        MetricType `json:"type"`
		Metrics     []Metric   `json:"metrics"`
	}
)

// IsMetric identifies SimpleMetric as a Metric.
func (_ *SimpleMetric) IsMetric() {}

// IsMetric identifies SummaryMetric as a Metric.
func (_ *SummaryMetric) IsMetric() {}

// IsMetric identifies HistogramMetric as a Metric.
func (_ *HistogramMetric) IsMetric() {}

// Keys gets the sorted list of label keys.
func (m LabelMap) Keys() []string {
	result := make([]string, 0, len(m))
	for label := range m {
		result = append(result, label)
	}
	sort.Strings(result)
	return result
}

// Keys gets the sorted list of quantile keys.
func (m QuantileMap) Keys() []float64 {
	result := make([]float64, 0, len(m))
	for q := range m {
		result = append(result, q)
	}
	sort.Float64s(result)
	return result
}

type (
	// MetricsListReq is used to request the list of metrics.
	MetricsListReq struct {
		Host string // Host to query for telemetry data
		Port uint32 // Port to use for collecting telemetry data
	}

	// MetricsListResp contains the list of available metrics.
	MetricsListResp struct {
		AvailableMetricSets []*MetricSet `json:"available_metric_sets"`
	}
)

// MetricsList fetches the list of available metric types from the DAOS nodes.
func MetricsList(ctx context.Context, req *MetricsListReq) (*MetricsListResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}

	if req.Host == "" {
		return nil, errors.New("host must be specified")
	}

	if req.Port == 0 {
		return nil, errors.New("port must be specified")
	}

	scraped, err := scrapeMetrics(ctx, req.Host, req.Port)
	if err != nil {
		return nil, errors.Wrap(err, "unable to list metrics")
	}

	resp := new(MetricsListResp)

	list := make([]*MetricSet, 0, len(scraped))
	for _, name := range scraped.Keys() {
		mf := scraped[name]
		newMetric := &MetricSet{
			Name:        name,
			Description: mf.GetHelp(),
			Type:        metricTypeFromPrometheus(mf.GetType()),
		}
		list = append(list, newMetric)
	}

	resp.AvailableMetricSets = list
	return resp, nil
}

type (
	// MetricsQueryReq is used to query telemetry values.
	MetricsQueryReq struct {
		Host        string   // host to query for telemetry data
		Port        uint32   // port to use for collecting telemetry data
		MetricNames []string // if empty, collects all metrics
	}

	// MetricsQueryResp contains the list of telemetry values per host.
	MetricsQueryResp struct {
		MetricSets []*MetricSet `json:"metric_sets"`
	}
)

// MetricsQuery fetches the requested metrics values from the DAOS nodes.
func MetricsQuery(ctx context.Context, req *MetricsQueryReq) (*MetricsQueryResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}

	if req.Host == "" {
		return nil, errors.New("host must be specified")
	}

	if req.Port == 0 {
		return nil, errors.New("port must be specified")
	}

	scraped, err := scrapeMetrics(ctx, req.Host, req.Port)
	if err != nil {
		return nil, errors.Wrap(err, "unable to query metrics")
	}

	metricNames := req.MetricNames
	if len(metricNames) == 0 {
		// when caller doesn't specify metrics, we include all of them
		metricNames = scraped.Keys()
	}

	return newMetricsQueryResp(scraped, metricNames)
}

func newMetricsQueryResp(scraped pbMetricMap, metricNames []string) (*MetricsQueryResp, error) {
	resp := new(MetricsQueryResp)

	list := make([]*MetricSet, 0, len(metricNames))
	for _, name := range metricNames {
		mf, found := scraped[name]
		if !found {
			return nil, errors.Errorf("metric %q not found on host", name)
		}

		newSet := &MetricSet{
			Name:        name,
			Description: mf.GetHelp(),
			Type:        metricTypeFromPrometheus(mf.GetType()),
		}

		for _, m := range mf.Metric {
			newMetric, err := getMetricFromPrometheus(m, mf.GetType())
			if err != nil {
				// skip anything we can't process
				continue
			}
			newSet.Metrics = append(newSet.Metrics, newMetric)
		}

		list = append(list, newSet)
	}

	resp.MetricSets = list
	return resp, nil
}

func getMetricFromPrometheus(pMetric *pclient.Metric, metricType pclient.MetricType) (Metric, error) {
	labels := metricsLabelsToMap(pMetric)
	switch metricType {
	case pclient.MetricType_COUNTER:
		return newSimpleMetric(labels, pMetric.GetCounter().GetValue()), nil
	case pclient.MetricType_GAUGE:
		return newSimpleMetric(labels, pMetric.GetGauge().GetValue()), nil
	case pclient.MetricType_SUMMARY:
		summary := pMetric.GetSummary()
		newMetric := &SummaryMetric{
			Labels:      labels,
			SampleSum:   summary.GetSampleSum(),
			SampleCount: summary.GetSampleCount(),
			Quantiles:   QuantileMap{},
		}
		for _, q := range summary.Quantile {
			newMetric.Quantiles[q.GetQuantile()] = q.GetValue()
		}
		return newMetric, nil
	case pclient.MetricType_HISTOGRAM:
		histogram := pMetric.GetHistogram()
		newMetric := &HistogramMetric{
			Labels:      labels,
			SampleSum:   histogram.GetSampleSum(),
			SampleCount: histogram.GetSampleCount(),
		}
		for _, b := range histogram.Bucket {
			newMetric.Buckets = append(newMetric.Buckets,
				&MetricBucket{
					UpperBound:      b.GetUpperBound(),
					CumulativeCount: b.GetCumulativeCount(),
				})
		}
		return newMetric, nil
	case pclient.MetricType_UNTYPED:
		return newSimpleMetric(labels, pMetric.GetUntyped().GetValue()), nil
	}

	return nil, errors.New("unknown metric type")
}

func newSimpleMetric(labels map[string]string, value float64) *SimpleMetric {
	return &SimpleMetric{
		Labels: labels,
		Value:  value,
	}
}

func metricsLabelsToMap(m *pclient.Metric) map[string]string {
	result := make(map[string]string)

	for _, label := range m.GetLabel() {
		result[label.GetName()] = label.GetValue()
	}
	return result
}
