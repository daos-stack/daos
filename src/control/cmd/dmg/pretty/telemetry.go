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

// PrintMetricsListResp formats the MetricsListResp for human-readable output,
// and writes it to the io.Writer.
func PrintMetricsListResp(resp *control.MetricsListResp, out io.Writer) error {
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

// PrintMetricsQueryResp formats a MetricsQueryResp for human-readable output,
// and writes it to the io.Writer.
func PrintMetricsQueryResp(resp *control.MetricsQueryResp, out io.Writer) error {
	if resp == nil {
		return errors.New("nil response")
	}

	if len(resp.MetricSets) == 0 {
		return nil
	}

	for i, set := range resp.MetricSets {
		if i > 0 { // a little space between metric sets
			fmt.Fprintf(out, "\n")
		}
		fmt.Fprintf(out, "- Metric Set: %s (Type: %s)\n", set.Name, set.Type.String())

		dw := txtfmt.NewIndentWriter(out)
		fmt.Fprintf(dw, "%s\n", set.Description)

		iw := txtfmt.NewIndentWriter(dw)
		printMetrics(iw, set.Metrics, set.Type)

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
		switch m.(type) {
		case *control.SimpleMetric:
			sm, _ := m.(*control.SimpleMetric)
			labels := metricLabelsToStr(sm.Labels)
			name := metricType.String()
			table = append(table, txtfmt.TableRow{
				nameTitle:  name,
				labelTitle: labels,
				valTitle:   fmt.Sprintf("%g", sm.Value),
			})
		case *control.SummaryMetric:
			sm, _ := m.(*control.SummaryMetric)

			labels := metricLabelsToStr(sm.Labels)
			table = append(table, txtfmt.TableRow{
				nameTitle:  "Sample Count",
				labelTitle: labels,
				valTitle:   fmt.Sprintf("%d", sm.SampleCount),
			})
			table = append(table, txtfmt.TableRow{
				nameTitle:  "Sample Sum",
				labelTitle: labels,
				valTitle:   fmt.Sprintf("%g", sm.SampleSum),
			})
			for _, quant := range sm.Quantiles.Keys() {
				table = append(table, txtfmt.TableRow{
					nameTitle:  fmt.Sprintf("Quantile(%g)", quant),
					labelTitle: labels,
					valTitle:   fmt.Sprintf("%g", sm.Quantiles[quant]),
				})
			}
		case *control.HistogramMetric:
			hm, _ := m.(*control.HistogramMetric)

			labels := metricLabelsToStr(hm.Labels)
			table = append(table, txtfmt.TableRow{
				nameTitle:  "Sample Count",
				labelTitle: labels,
				valTitle:   fmt.Sprintf("%d", hm.SampleCount),
			})
			table = append(table, txtfmt.TableRow{
				nameTitle:  "Sample Sum",
				labelTitle: labels,
				valTitle:   fmt.Sprintf("%g", hm.SampleSum),
			})
			for i, bucket := range hm.Buckets {
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
		default:
		}
	}

	tablePrint.Format(table)
}

func metricLabelsToStr(labels control.LabelMap) string {
	if len(labels) == 0 {
		return "N/A"
	}

	var b strings.Builder

	first := true
	for _, k := range labels.Keys() {
		if first {
			first = false
		} else {
			b.WriteString(", ")
		}
		fmt.Fprintf(&b, "%s=%s", k, labels[k])
	}

	return fmt.Sprintf("(%s)", b.String())
}
