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
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/logging"
)

// storageQueryCmd is the struct representing the query storage subcommand
type storageQueryCmd struct {
	NVMe nvmeHealthQueryCmd `command:"nvme-health" alias:"d" description:"Query raw NVMe SPDK device statistics."`
	BS   bsHealthQueryCmd   `command:"blobstore-health" alias:"b" description:"Query internal blobstore health data."`
	Smd  smdQueryCmd        `command:"smd" alias:"s" description:"Query per-server metadata."`
}

// nvmeHealthQueryCmd is the struct representing the "storage query health" subcommand
//
// Command is issued across all connected hosts (calls client.StorageScan and is
// an alias for "storage scan").
type nvmeHealthQueryCmd struct {
	logCmd
	connectedCmd
}

// Execute queries the SPDK NVMe device health stats from all devices on all hosts
// when nvmeHealthQueryCmd activates.
func (h *nvmeHealthQueryCmd) Execute(args []string) error {
	controllers, _ := h.conns.StorageScan()
	h.log.Infof(controllers.StringHealthStats())
	return nil
}

// bsHealthQueryCmd is the struct representing the "storage query bio" subcommand
//
// Command is issued to the management service access point.
type bsHealthQueryCmd struct {
	logCmd
	connectedCmd
	Devuuid string `short:"u" long:"devuuid" description:"Device/Blobstore UUID to query"`
	Tgtid   string `short:"t" long:"tgtid" description:"VOS target ID to query"`
}

// Query the BIO health and error stats of the given device
func bsHealthQuery(log logging.Logger, conns client.Connect, uuid string, tgtid string) {
	if uuid != "" && tgtid != "" {
		log.Infof("Either device UUID OR target ID need to be specified, not both\n")
		return
	} else if uuid == "" && tgtid == "" {
		log.Infof("Device UUID or target ID is required\n")
		return
	}

	req := &mgmtpb.BioHealthReq{DevUuid: uuid, TgtId: tgtid}

	log.Infof("Blobstore Health Data:\n%s\n", conns.BioHealthQuery(req))
}

// Execute is run when bsHealthQueryCmd activates
func (b *bsHealthQueryCmd) Execute(args []string) error {
	bsHealthQuery(b.log, b.conns, b.Devuuid, b.Tgtid)
	return nil
}

// smdQueryCmd is the struct representing the "storage query smd" subcommand
//
// Command is issued to the management service access point.
type smdQueryCmd struct {
	logCmd
	connectedCmd
	Devices bool `short:"d" long:"devices" description:"List all devices/blobstores stored in per-server metadata table."`
	Pools   bool `short:"p" long:"pools" descriptsion:"List all VOS pool targets stored in per-server metadata table."`
}

// Query per-server metadata device table for all connected servers
func smdQuery(log logging.Logger, conns client.Connect, devices bool, pools bool) {
	// default is to print both pools and devices if not specified
	if !pools && !devices {
		pools = true
		devices = true
	}

	if devices {
		req_dev := &mgmtpb.SmdDevReq{}
		log.Infof("SMD Device List:\n%s\n", conns.SmdListDevs(req_dev))
	}

	if pools {
		req_pool := &mgmtpb.SmdPoolReq{}
		log.Infof("SMD Pool List:\n%s\n", conns.SmdListPools(req_pool))
	}
}

// Execute is run when ListSmdDevCmd activates
func (s *smdQueryCmd) Execute(args []string) error {
	smdQuery(s.log, s.conns, s.Devices, s.Pools)
	return nil
}
