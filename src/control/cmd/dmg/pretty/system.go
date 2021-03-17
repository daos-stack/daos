//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pretty

import (
	"fmt"
	"io"
	"strings"

	"github.com/dustin/go-humanize/english"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
	"github.com/daos-stack/daos/src/control/system"
)

// tabulateRankGroups produces a representation of rank groupings in a tabular form.
func tabulateRankGroups(out io.Writer, groups system.RankGroups, titles ...string) error {
	if len(titles) < 2 {
		return errors.New("insufficient number of column titles")
	}
	groupTitle := titles[0]
	columnTitles := titles[1:]

	formatter := txtfmt.NewTableFormatter(titles...)
	var table []txtfmt.TableRow

	for _, result := range groups.Keys() {
		row := txtfmt.TableRow{groupTitle: groups[result].RangedString()}

		summary := strings.Split(result, rowFieldSep)
		if len(summary) != len(columnTitles) {
			return errors.New("unexpected summary format")
		}
		for i, title := range columnTitles {
			row[title] = summary[i]
		}

		table = append(table, row)
	}

	fmt.Fprintln(out, formatter.Format(table))

	return nil
}

func printAbsentHosts(out io.Writer, absentHosts *hostlist.HostSet) {
	if absentHosts.Count() > 0 {
		fmt.Fprintf(out, "Unknown %s: %s\n",
			english.Plural(absentHosts.Count(), "host", "hosts"),
			absentHosts.String())
	}
}

func printAbsentRanks(out io.Writer, absentRanks *system.RankSet) {
	if absentRanks.Count() > 0 {
		fmt.Fprintf(out, "Unknown %s: %s\n",
			english.Plural(absentRanks.Count(), "rank", "ranks"),
			absentRanks.String())
	}
}

func printSystemQuery(out io.Writer, members system.Members, absentRanks *system.RankSet) error {
	groups := make(system.RankGroups)
	if err := groups.FromMembers(members); err != nil {
		return err
	}

	if absentRanks.Count() != 0 {
		groups["Unknown Rank"] = absentRanks
	}

	if err := tabulateRankGroups(out, groups, "Rank", "State"); err != nil {
		return errors.Wrap(err, "printing state table")
	}

	return nil
}

func printSystemQueryVerbose(out io.Writer, members system.Members) {
	rankTitle := "Rank"
	uuidTitle := "UUID"
	addrTitle := "Control Address"
	faultDomainTitle := "Fault Domain"
	stateTitle := "State"
	reasonTitle := "Reason"

	formatter := txtfmt.NewTableFormatter(rankTitle, uuidTitle, addrTitle, faultDomainTitle, stateTitle, reasonTitle)
	var table []txtfmt.TableRow

	for _, m := range members {
		row := txtfmt.TableRow{rankTitle: fmt.Sprintf("%d", m.Rank)}
		row[uuidTitle] = m.UUID.String()
		row[addrTitle] = m.Addr.String()
		row[faultDomainTitle] = m.FaultDomain.String()
		row[stateTitle] = m.State().String()
		row[reasonTitle] = m.Info

		table = append(table, row)
	}

	fmt.Fprintln(out, formatter.Format(table))
}

// PrintSystemQueryResponse generates a human-readable representation of the supplied
// SystemQueryResp struct and writes it to the supplied io.Writer.
func PrintSystemQueryResponse(out, outErr io.Writer, resp *control.SystemQueryResp, opts ...PrintConfigOption) error {
	if resp == nil {
		return errors.Errorf("nil %T", resp)
	}

	switch {
	case len(resp.Members) == 0:
		fmt.Fprintln(out, "Query matches no ranks in system")
	case getPrintConfig(opts...).Verbose:
		printSystemQueryVerbose(out, resp.Members)
	default:
		if err := printSystemQuery(out, resp.Members, &resp.AbsentRanks); err != nil {
			return err
		}
		printAbsentHosts(outErr, &resp.AbsentHosts)

		return nil
	}

	printAbsentHosts(outErr, &resp.AbsentHosts)
	printAbsentRanks(outErr, &resp.AbsentRanks)

	return nil
}

func printSystemResultTable(out io.Writer, results system.MemberResults, absentRanks *system.RankSet) error {
	groups := make(system.RankGroups)
	if err := groups.FromMemberResults(results, rowFieldSep); err != nil {
		return err
	}

	if absentRanks.Count() > 0 {
		groups[fmt.Sprintf("----%sUnknown Rank", rowFieldSep)] = absentRanks
	}

	if err := tabulateRankGroups(out, groups, "Rank", "Operation", "Result"); err != nil {
		return errors.Wrap(err, "printing result table")
	}

	return nil
}

func printSystemResults(out, outErr io.Writer, results system.MemberResults, absentHosts *hostlist.HostSet, absentRanks *system.RankSet) error {
	if len(results) == 0 {
		fmt.Fprintln(out, "No results returned")
		printAbsentHosts(outErr, absentHosts)
		printAbsentRanks(outErr, absentRanks)

		return nil
	}

	if err := printSystemResultTable(out, results, absentRanks); err != nil {
		return err
	}
	printAbsentHosts(outErr, absentHosts)

	return nil
}

// PrintSystemStartResponse generates a human-readable representation of the
// supplied SystemStartResp struct and writes it to the supplied io.Writer.
func PrintSystemStartResponse(out, outErr io.Writer, resp *control.SystemStartResp) error {
	return printSystemResults(out, outErr, resp.Results, &resp.AbsentHosts, &resp.AbsentRanks)
}

// PrintSystemStopResponse generates a human-readable representation of the
// supplied SystemStopResp struct and writes it to the supplied io.Writer.
func PrintSystemStopResponse(out, outErr io.Writer, resp *control.SystemStopResp) error {
	return printSystemResults(out, outErr, resp.Results, &resp.AbsentHosts, &resp.AbsentRanks)
}

// PrintListPoolsResponse generates a human-readable representation of the
// supplied ListPoolsResp struct and writes it to the supplied io.Writer.
func PrintListPoolsResponse(out io.Writer, resp *control.ListPoolsResp) error {
	if len(resp.Pools) == 0 {
		fmt.Fprintln(out, "no pools in system")
		return nil
	}

	uuidTitle := "Pool UUID"
	svcRepTitle := "Svc Replicas"

	formatter := txtfmt.NewTableFormatter(uuidTitle, svcRepTitle)
	var table []txtfmt.TableRow

	for _, pool := range resp.Pools {
		row := txtfmt.TableRow{uuidTitle: pool.UUID}

		if len(pool.SvcReplicas) != 0 {
			row[svcRepTitle] = formatRanks(pool.SvcReplicas)
		}

		table = append(table, row)
	}
	fmt.Fprintln(out, formatter.Format(table))

	return nil
}
