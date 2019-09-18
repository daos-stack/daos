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
	"context"
	"io/ioutil"
	"net"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/ioserver"
	"github.com/daos-stack/daos/src/control/server/storage"
)

func TestHarnessCreateSuperblocks(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)()

	testDir, err := ioutil.TempDir("", strings.Replace(t.Name(), "/", "-", -1))
	defer os.RemoveAll(testDir)
	if err != nil {
		t.Fatal(err)
	}

	ext := &mockExt{
		isMountPointRet: true,
	}
	h := NewIOServerHarness(ext, log)
	sp := storage.DefaultMockScmProvider(log, storage.NewMockScmExt(
		true, nil, nil, nil, nil, nil,
	))
	for idx, mnt := range []string{"one", "two"} {
		if err := os.MkdirAll(filepath.Join(testDir, mnt), 0777); err != nil {
			t.Fatal(err)
		}
		cfg := ioserver.NewConfig().
			WithRank(uint32(idx)).
			WithSystemName(t.Name()).
			WithScmClass("ram").
			WithScmMountPoint(mnt)
		r := ioserver.NewRunner(log, cfg)
		m := newMgmtSvcClient(
			context.Background(), log, mgmtSvcClientCfg{
				ControlAddr: &net.TCPAddr{},
			},
		)
		i := NewIOServerInstance(log, nil, sp, m, r)
		i.fsRoot = testDir
		if err := h.AddInstance(i); err != nil {
			t.Fatal(err)
		}
	}

	if err := h.CreateSuperblocks(false); err != nil {
		t.Fatal(err)
	}

	mi, err := h.GetManagementInstance()
	if err != nil {
		t.Fatal(err)
	}
	if mi._superblock == nil {
		t.Fatal("instance superblock is nil after CreateSuperblocks()")
	}
	if mi._superblock.System != t.Name() {
		t.Fatalf("expected superblock system name to be %q, got %q", t.Name(), mi._superblock.System)
	}

	for idx, i := range h.Instances() {
		if uint32(*i._superblock.Rank) != uint32(idx) {
			t.Fatalf("instance %d has rank %s (not %d)", idx, i._superblock.Rank, idx)
		}
		if i == mi {
			continue
		}
		if i._superblock.UUID == mi._superblock.UUID {
			t.Fatal("second instance has same superblock as first")
		}
	}
}
