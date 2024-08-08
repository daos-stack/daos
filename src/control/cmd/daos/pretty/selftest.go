//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pretty

import (
	"fmt"
	"io"
	"sort"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
	"github.com/daos-stack/daos/src/control/lib/ui"
)

type timeUnit uint64

const (
	ns timeUnit = 0
	us timeUnit = 1000
	ms timeUnit = 1000 * 1000
	s  timeUnit = 1000 * 1000 * 1000
)

func (u timeUnit) String() string {
	switch u {
	case ns:
		return "ns"
	case us:
		return "μs"
	case ms:
		return "ms"
	case s:
		return "s"
	default:
		return "unknown"
	}
}

func printLatencyVal(val float64, u timeUnit) string {
	return fmt.Sprintf("%.02f%s", val/float64(u), u)
}

// PrintSelfTestResult generates a human-readable representation of the supplied
// daos.SelfTestResult struct and writes it to the supplied io.Writer.
func PrintSelfTestResult(out io.Writer, result *daos.SelfTestResult, verbose, showBytes bool) error {
	if result == nil {
		return errors.Errorf("nil %T", result)
	}

	rpcThroughput := float64(result.MasterLatency.Succeeded()) / result.Duration.Seconds()

	epRanks := ranklist.NewRankSet()
	epTgts := hostlist.NewNumericSet()
	for _, ep := range result.TargetEndpoints {
		epRanks.Add(ep.Rank)
		epTgts.Add(uint(ep.Tag))
	}
	srvEpTitle := "Server Endpoint"
	if epRanks.Count() > 1 {
		srvEpTitle += "s"
	}
	summary := []txtfmt.TableRow{
		{srvEpTitle: epRanks.RangedString() + ":" + epTgts.RangedString()},
		{"RPC Throughput": fmt.Sprintf("%.02f RPC/s", rpcThroughput)},
	}
	if result.SendSize > 0 || result.ReplySize > 0 {
		suffix := "B/s"
		bw := rpcThroughput * (float64(result.SendSize) + float64(result.ReplySize))
		if !showBytes {
			bw *= 8
			suffix = "bps"
		}
		summary = append(summary, txtfmt.TableRow{
			"RPC Bandwidth": ui.FmtHumanSize(bw, suffix, false),
		})
	}
	_, masterBuckets := result.MasterLatency.Percentiles()
	summary = append(summary, txtfmt.TableRow{
		"Average Latency": printLatencyVal(float64(result.MasterLatency.Average()), ms),
	})
	if l, found := masterBuckets[95]; found {
		summary = append(summary, txtfmt.TableRow{
			"95% Latency": printLatencyVal(l.UpperBound, ms),
		})
	}
	if l, found := masterBuckets[99]; found {
		summary = append(summary, txtfmt.TableRow{
			"99% Latency": printLatencyVal(l.UpperBound, ms),
		})
	}
	if verbose {
		summary = append(summary, []txtfmt.TableRow{
			{"Client Endpoint": result.MasterEndpoint.String()},
			{"Duration": result.Duration.String()},
			{"Repetitions": fmt.Sprintf("%d", result.Repetitions)},
			{"Send Size": ui.FmtHumanSize(float64(result.SendSize), "B", true)},
			{"Reply Size": ui.FmtHumanSize(float64(result.ReplySize), "B", true)},
		}...)
	}
	if result.MasterLatency.FailCount > 0 {
		failPct := (float64(result.MasterLatency.FailCount) / float64(result.Repetitions)) * 100
		summary = append(summary, txtfmt.TableRow{
			"Failed RPCs": fmt.Sprintf("%d (%.01f%%)", result.MasterLatency.FailCount, failPct),
		})
	}
	ef := txtfmt.NewEntityFormatter("Client/Server Network Test Summary", 2)
	fmt.Fprintln(out, ef.Format(summary))

	if !verbose {
		return nil
	}

	fmt.Fprintln(out, "Per-Target Latency Results")
	iw := txtfmt.NewIndentWriter(out)

	var hasFailed bool
	dispUnit := ms // TODO: Calculate based on average value?
	pctTitles := make(map[uint64]string)
	var table []txtfmt.TableRow
	for _, ep := range result.TargetEndpoints {
		el, found := result.TargetLatencies[ep]
		if !found {
			continue
		}

		if el.FailCount > 0 {
			hasFailed = true
		}
		pcts, buckets := el.Percentiles()

		row := txtfmt.TableRow{
			"Target": ep.String(),
			"Min":    printLatencyVal(float64(el.Min), dispUnit),
			"Max":    printLatencyVal(float64(el.Max), dispUnit),
			"Failed": fmt.Sprintf("%.01f%%", float64(el.FailCount)/float64(el.TotalRPCs)*100),
		}
		if verbose {
			row["Average"] = printLatencyVal(float64(el.Average()), dispUnit)
			row["StdDev"] = printLatencyVal(el.StdDev(), dispUnit)
		}

		for _, pct := range pcts {
			pctTitles[pct] = fmt.Sprintf("%d%%", pct)
			row[pctTitles[pct]] = printLatencyVal(buckets[pct].UpperBound, dispUnit)
		}

		table = append(table, row)
	}

	var pctKeys []uint64
	for key := range pctTitles {
		pctKeys = append(pctKeys, key)
	}
	sort.Slice(pctKeys, func(a, b int) bool {
		return pctKeys[a] < pctKeys[b]
	})
	titles := []string{"Target", "Min"}
	for _, key := range pctKeys {
		titles = append(titles, pctTitles[key])
	}
	titles = append(titles, "Max")
	if verbose {
		titles = append(titles, "Average")
		titles = append(titles, "StdDev")
	}
	if hasFailed {
		titles = append(titles, "Failed")
	}
	tf := txtfmt.NewTableFormatter(titles...)
	tf.InitWriter(iw)
	tf.Format(table)

	return nil
}

// PrintSelfTestResults generates a human-readable representation of the supplied
// slice of daos.SelfTestResult structs and writes it to the supplied io.Writer.
func PrintSelfTestResults(out io.Writer, results []*daos.SelfTestResult, verbose, showBytes bool) error {
	if len(results) == 0 {
		fmt.Fprintln(out, "No test results.")
	}
	if len(results) > 1 {
		fmt.Fprintf(out, "Showing %d self test results:\n", len(results))
		out = txtfmt.NewIndentWriter(out)
	}
	for _, res := range results {
		if err := PrintSelfTestResult(out, res, verbose, showBytes); err != nil {
			return err
		}
	}

	return nil
}

// PrintSelfTestConfig generates a human-readable representation of the self_test configuration.
func PrintSelfTestConfig(out io.Writer, cfg *daos.SelfTestConfig, verbose bool) error {
	if cfg == nil {
		return errors.Errorf("nil %T", cfg)
	}

	srvRow := func(r []ranklist.Rank) txtfmt.TableRow {
		srvTitle := "Server"
		if len(r) == 1 {
			return txtfmt.TableRow{srvTitle: fmt.Sprintf("%d", r[0])}
		}
		srvTitle += "s"
		if len(r) == 0 {
			return txtfmt.TableRow{srvTitle: "All"}
		}
		return txtfmt.TableRow{srvTitle: ranklist.RankSetFromRanks(r).RangedString()}
	}
	rpcSizeRow := func(dir string, sizes []uint64) txtfmt.TableRow {
		title := fmt.Sprintf("%s RPC Size", dir)
		if len(sizes) == 0 {
			return txtfmt.TableRow{title: "None"}
		} else if len(sizes) == 1 {
			return txtfmt.TableRow{title: ui.FmtHumanSize(float64(sizes[0]), "B", true)}
		}
		sizeStrs := make([]string, len(sizes))
		for i, size := range sizes {
			sizeStrs[i] = ui.FmtHumanSize(float64(size), "B", true)
		}
		return txtfmt.TableRow{title + "s": fmt.Sprintf("%v", sizeStrs)}
	}
	cfgRows := []txtfmt.TableRow{
		srvRow(cfg.EndpointRanks),
		rpcSizeRow("Send", cfg.SendSizes),
		rpcSizeRow("Reply", cfg.ReplySizes),
		{"RPCs Per Server": fmt.Sprintf("%d", cfg.Repetitions)},
	}
	if verbose {
		tagRow := func(t []uint32) txtfmt.TableRow {
			tagTitle := "Tag"
			if len(t) == 1 {
				return txtfmt.TableRow{tagTitle: fmt.Sprintf("%d", t[0])}
			}
			tagTitle += "s"
			if len(t) == 0 {
				return txtfmt.TableRow{tagTitle: "ERROR (0 tags)"} // Can't(?) happen...
			}
			return txtfmt.TableRow{tagTitle: ranklist.RankSetFromRanks(ranklist.RanksFromUint32(t)).RangedString()}
		}
		cfgRows = append(cfgRows, []txtfmt.TableRow{
			{"System Name": cfg.GroupName},
			tagRow(cfg.EndpointTags),
			{"Max In-Flight RPCs": fmt.Sprintf("%d", cfg.MaxInflightRPCs)},
		}...)
	}

	ef := txtfmt.NewEntityFormatter("Client/Server Network Test Parameters", 2)
	fmt.Fprintln(out, ef.Format(cfgRows))

	return nil
}
