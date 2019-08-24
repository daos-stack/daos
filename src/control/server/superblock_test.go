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

package server

import (
	"bytes"
	"io/ioutil"
	"os"
	"path"
	"strconv"
	"testing"

	. "github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/ioserver"
)

// Quick test to demonstrate writing/reading superblock in a temp dir
// in order to avoid mocking OS calls
func TestSuperblock(t *testing.T) {
	testDir, err := ioutil.TempDir("", "TestSuperblock-*")
	defer os.RemoveAll(testDir)
	if err != nil {
		t.Fatal(err)
	}
	storagePath := "/mnt/daos-test"
	if err := os.MkdirAll(path.Join(testDir, storagePath), 0755); err != nil {
		t.Fatal(err)
	}

	var logBuf bytes.Buffer
	log := logging.NewCombinedLogger(t.Name(), &logBuf)

	var rank uint32 = 42
	systemName := "testSystem"
	ti := &IOServerInstance{
		ext: &mockExt{},
		log: log,
		runner: &ioserver.Runner{
			Config: ioserver.NewConfig().
				WithScmClass("ram").
				WithScmMountPoint(storagePath).
				WithSystemName(systemName).
				WithRank(ioserver.NewRankPtr(rank)),
		},
		fsRoot: testDir,
	}
	if err := ti.CreateSuperblock(&mgmtInfo{}); err != nil {
		t.Fatal(err)
	}

	if err := ti.ReadSuperblock(); err != nil {
		t.Fatal(err)
	}

	AssertEqual(t, ti.superblock.Rank.String(), strconv.FormatUint(uint64(rank), 10), "superblock rank")
	AssertEqual(t, ti.superblock.System, systemName, "superblock system")
}
