//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pretty

import (
	"errors"
	"fmt"
	"io"
	"strings"

	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
)

// PrintMetricsListResp formats the MetricsListResp as a table of available
// metric sets with the name, type, and description for each.
func PrintMetricsListResp(out io.Writer, resp *control.MetricsListResp) error {
	if resp == nil {
		return errors.New("nil response")
	}

	if len(resp.AvailableMetricSets) == 0 {
		return nil
	}

	nameTitle := "Metric Set"
	typeTitle := "Type"
	descTitle := "Description"

	tablePrint := txtfmt.NewTableFormatter(nameTitle, typeTitle, descTitle)
	tablePrint.InitWriter(out)
	table := []txtfmt.TableRow{}

	for _, m := range resp.AvailableMetricSets {
		row := txtfmt.TableRow{
			nameTitle: m.Name,
			typeTitle: m.Type.String(),
			descTitle: m.Description,
		}

		table = append(table, row)
	}

	tablePrint.Format(table)

	return nil
}

// PrintMetricsQueryResp formats a MetricsQueryResp as a list of metric sets.
// For each metric set, it includes a table of metrics in the set, detailing the
// identifying information and value for each.
func PrintMetricsQueryResp(out io.Writer, resp *control.MetricsQueryResp) error {
	if resp == nil {
		return errors.New("nil response")
	}

	if len(resp.MetricSets) == 0 {
		return nil
	}

	for _, set := range resp.MetricSets {
		fmt.Fprintf(out, "- Metric Set: %s (Type: %s)\n", set.Name, set.Type.String())

		dw := txtfmt.NewIndentWriter(out)
		fmt.Fprintf(dw, "%s\n", set.Description)

		iw := txtfmt.NewIndentWriter(dw)
		printMetrics(iw, set.Metrics, set.Type)

		fmt.Fprintf(out, "\n")
	}
	return nil
}

func printMetrics(out io.Writer, metrics []control.Metric, metricType control.MetricType) {
	if len(metrics) == 0 {
		fmt.Fprintf(out, "No metrics found\n")
		return
	}

	nameTitle := "Metric"
	labelTitle := "Labels"
	valTitle := "Value"

	tablePrint := txtfmt.NewTableFormatter(nameTitle, labelTitle, valTitle)
	tablePrint.InitWriter(out)
	table := []txtfmt.TableRow{}

	for _, m := range metrics {
		switch realM := m.(type) {
		case *control.SimpleMetric:
			labels := metricLabelsToStr(realM.Labels)
			name := metricType.String()
			table = append(table, txtfmt.TableRow{
				nameTitle:  name,
				labelTitle: labels,
				valTitle:   fmt.Sprintf("%g", realM.Value),
			})
		case *control.SummaryMetric:
			labels := metricLabelsToStr(realM.Labels)
			table = append(table, txtfmt.TableRow{
				nameTitle:  "Sample Count",
				labelTitle: labels,
				valTitle:   fmt.Sprintf("%d", realM.SampleCount),
			})
			table = append(table, txtfmt.TableRow{
				nameTitle:  "Sample Sum",
				labelTitle: labels,
				valTitle:   fmt.Sprintf("%g", realM.SampleSum),
			})
			for _, quant := range realM.Quantiles.Keys() {
				table = append(table, txtfmt.TableRow{
					nameTitle:  fmt.Sprintf("Quantile(%g)", quant),
					labelTitle: labels,
					valTitle:   fmt.Sprintf("%g", realM.Quantiles[quant]),
				})
			}
		case *control.HistogramMetric:
			labels := metricLabelsToStr(realM.Labels)
			table = append(table, txtfmt.TableRow{
				nameTitle:  "Sample Count",
				labelTitle: labels,
				valTitle:   fmt.Sprintf("%d", realM.SampleCount),
			})
			table = append(table, txtfmt.TableRow{
				nameTitle:  "Sample Sum",
				labelTitle: labels,
				valTitle:   fmt.Sprintf("%g", realM.SampleSum),
			})
			for i, bucket := range realM.Buckets {
				name := fmt.Sprintf("Bucket(%d)", i)
				table = append(table, txtfmt.TableRow{
					nameTitle:  fmt.Sprintf("%s Upper Bound", name),
					labelTitle: labels,
					valTitle:   fmt.Sprintf("%g", bucket.UpperBound),
				})
				table = append(table, txtfmt.TableRow{
					nameTitle:  fmt.Sprintf("%s Cumulative Count", name),
					labelTitle: labels,
					valTitle:   fmt.Sprintf("%d", bucket.CumulativeCount),
				})
			}
		}
	}

	tablePrint.Format(table)
}

func metricLabelsToStr(labels control.LabelMap) string {
	if len(labels) == 0 {
		return "N/A"
	}

	labelStr := make([]string, 0, len(labels))

	for _, k := range labels.Keys() {
		labelStr = append(labelStr, fmt.Sprintf("%s=%s", k, labels[k]))
	}

	return fmt.Sprintf("(%s)", strings.Join(labelStr, ", "))
}
