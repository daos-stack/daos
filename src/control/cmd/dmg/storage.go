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
	"text/tabwriter"

	"github.com/daos-stack/daos/src/control/client"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	types "github.com/daos-stack/daos/src/control/common/storage"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
)

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

func scanCmdDisplay(result *client.StorageScanResp, summary bool) (string, error) {
	var out bytes.Buffer
	groups := make(hostlist.HostGroups)

	// group identical output identified by hostlist
	for _, srv := range result.Servers {
		var buf bytes.Buffer

		if summary {
			fmt.Fprintf(&buf, "%s\t%s\t",
				result.Scm[srv].Summary(), result.Nvme[srv].Summary())
		} else {
			fmt.Fprintf(&buf, "\t%s", result.Scm[srv].String())
			fmt.Fprintf(&buf, "\t%s", result.Nvme[srv].String())
		}

		// disregard connection port when grouping scan output
		srvHost, _, err := splitPort(srv, 0)
		if err != nil {
			return "", err
		}
		if err := groups.AddHost(buf.String(), srvHost); err != nil {
			return "", err
		}
	}

	if summary {
		w := new(tabwriter.Writer)

		// minwidth, tabwidth, padding, padchar, flags
		w.Init(&out, 8, 8, 0, '\t', 0)

		fmt.Fprintf(w, "\n %s\t%s\t%s\t", "HOSTS", "SCM", "NVME")
		fmt.Fprintf(w, "\n %s\t%s\t%s\t", "-----", "---", "----")

		for _, res := range groups.Keys() {
			fmt.Fprintf(w, "\n %s\t%s", groups[res].RangedString(), res)
		}

		w.Flush()
	} else {
		for _, res := range groups.Keys() {
			fmt.Fprintf(&out, "\n %s\n%s", groups[res].RangedString(), res)
		}
	}

	return out.String(), nil
}

// Execute is run when storageScanCmd activates.
// Runs NVMe and SCM storage scan on all connected servers.
func (cmd *storageScanCmd) Execute(args []string) error {
	out, err := scanCmdDisplay(cmd.conns.StorageScan(nil), cmd.Summary)
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
func (s *storageFormatCmd) Execute(args []string) error {
	s.log.Info(
		"This is a destructive operation and storage devices " +
			"specified in the server config file will be erased.\n" +
			"Please be patient as it may take several minutes.\n")

	cCtrlrResults, cMountResults := s.conns.StorageFormat(s.Reformat)
	s.log.Infof("NVMe storage format results:\n%s", cCtrlrResults)
	s.log.Infof("SCM storage format results:\n%s", cMountResults)
	return nil
}
