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
	"github.com/daos-stack/daos/src/control/client"
	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	log "github.com/daos-stack/daos/src/control/logging"
)

// StorageQueryCmd is the struct representing the query storage subcommand
type StorageQueryCmd struct {
	NVMe   NvmeHealthQueryCmd `command:"nvme-health" alias:"d" description:"Query raw NVMe SPDK device statistics."`
	BS     BSHealthQueryCmd   `command:"blobstore-health" alias:"b" description:"Query internal blobstore health data."`
	Smd    SmdQueryCmd        `command:"smd" alias:"s" description:"Query per-server metadata."`
}

// NvmeHealthQueryCmd is the struct representing the "storage query health" subcommand
type NvmeHealthQueryCmd struct {
	broadcastCmd
	connectedCmd
}

// Query the SPDK NVMe device health stats from all devices on all hosts
func nvmeHealthQuery(conns client.Connect) {
	cCtrlrs, _ := conns.StorageScan()
	log.Infof("NVMe SSD Device Health Stats:\n%s", cCtrlrs)
}

// Execute is run when NvmeHealthQueryCmd activates
func (h *NvmeHealthQueryCmd) Execute(args []string) error {
	nvmeHealthQuery(h.conns)
	return nil
}

// BSHealthQueryCmd is the struct representing the "storage query bio" subcommand
type BSHealthQueryCmd struct {
	connectedCmd
	Devuuid	string `short:"u" long:"devuuid" description:"Device/Blobstore UUID to query"`
	Tgtid	string `short:"t" long:"tgtid" description:"VOS target ID to query"`
}

// Query the BIO health and error stats of the given device
func bsHealthQuery(conns client.Connect, uuid string, tgtid string) {
	if uuid != "" && tgtid != "" {
		log.Infof("Either device UUID OR target ID need to be specified, not both\n")
		return
	} else if uuid == "" && tgtid == "" {
		log.Infof("Device UUID or target ID is required\n")
		return
	}

	req := &pb.BioHealthReq{DevUuid: uuid, TgtId: tgtid}

	log.Infof("Blobstore Health Data:\n%s\n", conns.BioHealthQuery(req))
}

// Execute is run when BSHealthQueryCmd activates
func (b *BSHealthQueryCmd) Execute(args []string) error {
	bsHealthQuery(b.conns, b.Devuuid, b.Tgtid)
	return nil
}

// SmdQueryCmd is the struct representing the "storage query smd" subcommand
type SmdQueryCmd struct {
	connectedCmd
	Devices bool `short:"d" long:"devices" description:"List all devices/blobstores stored in per-server metadata table."`
	Pools   bool `short:"p" long:"pools" descriptsion:"List all VOS pool targets stored in per-server metadata table."`
}

// Query per-server metadata device table for all connected servers
func smdQuery(conns client.Connect, devices bool, pools bool) {
	// default is to print both pools and devices if not specified
	if !pools && !devices {
		pools = true
		devices = true
	}
	if pools {
		// TODO implement query pool table
		log.Infof("SMD Pool List:\n")
		log.Infof("--pools option not implemented yet\n")
	}
	if devices {
		req := &pb.SmdDevReq{}
		log.Infof("SMD Device List:\n%s\n", conns.SmdListDevs(req))
	}
}

// Execute is run when ListSmdDevCmd activates
func (s *SmdQueryCmd) Execute(args []string) error {
	smdQuery(s.conns, s.Devices, s.Pools)
	return nil
}
