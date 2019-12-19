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
	"fmt"
	"testing"
)

func TestStorageQueryCommands(t *testing.T) {
	runCmdTests(t, []cmdTest{
		{
			"NVMe health query",
			"storage query nvme-health",
			"ConnectClients StorageScan-<nil>",
			nil,
		},
		{
			"blobstore health query none specified",
			"storage query blobstore-health",
			"ConnectClients BioHealthQuery",
			fmt.Errorf("device UUID or target ID is required"),
		},
		{
			"blobstore health query both specified",
			"storage query blobstore-health --tgtid 123 --devuuid abc",
			"ConnectClients BioHealthQuery",
			fmt.Errorf("either device UUID OR target ID need to be specified not both"),
		},
		{
			"blobstore health query tgtid",
			"storage query blobstore-health --tgtid 123",
			"ConnectClients BioHealthQuery-tgt_id:\"123\" ",
			nil,
		},
		{
			"blobstore health query devuuid",
			"storage query blobstore-health --devuuid abcd",
			"ConnectClients BioHealthQuery-dev_uuid:\"abcd\" ",
			nil,
		},
		{
			"per-server metadata query pools",
			"storage query smd --pools",
			"ConnectClients SmdListPools-",
			nil,
		},
		{
			"per-server metadata query devices",
			"storage query smd --devices",
			"ConnectClients SmdListDevs-",
			nil,
		},
		{
			"per-server metadata query not specified",
			"storage query smd",
			"ConnectClients SmdListDevs- SmdListPools-",
			nil,
		},
		{
			"per-server metadata query both specified",
			"storage query smd --pools --devices",
			"ConnectClients SmdListDevs- SmdListPools-",
			nil,
		},
		{
			"device state query",
			"storage query device-state --devuuid abcd",
			"ConnectClients DevStateQuery-dev_uuid:\"abcd\" ",
			nil,
		},
		{
			"device state query no device uuid specified",
			"storage query device-state",
			"ConnectClients DevStateQuery",
			fmt.Errorf("the required flag `-u, --devuuid' was not specified"),
		},
		{
			"Nonexistent subcommand",
			"storage query quack",
			"",
			fmt.Errorf("Unknown command"),
		},
	})
}
