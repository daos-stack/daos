//
// (C) Copyright 2019-2020 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package main

import (
	"bytes"
	"fmt"
	"sort"

	"github.com/dustin/go-humanize"

	"github.com/daos-stack/daos/src/control/common/proto"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
	"github.com/daos-stack/daos/src/control/server/storage"
)

func scmModuleScanTable(ms storage.ScmModules) string {
	buf := &bytes.Buffer{}

	if len(ms) == 0 {
		fmt.Fprint(buf, "\tNo SCM modules found\n")
		return buf.String()
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
	var table []txtfmt.TableRow

	sort.Slice(ms, func(i, j int) bool { return ms[i].PhysicalID < ms[j].PhysicalID })

	for _, m := range ms {
		row := txtfmt.TableRow{physicalIdTitle: fmt.Sprint(m.PhysicalID)}
		row[socketTitle] = fmt.Sprint(m.SocketID)
		row[memCtrlrTitle] = fmt.Sprint(m.ControllerID)
		row[channelTitle] = fmt.Sprint(m.ChannelID)
		row[slotTitle] = fmt.Sprint(m.ChannelPosition)
		row[capacityTitle] = humanize.IBytes(m.Capacity)

		table = append(table, row)
	}

	fmt.Fprint(buf, formatter.Format(table))

	return buf.String()
}

func scmNsScanTable(nss storage.ScmNamespaces) string {
	buf := &bytes.Buffer{}

	if len(nss) == 0 {
		fmt.Fprint(buf, "\tNo SCM namespaces found\n")
		return buf.String()
	}

	deviceTitle := "SCM Namespace"
	socketTitle := "Socket ID"
	capacityTitle := "Capacity"

	formatter := txtfmt.NewTableFormatter(deviceTitle, socketTitle, capacityTitle)
	var table []txtfmt.TableRow

	sort.Slice(nss, func(i, j int) bool { return nss[i].BlockDevice < nss[j].BlockDevice })

	for _, ns := range nss {
		row := txtfmt.TableRow{deviceTitle: ns.BlockDevice}
		row[socketTitle] = fmt.Sprint(ns.NumaNode)
		row[capacityTitle] = humanize.Bytes(ns.Size)

		table = append(table, row)
	}

	fmt.Fprint(buf, formatter.Format(table))

	return buf.String()
}

func scmFormatTable(smr proto.ScmMountResults) string {
	buf := &bytes.Buffer{}

	if len(smr) == 0 {
		fmt.Fprint(buf, "\tNo SCM mount results\n")
		return buf.String()
	}

	mntTitle := "SCM Mount"
	resultTitle := "Format Result"

	formatter := txtfmt.NewTableFormatter(mntTitle, resultTitle)
	var table []txtfmt.TableRow

	sort.Slice(smr, func(i, j int) bool { return smr[i].Mntpoint < smr[j].Mntpoint })

	for _, mnt := range smr {
		row := txtfmt.TableRow{mntTitle: mnt.Mntpoint}

		result := mnt.State.Status.String()
		if mnt.State.Error != "" {
			result = fmt.Sprintf("%s: %s", result, mnt.State.Error)
		}
		if mnt.State.Info != "" {
			result = fmt.Sprintf("%s (%s)", result, mnt.State.Info)
		}

		row[resultTitle] = result

		table = append(table, row)
	}

	fmt.Fprint(buf, formatter.Format(table))

	return buf.String()
}

func nvmeScanTable(ncs proto.NvmeControllers) string {
	buf := &bytes.Buffer{}

	if len(ncs) == 0 {
		fmt.Fprint(buf, "\tNo NVMe devices found\n")
		return buf.String()
	}

	pciTitle := "NVMe PCI"
	modelTitle := "Model"
	fwTitle := "FW Revision"
	socketTitle := "Socket ID"
	capacityTitle := "Capacity"

	formatter := txtfmt.NewTableFormatter(
		pciTitle, modelTitle, fwTitle, socketTitle, capacityTitle,
	)
	var table []txtfmt.TableRow

	sort.Slice(ncs, func(i, j int) bool { return ncs[i].Pciaddr < ncs[j].Pciaddr })

	for _, ctrlr := range ncs {
		row := txtfmt.TableRow{pciTitle: ctrlr.Pciaddr}
		row[modelTitle] = ctrlr.Model
		row[fwTitle] = ctrlr.Fwrev
		row[socketTitle] = fmt.Sprint(ctrlr.Socketid)
		row[capacityTitle] = humanize.Bytes((*proto.NvmeController)(ctrlr).Capacity())

		table = append(table, row)
	}

	fmt.Fprint(buf, formatter.Format(table))

	return buf.String()
}

func nvmeFormatTable(ncr proto.NvmeControllerResults) string {
	buf := &bytes.Buffer{}

	if len(ncr) == 0 {
		fmt.Fprint(buf, "\tNo NVMe devices formatted\n")
		return buf.String()
	}

	pciTitle := "NVMe PCI"
	resultTitle := "Format Result"

	formatter := txtfmt.NewTableFormatter(pciTitle, resultTitle)
	var table []txtfmt.TableRow

	sort.Slice(ncr, func(i, j int) bool { return ncr[i].Pciaddr < ncr[j].Pciaddr })

	for _, ctrlr := range ncr {
		row := txtfmt.TableRow{pciTitle: ctrlr.Pciaddr}

		result := ctrlr.State.Status.String()
		if ctrlr.State.Error != "" {
			result = fmt.Sprintf("%s: %s", result, ctrlr.State.Error)
		}
		if ctrlr.State.Info != "" {
			result = fmt.Sprintf("%s (%s)", result, ctrlr.State.Info)
		}

		row[resultTitle] = result

		table = append(table, row)
	}

	fmt.Fprint(buf, formatter.Format(table))

	return buf.String()
}
