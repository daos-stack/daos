//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pretty

import (
	"fmt"
	"io"

	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
)

// PrintCheckerPolicies displays a two-column table of checker policy classes and actions.
func PrintCheckerPolicies(out io.Writer, flags control.SystemCheckFlags, policies ...*control.SystemCheckPolicy) {
	fmt.Fprintf(out, "Checker flags: %s\n\n", flags)

	nameTitle := "Inconsistency Class"
	valueTitle := "Repair Action"
	table := []txtfmt.TableRow{}
	for _, policy := range policies {
		if policy == nil {
			continue
		}
		row := txtfmt.TableRow{}
		row[nameTitle] = policy.FindingClass.String()
		row[valueTitle] = policy.RepairAction.String()
		table = append(table, row)
	}

	tf := txtfmt.NewTableFormatter(nameTitle, valueTitle)
	tf.InitWriter(out)
	tf.Format(table)
}
