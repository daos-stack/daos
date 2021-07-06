//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pretty

import (
	"fmt"
	"io"
	"sort"

	"github.com/dustin/go-humanize"

	"github.com/daos-stack/daos/src/control/lib/txtfmt"
	"github.com/daos-stack/daos/src/control/server/storage"
)

func printScmMountPoints(mountpoints storage.ScmMountPoints, out io.Writer, opts ...PrintConfigOption) error {
	if len(mountpoints) == 0 {
		fmt.Fprintln(out, "\tNo SCM mount results")
		return nil
	}

	mntTitle := "SCM Mount"
	resultTitle := "Format Result"

	formatter := txtfmt.NewTableFormatter(mntTitle, resultTitle)
	formatter.InitWriter(out)
	var table []txtfmt.TableRow

	sort.Slice(mountpoints, func(i, j int) bool { return mountpoints[i].Path < mountpoints[j].Path })

	for _, mountpoint := range mountpoints {
		row := txtfmt.TableRow{mntTitle: mountpoint.Path}
		row[resultTitle] = mountpoint.Info

		table = append(table, row)
	}

	formatter.Format(table)
	return nil
}

// PrintScmModules displays PMM details in a verbose table.
//
// TODO: un-export function when not needed in cmd/daos_server/storage.go
func PrintScmModules(modules storage.ScmModules, out io.Writer, opts ...PrintConfigOption) error {
	w := txtfmt.NewErrWriter(out)

	if len(modules) == 0 {
		fmt.Fprintln(out, "\tNo SCM modules found")
		return w.Err
	}

	physicalIdTitle := "SCM Module ID"
	socketTitle := "Socket ID"
	memCtrlrTitle := "Memory Ctrlr ID"
	channelTitle := "Channel ID"
	slotTitle := "Channel Slot"
	capacityTitle := "Capacity"

	formatter := txtfmt.NewTableFormatter(
		physicalIdTitle, socketTitle, memCtrlrTitle, channelTitle, slotTitle, capacityTitle,
	)
	formatter.InitWriter(out)
	var table []txtfmt.TableRow

	sort.Slice(modules, func(i, j int) bool { return modules[i].PhysicalID < modules[j].PhysicalID })

	for _, m := range modules {
		row := txtfmt.TableRow{physicalIdTitle: fmt.Sprint(m.PhysicalID)}
		row[socketTitle] = fmt.Sprint(m.SocketID)
		row[memCtrlrTitle] = fmt.Sprint(m.ControllerID)
		row[channelTitle] = fmt.Sprint(m.ChannelID)
		row[slotTitle] = fmt.Sprint(m.ChannelPosition)
		row[capacityTitle] = humanize.IBytes(m.Capacity)

		table = append(table, row)
	}

	formatter.Format(table)
	return w.Err
}

// PrintScmNamespaces displays pmem block device details in a verbose table.
//
// TODO: un-export function when not needed in cmd/daos_server/storage.go
func PrintScmNamespaces(namespaces storage.ScmNamespaces, out io.Writer, opts ...PrintConfigOption) error {
	w := txtfmt.NewErrWriter(out)

	if len(namespaces) == 0 {
		fmt.Fprintln(out, "\tNo SCM namespaces found")
		return w.Err
	}

	deviceTitle := "SCM Namespace"
	socketTitle := "Socket ID"
	capacityTitle := "Capacity"

	formatter := txtfmt.NewTableFormatter(deviceTitle, socketTitle, capacityTitle)
	formatter.InitWriter(out)
	var table []txtfmt.TableRow

	sort.Slice(namespaces, func(i, j int) bool { return namespaces[i].BlockDevice < namespaces[j].BlockDevice })

	for _, ns := range namespaces {
		row := txtfmt.TableRow{deviceTitle: ns.BlockDevice}
		row[socketTitle] = fmt.Sprint(ns.NumaNode)
		row[capacityTitle] = humanize.Bytes(ns.Size)

		table = append(table, row)
	}

	formatter.Format(table)
	return w.Err
}
