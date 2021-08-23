//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"encoding/json"
	"fmt"
	"net/url"
	"sort"
	"strconv"
	"strings"

	"github.com/pkg/errors"
	pclient "github.com/prometheus/client_model/go"
	"github.com/prometheus/common/expfmt"
)

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

func getMetricsURL(host string, port uint32) *url.URL {
	return &url.URL{
		Scheme: "http",
		Host:   fmt.Sprintf("%s:%d", host, port),
		Path:   "metrics",
	}
}

// scrapeMetrics fetches the metrics published by the DAOS server in the
// Prometheus-compatible endpoint.
func scrapeMetrics(ctx context.Context, req httpGetter) (pbMetricMap, error) {
	body, err := httpGetBodyRetry(ctx, req)
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

	metricTypeUnknownStr   = "Unknown"
	metricTypeGenericStr   = "Generic"
	metricTypeCounterStr   = "Counter"
	metricTypeGaugeStr     = "Gauge"
	metricTypeSummaryStr   = "Summary"
	metricTypeHistogramStr = "Histogram"
)

func (t MetricType) String() string {
	switch t {
	case MetricTypeGeneric:
		return metricTypeGenericStr
	case MetricTypeCounter:
		return metricTypeCounterStr
	case MetricTypeGauge:
		return metricTypeGaugeStr
	case MetricTypeSummary:
		return metricTypeSummaryStr
	case MetricTypeHistogram:
		return metricTypeHistogramStr
	}

	return metricTypeUnknownStr
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

func metricTypeFromString(typeStr string) MetricType {
	// normalize the strings for comparison
	switch strings.ToLower(typeStr) {
	case strings.ToLower(metricTypeCounterStr):
		return MetricTypeCounter
	case strings.ToLower(metricTypeGaugeStr):
		return MetricTypeGauge
	case strings.ToLower(metricTypeSummaryStr):
		return MetricTypeSummary
	case strings.ToLower(metricTypeHistogramStr):
		return MetricTypeHistogram
	case strings.ToLower(metricTypeGenericStr):
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
func (*SimpleMetric) IsMetric() {}

// IsMetric identifies SummaryMetric as a Metric.
func (*SummaryMetric) IsMetric() {}

// UnmarshalJSON unmarshals a SummaryMetric from JSON.
func (m *SummaryMetric) UnmarshalJSON(data []byte) error {
	if m == nil {
		return errors.New("nil SummaryMetric")
	}

	if m.Quantiles == nil {
		m.Quantiles = make(QuantileMap)
	}

	type Alias SummaryMetric
	aux := (*Alias)(m)
	if err := json.Unmarshal(data, &aux); err != nil {
		return err
	}

	return nil
}

// IsMetric identifies HistogramMetric as a Metric.
func (*HistogramMetric) IsMetric() {}

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

// MarshalJSON marshals the QuantileMap into JSON.
func (m QuantileMap) MarshalJSON() ([]byte, error) {
	strMap := make(map[string]string)

	fmtFloat := func(f float64) string {
		return strconv.FormatFloat(f, 'g', -1, 64)
	}

	for key, val := range m {
		strMap[fmtFloat(key)] = fmtFloat(val)
	}

	return json.Marshal(&strMap)
}

// UnmarshalJSON unmarshals the QuantileMap from JSON.
func (m QuantileMap) UnmarshalJSON(data []byte) error {
	if m == nil {
		return errors.New("QuantileMap is nil")
	}

	fromJSON := make(map[string]string)

	if err := json.Unmarshal(data, &fromJSON); err != nil {
		return nil
	}

	for key, val := range fromJSON {
		floatKey, err := strconv.ParseFloat(key, 64)
		if err != nil {
			return errors.Wrapf(err, "QuantileMap key %q", key)
		}

		floatVal, err := strconv.ParseFloat(val, 64)
		if err != nil {
			return errors.Wrapf(err, "QuantileMap value %q for key %q", val, key)
		}

		m[floatKey] = floatVal
	}
	return nil
}

// MarshalJSON marshals the MetricSet to JSON.
func (ms *MetricSet) MarshalJSON() ([]byte, error) {
	type toJSON MetricSet
	return json.Marshal(&struct {
		Type string `json:"type"`
		*toJSON
	}{
		Type:   strings.ToLower(ms.Type.String()),
		toJSON: (*toJSON)(ms),
	})
}

// jsonMetric serves as a universal metric representation for unmarshaling from
// JSON. It covers all possible fields of Metric types.
type jsonMetric struct {
	Labels      LabelMap        `json:"labels"`
	Value       float64         `json:"value"`
	SampleCount uint64          `json:"sample_count"`
	SampleSum   float64         `json:"sample_sum"`
	Quantiles   QuantileMap     `json:"quantiles"`
	Buckets     []*MetricBucket `json:"buckets"`
}

// UnmarshalJSON unmarshals a Metric into the jsonMetric type.
func (jm *jsonMetric) UnmarshalJSON(data []byte) error {
	if jm == nil {
		return errors.New("nil jsonMetric")
	}

	if jm.Quantiles == nil {
		jm.Quantiles = make(QuantileMap)
	}

	type Alias jsonMetric
	aux := (*Alias)(jm)
	if err := json.Unmarshal(data, &aux); err != nil {
		return err
	}

	return nil
}

// UnmarshalJSON unmarshals the MetricSet from JSON.
func (ms *MetricSet) UnmarshalJSON(data []byte) error {
	if ms == nil {
		return errors.New("nil MetricSet")
	}

	type fromJSON MetricSet
	from := &struct {
		Type    string        `json:"type"`
		Metrics []*jsonMetric `json:"metrics"`
		*fromJSON
	}{
		fromJSON: (*fromJSON)(ms),
	}
	if err := json.Unmarshal(data, from); err != nil {
		return err
	}

	ms.Type = metricTypeFromString(from.Type)
	for _, m := range from.Metrics {
		switch ms.Type {
		case MetricTypeSummary:
			ms.Metrics = append(ms.Metrics, &SummaryMetric{
				Labels:      m.Labels,
				SampleCount: m.SampleCount,
				SampleSum:   m.SampleSum,
				Quantiles:   m.Quantiles,
			})
		case MetricTypeHistogram:
			ms.Metrics = append(ms.Metrics, &HistogramMetric{
				Labels:      m.Labels,
				SampleCount: m.SampleCount,
				SampleSum:   m.SampleSum,
				Buckets:     m.Buckets,
			})
		default:
			ms.Metrics = append(ms.Metrics, newSimpleMetric(m.Labels, m.Value))
		}
	}
	return nil
}

type (
	// MetricsListReq is used to request the list of metrics.
	MetricsListReq struct {
		httpReq
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

	req.url = getMetricsURL(req.Host, req.Port)

	scraped, err := scrapeMetrics(ctx, req)
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
		httpReq
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

	req.url = getMetricsURL(req.Host, req.Port)

	scraped, err := scrapeMetrics(ctx, req)
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
