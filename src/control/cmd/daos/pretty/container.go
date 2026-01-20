//
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pretty

import (
	"fmt"
	"io"

	"github.com/dustin/go-humanize"

	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
)

// PrintContainerInfo generates a human-readable representation of the supplied
// ContainerInfo struct and writes it to the supplied io.Writer.
func PrintContainerInfo(out io.Writer, ci *daos.ContainerInfo, verbose bool) error {
	rows := []txtfmt.TableRow{
		{"Container UUID": ci.ContainerUUID.String()},
	}
	if ci.ContainerLabel != "" {
		rows = append(rows, txtfmt.TableRow{"Container Label": ci.ContainerLabel})
	}
	rows = append(rows, txtfmt.TableRow{"Container Type": ci.Type.String()})

	if verbose {
		rows = append(rows, []txtfmt.TableRow{
			{"Pool UUID": ci.PoolUUID.String()},
			{"Container redundancy factor": fmt.Sprintf("%d", ci.RedundancyFactor)},
			{"Number of open handles": fmt.Sprintf("%d", ci.NumHandles)},
			{"Latest open time": fmt.Sprintf("%s (%#x)", ci.OpenTime, uint64(ci.OpenTime))},
			{"Latest close/modify time": fmt.Sprintf("%s (%#x)", ci.CloseModifyTime, uint64(ci.CloseModifyTime))},
			{"Number of snapshots": fmt.Sprintf("%d", ci.NumSnapshots)},
		}...)

		if ci.LatestSnapshot != 0 {
			rows = append(rows, txtfmt.TableRow{"Latest Persistent Snapshot": fmt.Sprintf("%#x (%s)", uint64(ci.LatestSnapshot), ci.LatestSnapshot)})
		}
		if ci.ObjectClass != 0 {
			rows = append(rows, txtfmt.TableRow{"Object Class": ci.ObjectClass.String()})
		}
		if ci.DirObjectClass != 0 {
			rows = append(rows, txtfmt.TableRow{"Dir Object Class": ci.DirObjectClass.String()})
		}
		if ci.FileObjectClass != 0 {
			rows = append(rows, txtfmt.TableRow{"File Object Class": ci.FileObjectClass.String()})
		}
		if ci.Hints != "" {
			rows = append(rows, txtfmt.TableRow{"Hints": ci.Hints})
		}
		if ci.ChunkSize > 0 {
			rows = append(rows, txtfmt.TableRow{"Chunk Size": humanize.IBytes(ci.ChunkSize)})
		}
	}
	_, err := fmt.Fprintln(out, txtfmt.FormatEntity("", rows))
	return err
}

// PrintContainers generates a human-readable representation of the supplied
// slice of ContainerInfo structs and writes it to the supplied io.Writer.
func PrintContainers(out io.Writer, poolID string, containers []*daos.ContainerInfo, verbose bool) {
	if len(containers) == 0 {
		fmt.Fprintf(out, "No containers.\n")
		return
	}

	fmt.Fprintf(out, "Containers in pool %s:\n", poolID)

	uuidTitle := "UUID"
	labelTitle := "Label"
	layoutTitle := "Layout"
	titles := []string{labelTitle}
	if verbose {
		titles = append(titles, uuidTitle, layoutTitle)
	}

	table := []txtfmt.TableRow{}
	for _, cont := range containers {
		table = append(table,
			txtfmt.TableRow{
				uuidTitle:   cont.ContainerUUID.String(),
				labelTitle:  cont.ContainerLabel,
				layoutTitle: cont.Type.String(),
			})
	}

	tf := txtfmt.NewTableFormatter(titles...)
	tf.InitWriter(txtfmt.NewIndentWriter(out))
	tf.Format(table)
}

// PrintContainerProperties generates a human-readable representation of the
// supplied slice of container properties and writes it to the supplied io.Writer.
func PrintContainerProperties(out io.Writer, header string, props ...*daos.ContainerProperty) {
	fmt.Fprintf(out, "%s\n", header)

	if len(props) == 0 {
		fmt.Fprintln(out, "  No properties found.")
		return
	}

	nameTitle := "Name"
	valueTitle := "Value"
	titles := []string{nameTitle}

	table := []txtfmt.TableRow{}
	for _, prop := range props {
		row := txtfmt.TableRow{}
		row[nameTitle] = fmt.Sprintf("%s (%s)", prop.Description, prop.Name)
		if prop.StringValue() != "" {
			row[valueTitle] = prop.StringValue()
			if len(titles) == 1 {
				titles = append(titles, valueTitle)
			}
		}
		table = append(table, row)
	}

	tf := txtfmt.NewTableFormatter(titles...)
	tf.InitWriter(out)
	tf.Format(table)
}
