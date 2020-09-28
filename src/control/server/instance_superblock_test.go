//
// (C) Copyright 2020 Intel Corporation.
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
	"net"
	"os"
	"path/filepath"
	"testing"

	. "github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/ioserver"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
	"golang.org/x/net/context"
)

func TestServer_Instance_createSuperblock(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer ShowBufferOnFailure(t, buf)

	testDir, cleanup := CreateTestDir(t)
	defer cleanup()

	defaultApList := []string{"1.2.3.4:5"}
	ctrlAddrs := []string{"1.2.3.4:5", "6.7.8.9:10"}
	h := NewIOServerHarness(log)
	for idx, mnt := range []string{"one", "two"} {
		if err := os.MkdirAll(filepath.Join(testDir, mnt), 0777); err != nil {
			t.Fatal(err)
		}
		cfg := ioserver.NewConfig().
			WithRank(uint32(idx)).
			WithSystemName(t.Name()).
			WithScmClass("ram").
			WithScmRamdiskSize(1).
			WithScmMountPoint(mnt)
		r := ioserver.NewRunner(log, cfg)
		ctrlAddr, err := net.ResolveTCPAddr("tcp", ctrlAddrs[idx])
		if err != nil {
			t.Fatal(err)
		}
		ms := newMgmtSvcClient(
			context.Background(), log, mgmtSvcClientCfg{
				ControlAddr:  ctrlAddr,
				AccessPoints: defaultApList,
			},
		)
		msc := &scm.MockSysConfig{
			IsMountedBool: true,
		}
		mp := scm.NewMockProvider(log, nil, msc)
		srv := NewIOServerInstance(log, nil, mp, ms, r)
		srv.fsRoot = testDir
		if err := h.AddInstance(srv); err != nil {
			t.Fatal(err)
		}
	}

	// ugh, this isn't ideal
	oldGetAddrFn := getInterfaceAddrs
	defer func() {
		getInterfaceAddrs = oldGetAddrFn
	}()
	getInterfaceAddrs = func() ([]net.Addr, error) {
		addrs := make([]net.Addr, len(ctrlAddrs))
		var err error
		for i, ca := range ctrlAddrs {
			addrs[i], err = net.ResolveTCPAddr("tcp", ca)
			if err != nil {
				return nil, err
			}
		}
		return addrs, nil
	}

	for _, instance := range h.Instances() {
		if err := instance.createSuperblock(false); err != nil {
			t.Fatal(err)
		}
	}

	h.started.SetTrue()
	mi, err := h.getMSLeaderInstance()
	if err != nil {
		t.Fatal(err)
	}
	if mi._superblock == nil {
		t.Fatal("instance superblock is nil after createSuperblock()")
	}
	if mi._superblock.System != t.Name() {
		t.Fatalf("expected superblock system name to be %q, got %q", t.Name(), mi._superblock.System)
	}

	for idx, i := range h.Instances() {
		if i._superblock.Rank.Uint32() != uint32(idx) {
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
