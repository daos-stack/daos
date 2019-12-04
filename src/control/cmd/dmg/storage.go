//
// (C) Copyright 2019 Intel Corporation.
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
	"strings"

	bytesize "github.com/inhies/go-bytesize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/client"
	"github.com/daos-stack/daos/src/control/common/proto"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	types "github.com/daos-stack/daos/src/control/common/storage"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/daos-stack/daos/src/control/server/storage"
)

const summarySep = "/"

// storageCmd is the struct representing the top-level storage subcommand.
type storageCmd struct {
	Prepare storagePrepareCmd `command:"prepare" alias:"p" description:"Prepare SCM and NVMe storage attached to remote servers."`
	Scan    storageScanCmd    `command:"scan" alias:"s" description:"Scan SCM and NVMe storage attached to remote servers."`
	Format  storageFormatCmd  `command:"format" alias:"f" description:"Format SCM and NVMe storage attached to remote servers."`
	Query   storageQueryCmd   `command:"query" alias:"q" description:"Query storage commands, including raw NVMe SSD device health stats and internal blobstore health info."`
}

// storagePrepareCmd is the struct representing the prep storage subcommand.
type storagePrepareCmd struct {
	logCmd
	connectedCmd
	types.StoragePrepareCmd
}

// Execute is run when storagePrepareCmd activates
func (cmd *storagePrepareCmd) Execute(args []string) error {
	var nReq *ctlpb.PrepareNvmeReq
	var sReq *ctlpb.PrepareScmReq

	prepNvme, prepScm, err := cmd.Validate()
	if err != nil {
		return err
	}

	if prepNvme {
		nReq = &ctlpb.PrepareNvmeReq{
			Pciwhitelist: cmd.PCIWhiteList,
			Nrhugepages:  int32(cmd.NrHugepages),
			Targetuser:   cmd.TargetUser,
			Reset_:       cmd.Reset,
		}
	}

	if prepScm {
		if err := cmd.Warn(cmd.log); err != nil {
			return err
		}

		sReq = &ctlpb.PrepareScmReq{Reset_: cmd.Reset}
	}

	cmd.log.Infof("NVMe & SCM preparation:\n%s",
		cmd.conns.StoragePrepare(&ctlpb.StoragePrepareReq{Nvme: nReq, Scm: sReq}))

	return nil
}

// storageScanCmd is the struct representing the scan storage subcommand.
type storageScanCmd struct {
	logCmd
	connectedCmd
	Summary bool `short:"m" long:"summary" description:"List total capacity and number of devices only"`
}

func scmModuleTable(ms storage.ScmModules) string {
	buf := &bytes.Buffer{}

	if len(ms) == 0 {
		fmt.Fprint(buf, "\tnone\n")
		return buf.String()
	}

	physicalIdTitle := "SCM Module ID"
	socketTitle := "Socket ID"
	memCtrlrTitle := "Memory Ctrlr ID"
	channelTitle := "Channel ID"
	slotTitle := "Channel Slot"
	capacityTitle := "Capacity"

	formatter := NewTableFormatter([]string{
		physicalIdTitle, socketTitle, memCtrlrTitle, channelTitle, slotTitle, capacityTitle,
	})
	var table []TableRow

	sort.Slice(ms, func(i, j int) bool { return ms[i].PhysicalID < ms[j].PhysicalID })

	for _, m := range ms {
		row := TableRow{physicalIdTitle: fmt.Sprint(m.PhysicalID)}
		row[socketTitle] = fmt.Sprint(m.SocketID)
		row[memCtrlrTitle] = fmt.Sprint(m.ControllerID)
		row[channelTitle] = fmt.Sprint(m.ChannelID)
		row[slotTitle] = fmt.Sprint(m.ChannelPosition)
		row[capacityTitle] = bytesize.New(float64(m.Capacity)).String()

		table = append(table, row)
	}

	fmt.Fprint(buf, formatter.Format(table))

	return buf.String()
}

func scmNsTable(nss storage.ScmNamespaces) string {
	buf := &bytes.Buffer{}

	if len(nss) == 0 {
		fmt.Fprint(buf, "\tnone\n")
		return buf.String()
	}

	deviceTitle := "SCM Namespace"
	socketTitle := "Socket ID"
	capacityTitle := "Capacity"

	formatter := NewTableFormatter([]string{deviceTitle, socketTitle, capacityTitle})
	var table []TableRow

	sort.Slice(nss, func(i, j int) bool { return nss[i].BlockDevice < nss[j].BlockDevice })

	for _, ns := range nss {
		row := TableRow{deviceTitle: ns.BlockDevice}
		row[socketTitle] = fmt.Sprint(ns.NumaNode)
		row[capacityTitle] = bytesize.New(float64(ns.Size)).String()

		table = append(table, row)
	}

	fmt.Fprint(buf, formatter.Format(table))

	return buf.String()
}

func nvmeTable(ncs proto.NvmeControllers) string {
	buf := &bytes.Buffer{}

	if len(ncs) == 0 {
		fmt.Fprint(buf, "\tnone\n")
		return buf.String()
	}

	pciTitle := "NVMe PCI"
	modelTitle := "Model"
	fwTitle := "FW Revision"
	socketTitle := "Socket ID"
	capacityTitle := "Capacity"

	formatter := NewTableFormatter([]string{
		pciTitle, modelTitle, fwTitle, socketTitle, capacityTitle,
	})
	var table []TableRow

	sort.Slice(ncs, func(i, j int) bool { return ncs[i].Pciaddr < ncs[j].Pciaddr })

	for _, ctrlr := range ncs {
		tCap := bytesize.New(0)
		for _, ns := range ctrlr.Namespaces {
			tCap += bytesize.GB * bytesize.New(float64(ns.Size))
		}

		row := TableRow{pciTitle: ctrlr.Pciaddr}
		row[modelTitle] = ctrlr.Model
		row[fwTitle] = ctrlr.Fwrev
		row[socketTitle] = fmt.Sprint(ctrlr.Socketid)
		row[capacityTitle] = tCap.String()

		table = append(table, row)
	}

	fmt.Fprint(buf, formatter.Format(table))

	return buf.String()
}

func groupScanResults(result *client.StorageScanResp, summary bool) (hostlist.HostGroups, error) {
	groups := make(hostlist.HostGroups)

	// group identical output identified by hostset
	for _, srv := range result.Servers {
		buf := &bytes.Buffer{}

		if summary {
			fmt.Fprintf(buf, "%s%s%s", result.Scm[srv].Summary(),
				summarySep, result.Nvme[srv].Summary())
		} else {
			sres := result.Scm[srv]
			switch {
			case sres.Err != nil:
				fmt.Fprintf(buf, "SCM Error: %s\n", sres.Err)
			case len(sres.Namespaces) > 0:
				fmt.Fprintf(buf, "%s\n", scmNsTable(sres.Namespaces))
			default:
				fmt.Fprintf(buf, "%s\n", scmModuleTable(sres.Modules))
			}

			if result.Nvme[srv].Err != nil {
				fmt.Fprintf(buf, "NVMe Error: %s\n", result.Nvme[srv].Err)
			} else {
				fmt.Fprintf(buf, "%s", nvmeTable(result.Nvme[srv].Ctrlrs))
			}
		}

		// disregard connection port when grouping scan output
		srvHost, _, err := splitPort(srv, 0)
		if err != nil {
			return nil, err
		}
		if err := groups.AddHost(buf.String(), srvHost); err != nil {
			return nil, err
		}
	}

	return groups, nil
}

func scanCmdDisplay(result *client.StorageScanResp) (string, error) {
	out := &bytes.Buffer{}
	groups, err := groupScanResults(result, false)
	if err != nil {
		return "", err
	}

	for _, res := range groups.Keys() {
		hostset := groups[res].RangedString()
		lineBreak := strings.Repeat("-", len(hostset))
		fmt.Fprintf(out, "%s\n%s\n%s\n%s", lineBreak, hostset, lineBreak, res)
	}

	return out.String(), nil
}

func scanCmdDisplaySummary(result *client.StorageScanResp) (string, error) {
	groups, err := groupScanResults(result, true)
	if err != nil {
		return "", err
	}

	hostsetTitle := "Hosts"
	scmTitle := "SCM Total"
	nvmeTitle := "NVMe Total"

	formatter := NewTableFormatter([]string{hostsetTitle, scmTitle, nvmeTitle})
	var table []TableRow

	for _, result := range groups.Keys() {
		row := TableRow{hostsetTitle: groups[result].RangedString()}

		summary := strings.Split(result, summarySep)
		if len(summary) != 2 {
			return "", errors.New("unexpected summary format")
		}
		row[scmTitle] = summary[0]
		row[nvmeTitle] = summary[1]

		table = append(table, row)
	}

	return formatter.Format(table), nil
}

// Execute is run when storageScanCmd activates.
// Runs NVMe and SCM storage scan on all connected servers.
func (cmd *storageScanCmd) Execute(args []string) (err error) {
	var out string

	if cmd.Summary {
		out, err = scanCmdDisplaySummary(cmd.conns.StorageScan(nil))
	} else {
		out, err = scanCmdDisplay(cmd.conns.StorageScan(nil))
	}

	if err != nil {
		return err
	}
	cmd.log.Info(out)

	return nil
}

// storageFormatCmd is the struct representing the format storage subcommand.
type storageFormatCmd struct {
	logCmd
	connectedCmd
	Reformat bool `long:"reformat" description:"Always reformat storage (CAUTION: Potentially destructive)"`
}

// Execute is run when storageFormatCmd activates
// run NVMe and SCM storage format on all connected servers
func (cmd *storageFormatCmd) Execute(args []string) error {
	cmd.log.Infof("Format Results: %v", cmd.conns.StorageFormat(cmd.Reformat))

	return nil
}
