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
	"strconv"
	"strings"
	"testing"
	"time"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/ioserver"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

func TestHarnessCreateSuperblocks(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer common.ShowBufferOnFailure(t, buf)

	testDir, err := ioutil.TempDir("", strings.Replace(t.Name(), "/", "-", -1))
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(testDir)

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

	if err := h.CreateSuperblocks(false); err != nil {
		t.Fatal(err)
	}

	mi, err := h.GetMSLeaderInstance()
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

func TestHarnessGetMSLeaderInstance(t *testing.T) {
	defaultApList := []string{"1.2.3.4:5", "6.7.8.9:10"}
	defaultCtrlList := []string{"6.3.1.2:5", "1.2.3.4:5"}
	for name, tc := range map[string]struct {
		instanceCount int
		hasSuperblock bool
		apList        []string
		ctrlAddrs     []string
		expError      error
	}{
		"zero instances": {
			apList:   defaultApList,
			expError: errors.New("harness has no managed instances"),
		},
		"empty AP list": {
			instanceCount: 2,
			apList:        nil,
			ctrlAddrs:     defaultApList,
			expError:      errors.New("no access points defined"),
		},
		"not MS leader": {
			instanceCount: 1,
			apList:        defaultApList,
			ctrlAddrs:     []string{"4.3.2.1:10"},
			expError:      errors.New("not an access point"),
		},
		"is MS leader, but no superblock": {
			instanceCount: 2,
			apList:        defaultApList,
			ctrlAddrs:     defaultCtrlList,
			expError:      errors.New("not an access point"),
		},
		"is MS leader": {
			instanceCount: 2,
			hasSuperblock: true,
			apList:        defaultApList,
			ctrlAddrs:     defaultCtrlList,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			// ugh, this isn't ideal
			oldGetAddrFn := getInterfaceAddrs
			defer func() {
				getInterfaceAddrs = oldGetAddrFn
			}()
			getInterfaceAddrs = func() ([]net.Addr, error) {
				addrs := make([]net.Addr, len(tc.ctrlAddrs))
				var err error
				for i, ca := range tc.ctrlAddrs {
					addrs[i], err = net.ResolveTCPAddr("tcp", ca)
					if err != nil {
						return nil, err
					}
				}
				return addrs, nil
			}

			h := NewIOServerHarness(log)
			for i := 0; i < tc.instanceCount; i++ {
				cfg := ioserver.NewConfig().
					WithRank(uint32(i)).
					WithSystemName(t.Name()).
					WithScmClass("ram").
					WithScmMountPoint(strconv.Itoa(i))
				r := ioserver.NewRunner(log, cfg)

				m := newMgmtSvcClient(
					context.Background(), log, mgmtSvcClientCfg{
						ControlAddr:  &net.TCPAddr{},
						AccessPoints: tc.apList,
					},
				)

				isAP := func(ca string) bool {
					for _, ap := range tc.apList {
						if ca == ap {
							return true
						}
					}
					return false
				}
				srv := NewIOServerInstance(log, nil, nil, m, r)
				if tc.hasSuperblock {
					srv.setSuperblock(&Superblock{
						MS: isAP(tc.ctrlAddrs[i]),
					})
				}
				if err := h.AddInstance(srv); err != nil {
					t.Fatal(err)
				}
			}

			_, err := h.GetMSLeaderInstance()
			common.CmpErr(t, tc.expError, err)
		})
	}
}

func TestHarnessIOServerStart(t *testing.T) {
	for name, tc := range map[string]struct {
		trc           *ioserver.TestRunnerConfig
		expStartErr   error
		expStartCount int
	}{
		"normal startup/shutdown": {
			expStartErr:   context.Canceled,
			expStartCount: maxIoServers,
		},
		"fails to start": {
			trc:           &ioserver.TestRunnerConfig{StartErr: errors.New("no")},
			expStartErr:   errors.New("no"),
			expStartCount: 1, // first one starts, dies, next one never starts
		},
		"delayed failure": {
			trc: &ioserver.TestRunnerConfig{
				ErrChanCb: func() error {
					time.Sleep(10 * time.Millisecond)
					return errors.New("oops")
				},
			},
			expStartErr:   errors.New("oops"),
			expStartCount: maxIoServers,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			testDir, err := ioutil.TempDir("", strings.Replace(t.Name(), "/", "-", -1))
			if err != nil {
				t.Fatal(err)
			}
			defer os.RemoveAll(testDir)

			srvCfgs := make([]*ioserver.Config, maxIoServers)
			for i := 0; i < maxIoServers; i++ {
				srvCfgs[i] = ioserver.NewConfig().
					WithScmClass("ram").
					WithScmRamdiskSize(1).
					WithScmMountPoint(filepath.Join(testDir, strconv.Itoa(i)))
			}
			config := NewConfiguration().WithServers(srvCfgs...)

			instanceStarts := 0
			harness := NewIOServerHarness(log)
			for _, srvCfg := range config.Servers {
				if err := os.MkdirAll(srvCfg.Storage.SCM.MountPoint, 0777); err != nil {
					t.Fatal(err)
				}

				if tc.trc == nil {
					tc.trc = &ioserver.TestRunnerConfig{}
				}
				if tc.trc.StartCb == nil {
					tc.trc.StartCb = func() { instanceStarts++ }
				}
				runner := ioserver.NewTestRunner(tc.trc, srvCfg)
				bdevProvider, err := bdev.NewClassProvider(log,
					srvCfg.Storage.SCM.MountPoint, &srvCfg.Storage.Bdev)
				if err != nil {
					t.Fatal(err)
				}
				scmProvider := scm.NewMockProvider(log, nil, &scm.MockSysConfig{IsMountedBool: true})
				msClientCfg := mgmtSvcClientCfg{
					ControlAddr:  &net.TCPAddr{},
					AccessPoints: []string{"localhost"},
				}
				msClient := newMgmtSvcClient(context.TODO(), log, msClientCfg)
				srv := NewIOServerInstance(log, bdevProvider, scmProvider, msClient, runner)
				if err := harness.AddInstance(srv); err != nil {
					t.Fatal(err)
				}
			}

			if err := harness.CreateSuperblocks(false); err != nil {
				t.Fatal(err)
			}

			for _, srv := range harness.Instances() {
				// simulate ready notification
				srv.setDrpcClient(newMockDrpcClient(&mockDrpcClientConfig{
					SendMsgResponse: &drpc.Response{},
				}))
			}

			done := make(chan struct{})
			ctx, shutdown := context.WithCancel(context.Background())
			go func(t *testing.T, expStartErr error, th *IOServerHarness) {
				common.CmpErr(t, expStartErr, th.Start(ctx, nil))
				close(done)
			}(t, tc.expStartErr, harness)

			time.Sleep(50 * time.Millisecond)
			shutdown()
			<-done // wait for inner goroutine to finish

			if instanceStarts != tc.expStartCount {
				t.Fatalf("expected %d starts, got %d", tc.expStartCount, instanceStarts)
			}

			if tc.expStartErr != context.Canceled {
				return
			}

			for _, srv := range harness.Instances() {
				expCall := &drpc.Call{
					Module: drpc.ModuleMgmt,
					Method: drpc.MethodSetUp,
				}
				lastCall := srv._drpcClient.(*mockDrpcClient).SendMsgInputCall
				if lastCall == nil ||
					lastCall.Module != expCall.Module ||
					lastCall.Method != expCall.Method {
					t.Fatalf("expected final dRPC call for instance %d to be %s, got %s",
						srv.Index(), expCall, lastCall)
				}
			}
		})
	}
}
