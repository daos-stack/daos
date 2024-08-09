//
// (C) Copyright 2021-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"fmt"
	"net/url"
	"sort"
	"strings"

	"github.com/daos-stack/daos/src/control/lib/daos"
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

func metricTypeFromPrometheus(pType pclient.MetricType) daos.MetricType {
	switch pType {
	case pclient.MetricType_COUNTER:
		return daos.MetricTypeCounter
	case pclient.MetricType_GAUGE:
		return daos.MetricTypeGauge
	case pclient.MetricType_SUMMARY:
		return daos.MetricTypeSummary
	case pclient.MetricType_HISTOGRAM:
		return daos.MetricTypeHistogram
	case pclient.MetricType_UNTYPED:
		return daos.MetricTypeGeneric
	}

	return daos.MetricTypeUnknown
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
		AvailableMetricSets []*daos.MetricSet `json:"available_metric_sets"`
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

	list := make([]*daos.MetricSet, 0, len(scraped))
	for _, name := range scraped.Keys() {
		mf := scraped[name]
		newMetric := &daos.MetricSet{
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
		MetricSets []*daos.MetricSet `json:"metric_sets"`
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

	list := make([]*daos.MetricSet, 0, len(metricNames))
	for _, name := range metricNames {
		mf, found := scraped[name]
		if !found {
			return nil, errors.Errorf("metric %q not found on host", name)
		}

		newSet := &daos.MetricSet{
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

func getMetricFromPrometheus(pMetric *pclient.Metric, metricType pclient.MetricType) (daos.Metric, error) {
	labels := metricsLabelsToMap(pMetric)
	switch metricType {
	case pclient.MetricType_COUNTER:
		return newSimpleMetric(labels, pMetric.GetCounter().GetValue()), nil
	case pclient.MetricType_GAUGE:
		return newSimpleMetric(labels, pMetric.GetGauge().GetValue()), nil
	case pclient.MetricType_SUMMARY:
		summary := pMetric.GetSummary()
		newMetric := &daos.SummaryMetric{
			Labels:      labels,
			SampleSum:   summary.GetSampleSum(),
			SampleCount: summary.GetSampleCount(),
			Quantiles:   daos.QuantileMap{},
		}
		for _, q := range summary.Quantile {
			newMetric.Quantiles[q.GetQuantile()] = q.GetValue()
		}
		return newMetric, nil
	case pclient.MetricType_HISTOGRAM:
		histogram := pMetric.GetHistogram()
		newMetric := &daos.HistogramMetric{
			Labels:      labels,
			SampleSum:   histogram.GetSampleSum(),
			SampleCount: histogram.GetSampleCount(),
		}
		for _, b := range histogram.Bucket {
			newMetric.Buckets = append(newMetric.Buckets,
				&daos.MetricBucket{
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

func newSimpleMetric(labels map[string]string, value float64) *daos.SimpleMetric {
	return &daos.SimpleMetric{
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
