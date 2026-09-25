//
// (C) Copyright 2021-2024 Intel Corporation.
// (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
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

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
	"github.com/daos-stack/daos/src/control/system"
)

const rowFieldSep = "\t"

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
			return errors.Errorf("unexpected summary format, fields %v values %v",
				columnTitles, summary)
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

func printAbsentRanks(out io.Writer, absentRanks *ranklist.RankSet) {
	if absentRanks.Count() > 0 {
		fmt.Fprintf(out, "Unknown %s: %s\n",
			english.Plural(absentRanks.Count(), "rank", "ranks"),
			absentRanks.String())
	}
}

func printSysSelfHealUnsetFlags(out io.Writer, propVal string) {
	offFlags := daos.SystemPropertySelfHealUnsetFlags(propVal)
	if len(offFlags) > 0 {
		fmt.Fprintf(out, "System property self_heal %s disabled: %s\n",
			english.PluralWord(len(offFlags), "flag", "flags"),
			strings.Join(offFlags, ", "))
	}
}

func printSystemQuery(out io.Writer, members system.Members, absentRanks *ranklist.RankSet) error {
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
		row[stateTitle] = m.State.String()
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
		printSysSelfHealUnsetFlags(out, resp.SysSelfHealPolicy)

		return nil
	}

	printAbsentHosts(outErr, &resp.AbsentHosts)
	printAbsentRanks(outErr, &resp.AbsentRanks)
	printSysSelfHealUnsetFlags(out, resp.SysSelfHealPolicy)

	return nil
}

func printSystemResultTable(out io.Writer, results system.MemberResults, absentRanks *ranklist.RankSet) error {
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

func printSystemResults(out, outErr io.Writer, results system.MemberResults, absentHosts *hostlist.HostSet, absentRanks *ranklist.RankSet) error {
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

func printSystemCleanupRespVerbose(out io.Writer, resp *control.SystemCleanupResp) {
	if len(resp.Results) == 0 {
		fmt.Fprintln(out, "no handles cleaned up")
		return
	}

	titles := []string{"Pool", "Handles Revoked"}
	formatter := txtfmt.NewTableFormatter(titles...)

	var table []txtfmt.TableRow
	for _, r := range resp.Results {
		row := txtfmt.TableRow{
			"Pool":            r.PoolID,
			"Handles Revoked": fmt.Sprintf("%d", r.Count),
		}
		table = append(table, row)
	}

	fmt.Fprintln(out, formatter.Format(table))
}

// PrintSystemCleanupResponse generates a human-readable representation of the
// supplied SystemCleanupResp struct and writes it to the supplied io.Writer.
func PrintSystemCleanupResponse(out io.Writer, resp *control.SystemCleanupResp, verbose bool) {
	if len(resp.Results) == 0 {
		fmt.Fprintln(out, "No handles cleaned up")
		return
	}

	if verbose {
		printSystemCleanupRespVerbose(out, resp)
		return
	}

	fmt.Fprintln(out, "System Cleanup Success")
}

// PrintSystemProperties generates a human readable representation of the property supplied.
func PrintSystemProperties(out io.Writer, props []*daos.SystemProperty) {
	if len(props) == 0 {
		fmt.Fprintln(out, "No system properties found.")
		return
	}

	nameTitle := "Name"
	valueTitle := "Value"
	table := []txtfmt.TableRow{}
	for _, prop := range props {
		row := txtfmt.TableRow{}
		row[nameTitle] = fmt.Sprintf("%s (%s)", prop.Description, prop.Key)
		row[valueTitle] = prop.Value.String()
		table = append(table, row)
	}

	tf := txtfmt.NewTableFormatter(nameTitle, valueTitle)
	tf.InitWriter(out)
	tf.Format(table)
}

func printRebuildManageLines(out io.Writer, inTxt string, items []string, verbose bool) {
	extra := ""
	if verbose {
		extra = fmt.Sprintf(" (%s)", strings.Join(items, ", "))
	}
	fmt.Fprintf(out, "- %-34s %d %s%s\n", inTxt, len(items),
		common.Pluralise("pool", len(items)), extra)
}

func printSystemRebuildStopResp(out io.Writer, resp *control.SystemRebuildManageResp, verbose bool) error {
	// Categorize results: successful (or finishing - DER_BUSY), not-rebuilding (DER_NONEXIST)
	// and errors
	var succeeded, notRebuilding, actualErrors, errMsgs []string
	for _, res := range resp.Results {
		if !res.Errored || strings.Contains(res.Msg, "DER_BUSY") {
			succeeded = append(succeeded, res.ID)
		} else if strings.Contains(res.Msg, "DER_NONEXIST") ||
			strings.Contains(res.Msg, "entity does not exist") {
			notRebuilding = append(notRebuilding, res.ID)
		} else {
			actualErrors = append(actualErrors, res.ID)
			errMsgs = append(errMsgs,
				fmt.Sprintf("pool-rebuild stop failed on pool %s: %s", res.ID,
					res.Msg))
		}
	}

	// Print structured output in a consistent format
	totalPools := len(resp.Results)
	fmt.Fprintf(out, "System-rebuild stop requested for %d %s\n", totalPools,
		common.Pluralise("pool", totalPools))

	if len(succeeded) > 0 {
		printRebuildManageLines(out, "With active or finishing rebuild:", succeeded,
			verbose)
	}

	if len(notRebuilding) > 0 {
		printRebuildManageLines(out, "Without active rebuild:", notRebuilding, verbose)
	}

	// Only return error if there are actual failures (not DER_NONEXIST)
	if len(actualErrors) > 0 {
		printRebuildManageLines(out, "Errors:", actualErrors, verbose)

		return errors.New(strings.Join(errMsgs, ", "))
	}

	fmt.Fprintln(out, "Command completed successfully.")

	return nil
}

func printSystemRebuildStartResp(out io.Writer, resp *control.SystemRebuildManageResp, verbose bool) error {
	// Categorize results: successful and errors
	var succeeded, actualErrors, errMsgs []string
	for _, res := range resp.Results {
		if !res.Errored {
			succeeded = append(succeeded, res.ID)
		} else {
			actualErrors = append(actualErrors, res.ID)
			errMsgs = append(errMsgs,
				fmt.Sprintf("pool-rebuild start failed on pool %s: %s", res.ID,
					res.Msg))
		}
	}

	// Print structured output in a consistent format
	totalPools := len(resp.Results)
	fmt.Fprintf(out, "System-rebuild start requested for %d %s\n", totalPools,
		common.Pluralise("pool", totalPools))

	if len(succeeded) > 0 {
		printRebuildManageLines(out, "Successfully requested:", succeeded, verbose)
	}

	// Only return error if there are actual failures (not DER_NONEXIST)
	if len(actualErrors) > 0 {
		printRebuildManageLines(out, "Errors:", actualErrors, verbose)

		return errors.New(strings.Join(errMsgs, ", "))
	}

	fmt.Fprintln(out, "Command completed successfully.")

	return nil
}

// PrintSystemRebuildManageResp renders a human readable output representing the results of a dmg
// system rebuild manage command response. If <operation>==stop, pools that have no active rebuild
// to stop will be reported in the output but not result in an error being reported.
func PrintSystemRebuildManageResp(out io.Writer, resp *control.SystemRebuildManageResp, verbose bool) error {
	// Handle special case: no pools in system
	if len(resp.Results) == 0 {
		fmt.Fprintln(out, "No pools in system.")
		fmt.Fprintln(out, "Command completed successfully.")

		return nil
	}

	// Determine operation type
	var opCode control.PoolRebuildOpCode
	for idx, res := range resp.Results {
		if idx == 0 {
			opCode = res.OpCode
		} else if opCode != res.OpCode {
			return errors.Errorf("different system rebuild manage opcodes found in "+
				"results: %s and %s", opCode, res.OpCode)
		}
	}

	switch opCode {
	case control.PoolRebuildOpCodeStop:
		return printSystemRebuildStopResp(out, resp, verbose)
	case control.PoolRebuildOpCodeStart:
		return printSystemRebuildStartResp(out, resp, verbose)
	default:
		return errors.Errorf("unrecognized system rebuild opcode: %d", opCode)
	}
}
