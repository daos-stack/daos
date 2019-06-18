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
	"os"
)

// SvcCmd is the struct representing the top-level service subcommand.
type SvcCmd struct {
	KillRank KillRankSvcCmd `command:"kill-rank" alias:"kr" description:"Terminate server running as specific rank on a DAOS pool"`
}

// KillRankSvcCmd is the struct representing the command to kill server
// identified by rank on given pool identified by uuid.
type KillRankSvcCmd struct {
	Rank     uint32 `short:"r" long:"rank" description:"Rank identifying DAOS server"`
	PoolUUID string `short:"p" long:"pool-uuid" description:"Pool uuid that rank relates to"`
}

// run kill rank command with specified parameters on all connected servers
func killRankSvc(uuid string, rank uint32) {
	fmt.Printf("Kill Rank command results:\n%s", conns.KillRank(uuid, rank))
}

// Execute is run when KillRankSvcCmd activates
func (k *KillRankSvcCmd) Execute(args []string) error {
	// broadcast == false to connect to mgmt svc access point
	if err := appSetup(false); err != nil {
		return err
	}

	killRankSvc(k.PoolUUID, k.Rank)

	// exit immediately to avoid continuation of main
	os.Exit(0)
	// never reached
	return nil
}
