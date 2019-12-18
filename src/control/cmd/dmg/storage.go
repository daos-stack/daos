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

	"github.com/daos-stack/daos/src/control/client"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	types "github.com/daos-stack/daos/src/control/common/storage"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
)

const (
	summarySep = "/"
	successMsg = "storage format ok"
)

// storageCmd is the struct representing the top-level storage subcommand.
type storageCmd struct {
	Prepare   storagePrepareCmd   `command:"prepare" alias:"p" description:"Prepare SCM and NVMe storage attached to remote servers."`
	Scan      storageScanCmd      `command:"scan" alias:"s" description:"Scan SCM and NVMe storage attached to remote servers."`
	Format    storageFormatCmd    `command:"format" alias:"f" description:"Format SCM and NVMe storage attached to remote servers."`
	Query     storageQueryCmd     `command:"query" alias:"q" description:"Query storage commands, including raw NVMe SSD device health stats and internal blobstore health info."`
	SetFaulty storageSetFaultyCmd `command:"setfaulty" alias:"sf" descrption:"Manually set the device state of an NVMe SSD to FAULTY."`
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
	Verbose bool `short:"v" long:"verbose" description:"List SCM & NVMe device details"`
}

// Execute is run when storageScanCmd activates.
//
// Runs NVMe and SCM storage scan on all connected servers.
func (cmd *storageScanCmd) Execute(args []string) error {
	out, err := scanCmdDisplay(cmd.conns.StorageScan(nil), !cmd.Verbose)
	if err != nil {
		return err
	}
	cmd.log.Info(out)

	return nil
}

// scanCmdDisplay returns tabulated output of grouped host summaries or groups
// matched on all tabulatd device details per host.
func scanCmdDisplay(result *client.StorageScanResp, summary bool) (string, error) {
	out := &bytes.Buffer{}

	groups, err := groupScanResults(result, summary)
	if err != nil {
		return "", err
	}

	if summary {
		if len(groups) == 0 {
			return "no hosts found", nil
		}
		return storageSummaryTable("Hosts", "SCM Total", "NVMe Total", groups)
	}

	formatHostGroupResults(out, groups)

	return out.String(), nil
}

// groupScanResults collects identical output keyed on hostset from
// scan results. SCM namespaces will be displayed instead of modules if they
// exist. SCM & NVMe device details will be tabulated and grouping will match
// all tabulated output for a given host. Summary will provide storage capacity
// totals and number of device.
func groupScanResults(result *client.StorageScanResp, summary bool) (groups hostlist.HostGroups, err error) {
	var host string
	buf := &bytes.Buffer{}
	groups = make(hostlist.HostGroups)

	for _, srv := range result.Servers {
		buf.Reset()

		host, _, err = splitPort(srv, 0) // disregard port when grouping output
		if err != nil {
			return
		}

		if summary {
			fmt.Fprintf(buf, "%s%s%s", result.Scm[srv].Summary(),
				summarySep, result.Nvme[srv].Summary())
			if err = groups.AddHost(buf.String(), host); err != nil {
				return
			}
			continue
		}

		sres := result.Scm[srv]
		switch {
		case sres.Err != nil:
			fmt.Fprintf(buf, "SCM Error: %s\n", sres.Err)
		case len(sres.Namespaces) > 0:
			fmt.Fprintf(buf, "%s\n", scmNsScanTable(sres.Namespaces))
		default:
			fmt.Fprintf(buf, "%s\n", scmModuleScanTable(sres.Modules))
		}

		if result.Nvme[srv].Err != nil {
			fmt.Fprintf(buf, "NVMe Error: %s\n", result.Nvme[srv].Err)
		} else {
			fmt.Fprintf(buf, "%s", nvmeScanTable(result.Nvme[srv].Ctrlrs))
		}

		if err = groups.AddHost(buf.String(), host); err != nil {
			return
		}
	}

	return
}

// storageFormatCmd is the struct representing the format storage subcommand.
type storageFormatCmd struct {
	logCmd
	connectedCmd
	Verbose  bool `short:"v" long:"verbose" description:"Show results of each SCM & NVMe device format operation"`
	Reformat bool `long:"reformat" description:"Always reformat storage (CAUTION: Potentially destructive)"`
}

// Execute is run when storageFormatCmd activates
//
// run NVMe and SCM storage format on all connected servers
func (cmd *storageFormatCmd) Execute(args []string) error {
	out, err := formatCmdDisplay(cmd.conns.StorageFormat(cmd.Reformat), !cmd.Verbose)
	if err != nil {
		return err
	}
	cmd.log.Info(out)

	return nil
}

// storageSetFaultyCmd is the struct representing the set-faulty storage subcommand
type storageSetFaultyCmd struct {
	logCmd
	connectedCmd
	Devuuid string `short:"u" long:"devuuid" description:"Device/Blobstore UUID to query" required:"1"`
}

// Execute is run when storageSetFaultyCmd activates
// Set the SMD device state of the given device to "FAULTY"
func (s *storageSetFaultyCmd) Execute(args []string) error {
	// Devuuid is a required command parameter
	req := &mgmtpb.DevStateReq{DevUuid: s.Devuuid}

	s.log.Infof("Device State Info:\n%s\n", s.conns.StorageSetFaulty(req))

	return nil
}

// formatCmdDisplay returns tabulated output of grouped host summaries or groups
// matched on all tabulatd device format results per host.
func formatCmdDisplay(results client.StorageFormatResults, summary bool) (string, error) {
	out := &bytes.Buffer{}

	groups, mixedGroups, err := groupFormatResults(results, summary)
	if err != nil {
		return "", err
	}

	if len(groups) > 0 {
		fmt.Fprintf(out, "\n%s\n", groups)
	}

	return formatHostGroupResults(out, mixedGroups), nil
}

// groupFormatResults collects identical output keyed on hostset from
// format results and returns separate groups for host (as opposed to
// storage subsystem) level errors. Summary will provide success/failure only.
func groupFormatResults(results client.StorageFormatResults, summary bool) (groups, mixedGroups hostlist.HostGroups, err error) {
	var host string
	buf := &bytes.Buffer{}
	groups = make(hostlist.HostGroups)      // result either complete success or failure
	mixedGroups = make(hostlist.HostGroups) // result mix of device success/failure

	for _, srv := range results.Keys() {
		buf.Reset()
		result := results[srv]

		host, _, err = splitPort(srv, 0) // disregard port when grouping output
		if err != nil {
			return
		}

		hostErr := result.Err
		if hostErr != nil {
			if err = groups.AddHost(hostErr.Error(), host); err != nil {
				return
			}
			continue
		}

		if summary && !result.HasErrors() {
			if err = groups.AddHost(successMsg, host); err != nil {
				return
			}
			continue
		}

		fmt.Fprintf(buf, "%s\n", scmFormatTable(result.Scm))
		fmt.Fprintf(buf, "%s\n", nvmeFormatTable(result.Nvme))

		if err = mixedGroups.AddHost(buf.String(), host); err != nil {
			return
		}
	}

	return
}
