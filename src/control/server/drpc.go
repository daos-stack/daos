//
// (C) Copyright 2018 Intel Corporation.
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
	"log"
	"os"
	"path/filepath"

	"github.com/daos-stack/daos/src/control/drpc"
)

// drpcSetup creates socket directory, specifies socket path and then
// starts drpc server.
func drpcSetup(sockDir string) {
	// Create our socket directory if it doesn't exist
	_, err := os.Stat(sockDir)
	if err != nil && os.IsPermission(err) {
		log.Fatalf("User does not have permission to access %s", sockDir)
	} else if err != nil && os.IsNotExist(err) {
		err = os.MkdirAll(sockDir, 0755)
		if err != nil {
			log.Fatalf(
				"Unable to create socket directory %s: %v", sockDir, err)
		}
	}

	sockPath := filepath.Join(sockDir, "daos_server.sock")
	drpcServer, err := drpc.NewDomainSocketServer(sockPath)
	if err != nil {
		log.Fatalf("Unable to create socket server: %v", err)
	}

	err = drpcServer.Start()
	if err != nil {
		log.Fatalf("Unable to start socket server on %s: %v", sockPath, err)
	}
}
