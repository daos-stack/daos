//
// (C) Copyright 2019-2020 Intel Corporation.
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
	"fmt"
	"net"
	"os"
	"path/filepath"
	"strconv"
	"sync"
	"syscall"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"
	"google.golang.org/grpc"

	. "github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/server/ioserver"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
	"github.com/daos-stack/daos/src/control/system"
)

const (
	testShortTimeout   = 60 * time.Millisecond
	testMediumTimeout  = 100 * testShortTimeout
	testLongTimeout    = 2 * testMediumTimeout
	delayedFailTimeout = 80 * testShortTimeout
)

func TestServer_HarnessCreateSuperblocks(t *testing.T) {
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

	if err := h.CreateSuperblocks(false); err != nil {
		t.Fatal(err)
	}

	h.started.SetTrue()
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

func TestServer_HarnessGetMSLeaderInstance(t *testing.T) {
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
			defer ShowBufferOnFailure(t, buf)

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
			h.started.SetTrue()

			_, err := h.GetMSLeaderInstance()
			CmpErr(t, tc.expError, err)
		})
	}
}

func TestServer_HarnessIOServerStart(t *testing.T) {
	defaultAddrStr := "127.0.0.1:10001"
	defaultAddr, err := net.ResolveTCPAddr("tcp", defaultAddrStr)
	if err != nil {
		t.Fatal(err)
	}

	for name, tc := range map[string]struct {
		trc              *ioserver.TestRunnerConfig
		isAP             bool           // should first instance be AP/MS replica/bootstrap
		rankInSuperblock bool           // rank already set in superblock when starting
		instanceUuids    map[int]string // UUIDs for each instance.Index()
		expStartErr      error
		expStartCount    int
		expDrpcCalls     map[uint32][]int32     // method ids called for each instance.Index()
		expGrpcCalls     map[uint32][]string    // string repr of call for each instance.Index()
		expRanks         map[uint32]system.Rank // ranks to have been set during Start()
		expMembers       system.Members         // members to have been registered during Stop()
		expIoErrs        map[uint32]error       // errors expected from instances
	}{
		"normal startup/shutdown": {
			trc: &ioserver.TestRunnerConfig{
				ErrChanCb: func(idx uint32) ioserver.InstanceError {
					time.Sleep(testLongTimeout)
					return ioserver.InstanceError{Idx: idx}
				},
			},
			instanceUuids: map[int]string{
				0: MockUUID(0),
				1: MockUUID(1),
			},
			expStartErr:   context.DeadlineExceeded,
			expStartCount: maxIOServers,
			expDrpcCalls: map[uint32][]int32{
				0: {
					drpc.MethodSetRank,
					drpc.MethodSetUp,
				},
				1: {
					drpc.MethodSetRank,
					drpc.MethodSetUp,
				},
			},
			expGrpcCalls: map[uint32][]string{
				0: {fmt.Sprintf("Join %d", system.NilRank)},
				1: {fmt.Sprintf("Join %d", system.NilRank)},
			},
			expRanks: map[uint32]system.Rank{
				0: system.Rank(0),
				1: system.Rank(1),
			},
		},
		"startup/shutdown with preset ranks": {
			trc: &ioserver.TestRunnerConfig{
				ErrChanCb: func(idx uint32) ioserver.InstanceError {
					time.Sleep(testLongTimeout)
					return ioserver.InstanceError{Idx: idx}
				},
			},
			rankInSuperblock: true,
			expStartErr:      context.DeadlineExceeded,
			expStartCount:    maxIOServers,
			expDrpcCalls: map[uint32][]int32{
				0: {
					drpc.MethodSetRank,
					drpc.MethodSetUp,
				},
				1: {
					drpc.MethodSetRank,
					drpc.MethodSetUp,
				},
			},
			expGrpcCalls: map[uint32][]string{
				0: {"Join 1"}, // rank == instance.Index() + 1
				1: {"Join 2"},
			},
			expRanks: map[uint32]system.Rank{
				0: system.Rank(1),
				1: system.Rank(2),
			},
		},
		"normal startup/shutdown with MS bootstrap": {
			trc: &ioserver.TestRunnerConfig{
				ErrChanCb: func(idx uint32) ioserver.InstanceError {
					time.Sleep(testLongTimeout)
					return ioserver.InstanceError{Idx: idx}
				},
			},
			isAP: true,
			instanceUuids: map[int]string{
				0: MockUUID(0),
				1: MockUUID(1),
			},
			expStartErr:   context.DeadlineExceeded,
			expStartCount: maxIOServers,
			expDrpcCalls: map[uint32][]int32{
				0: {
					drpc.MethodSetRank,
					drpc.MethodCreateMS,
					drpc.MethodStartMS,
					drpc.MethodSetUp,
				},
				1: {
					drpc.MethodSetRank,
					drpc.MethodSetUp,
				},
			},
			expGrpcCalls: map[uint32][]string{
				0: {"Join 0"}, // bootstrap instance will be pre-allocated rank 0
				1: {fmt.Sprintf("Join %d", system.NilRank)},
			},
			expRanks: map[uint32]system.Rank{
				0: system.Rank(0),
				1: system.Rank(1),
			},
			expMembers: system.Members{ // bootstrap member is added on start
				system.NewMember(system.Rank(0), "", defaultAddr, system.MemberStateStarted),
			},
		},
		"fails to start": {
			trc:           &ioserver.TestRunnerConfig{StartErr: errors.New("no")},
			expStartErr:   errors.New("no"),
			expStartCount: 1, // first one starts, dies, next one never starts
		},
		"delayed failure": {
			trc: &ioserver.TestRunnerConfig{
				ErrChanCb: func(idx uint32) ioserver.InstanceError {
					time.Sleep(delayedFailTimeout)
					return ioserver.InstanceError{Idx: idx, Err: errors.New("oops")}
				},
			},
			instanceUuids: map[int]string{
				0: MockUUID(0),
				1: MockUUID(1),
			},
			expStartErr:   context.DeadlineExceeded,
			expStartCount: maxIOServers,
			expDrpcCalls: map[uint32][]int32{
				0: {
					drpc.MethodSetRank,
					drpc.MethodSetUp,
				},
				1: {
					drpc.MethodSetRank,
					drpc.MethodSetUp,
				},
			},
			expGrpcCalls: map[uint32][]string{
				0: {fmt.Sprintf("Join %d", system.NilRank)},
				1: {fmt.Sprintf("Join %d", system.NilRank)},
			},
			expRanks: map[uint32]system.Rank{
				0: system.Rank(0),
				1: system.Rank(1),
			},
			expIoErrs: map[uint32]error{
				0: errors.New("oops"),
				1: errors.New("oops"),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)

			testDir, cleanup := CreateTestDir(t)
			defer cleanup()

			srvCfgs := make([]*ioserver.Config, maxIOServers)
			for i := 0; i < maxIOServers; i++ {
				srvCfgs[i] = ioserver.NewConfig().
					WithScmClass("ram").
					WithScmRamdiskSize(1).
					WithScmMountPoint(filepath.Join(testDir, strconv.Itoa(i)))
			}
			config := NewConfiguration().WithServers(srvCfgs...)

			instanceStarts := 0
			harness := NewIOServerHarness(log)
			mockMSClients := make(map[int]*proto.MockMgmtSvcClient)
			for i, srvCfg := range config.Servers {
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
					AccessPoints: []string{defaultAddrStr},
				}
				msClient := newMgmtSvcClient(context.TODO(), log, msClientCfg)
				// create mock that implements MgmtSvcClient
				mockMSClient := proto.NewMockMgmtSvcClient(
					proto.MockMgmtSvcClientConfig{})
				// store for checking calls later
				mockMSClients[i] = mockMSClient.(*proto.MockMgmtSvcClient)
				mockConnectFn := func(ctx context.Context, ap string,
					tc *security.TransportConfig,
					fn func(context.Context, mgmtpb.MgmtSvcClient) error,
					extraDialOpts ...grpc.DialOption) error {

					return fn(ctx, mockMSClient)
				}
				// inject fn that uses the mock client to be used on connect
				msClient.connectFn = mockConnectFn

				srv := NewIOServerInstance(log, bdevProvider, scmProvider, msClient, runner)
				var isAP bool
				if tc.isAP && i == 0 { // first instance will be AP & bootstrap MS
					isAP = true
				}
				var uuid string
				if UUID, exists := tc.instanceUuids[i]; exists {
					uuid = UUID
				}
				var rank *system.Rank
				if tc.rankInSuperblock {
					rank = system.NewRankPtr(uint32(i + 1))
				} else if isAP { // bootstrap will assume rank 0
					rank = new(system.Rank)
				}
				srv.setSuperblock(&Superblock{
					MS: isAP, UUID: uuid, Rank: rank, CreateMS: isAP, BootstrapMS: isAP,
				})

				if err := harness.AddInstance(srv); err != nil {
					t.Fatal(err)
				}
			}

			instances := harness.Instances()

			// set mock dRPC client to record call details
			for _, srv := range instances {
				srv.setDrpcClient(newMockDrpcClient(&mockDrpcClientConfig{
					SendMsgResponse: &drpc.Response{},
				}))
			}

			ctx, cancel := context.WithTimeout(context.Background(), testMediumTimeout)
			defer cancel()

			// start harness async and signal completion
			var gotErr error
			membership := system.NewMembership(log)
			done := make(chan struct{})
			go func(inCtx context.Context) {
				gotErr = harness.Start(ctx, membership, nil)
				close(done)
			}(ctx)

			waitDrpcReady := make(chan struct{})
			go func(inCtx context.Context) {
				for {
					ready := true
					for _, srv := range instances {
						if srv.waitDrpc.IsFalse() {
							ready = false
						}
					}
					if ready {
						close(waitDrpcReady)
						return
					}
					select {
					case <-time.After(testShortTimeout):
					case <-inCtx.Done():
						return
					}
				}
			}(ctx)

			select {
			case <-waitDrpcReady:
			case <-ctx.Done():
				if tc.expStartErr != context.DeadlineExceeded {
					<-done
					CmpErr(t, tc.expStartErr, gotErr)
					return
				}
				// deadline exceeded as expected but desired state not reached
				t.Fatalf("instances did not get to waiting for dRPC state: %s", ctx.Err())
			}
			t.Log("instances ready and waiting for dRPC ready notification")

			// simulate receiving notify ready whilst instances
			// running in harness
			for _, srv := range instances {
				req := getTestNotifyReadyReq(t, "/tmp/instance_test.sock", 0)
				go func(inCtx context.Context, i *IOServerInstance) {
					select {
					case i.drpcReady <- req:
					case <-inCtx.Done():
					}
				}(ctx, srv)
			}

			waitReady := make(chan struct{})
			go func(inCtx context.Context) {
				for {
					if len(harness.ReadyRanks()) == len(instances) {
						close(waitReady)
						return
					}
					select {
					case <-time.After(testShortTimeout):
					case <-inCtx.Done():
						return
					}
				}
			}(ctx)

			select {
			case <-waitReady:
			case <-ctx.Done():
				if tc.expStartErr != context.DeadlineExceeded {
					<-done
					CmpErr(t, tc.expStartErr, gotErr)
					return
				}
				// deadline exceeded as expected but desired state not reached
				t.Fatalf("instances did not get to ready state: %s", ctx.Err())
			}
			t.Log("instances setup and ready")

			<-done
			t.Log("harness Start() exited")

			if instanceStarts != tc.expStartCount {
				t.Fatalf("expected %d starts, got %d", tc.expStartCount, instanceStarts)
			}

			CmpErr(t, tc.expStartErr, gotErr)
			if tc.expStartErr != context.DeadlineExceeded {
				return
			}

			// verify expected RPCs were made, ranks allocated and
			// members added to membership
			for _, srv := range instances {
				gotDrpcCalls := srv._drpcClient.(*mockDrpcClient).Calls
				if diff := cmp.Diff(tc.expDrpcCalls[srv.Index()], gotDrpcCalls); diff != "" {
					t.Fatalf("unexpected dRPCs for instance %d (-want, +got):\n%s\n",
						srv.Index(), diff)
				}
				gotGrpcCalls := mockMSClients[int(srv.Index())].Calls
				if diff := cmp.Diff(tc.expGrpcCalls[srv.Index()], gotGrpcCalls); diff != "" {
					t.Fatalf("unexpected gRPCs for instance %d (-want, +got):\n%s\n",
						srv.Index(), diff)
				}
				rank, err := srv.GetRank()
				if err != nil {
					t.Fatal(err)
				}
				if diff := cmp.Diff(tc.expRanks[srv.Index()], rank); diff != "" {
					t.Fatalf("unexpected rank for instance %d (-want, +got):\n%s\n",
						srv.Index(), diff)
				}
				CmpErr(t, tc.expIoErrs[srv.Index()], srv._lastErr)
			}
			members := membership.Members([]system.Rank{})
			AssertEqual(t, len(tc.expMembers), len(members), "unexpected number in membership")
			for i, member := range members {
				if diff := cmp.Diff(fmt.Sprintf("%v", member),
					fmt.Sprintf("%v", tc.expMembers[i])); diff != "" {

					t.Fatalf("unexpected system membership (-want, +got):\n%s\n", diff)
				}
			}
		})
	}
}

func TestHarness_StopInstances(t *testing.T) {
	for name, tc := range map[string]struct {
		ioserverCount     int
		missingSB         bool
		signal            os.Signal
		ranks             []system.Rank
		harnessNotStarted bool
		signalErr         error
		ctxTimeout        time.Duration
		expRankErrs       map[system.Rank]error
		expSignalsSent    map[uint32]os.Signal
		expErr            error
	}{
		"nil signal": {
			expErr: errors.New("nil signal"),
		},
		"missing superblock": {
			missingSB: true,
			signal:    syscall.SIGKILL,
			expErr:    errors.New("nil superblock"),
		},
		"harness not started": {
			harnessNotStarted: true,
			signal:            syscall.SIGKILL,
			expSignalsSent:    map[uint32]os.Signal{},
		},
		"rank not in list": {
			ranks:          []system.Rank{system.Rank(2), system.Rank(3)},
			signal:         syscall.SIGKILL,
			expRankErrs:    map[system.Rank]error{},
			expSignalsSent: map[uint32]os.Signal{1: syscall.SIGKILL}, // instance 1 has rank 2
		},
		"signal send error": {
			signal:    syscall.SIGKILL,
			signalErr: errors.New("sending signal failed"),
			expRankErrs: map[system.Rank]error{
				1: errors.New("sending signal failed"),
				2: errors.New("sending signal failed"),
			},
			expSignalsSent: map[uint32]os.Signal{0: syscall.SIGKILL, 1: syscall.SIGKILL},
		},
		"context timeout": {
			signal:     syscall.SIGKILL,
			ctxTimeout: 1 * time.Nanosecond,
			expErr:     context.DeadlineExceeded,
		},
		"normal stop single-io": {
			ioserverCount:  1,
			signal:         syscall.SIGINT,
			expSignalsSent: map[uint32]os.Signal{0: syscall.SIGINT},
		},
		"normal stop multi-io": {
			signal:         syscall.SIGTERM,
			expSignalsSent: map[uint32]os.Signal{0: syscall.SIGTERM, 1: syscall.SIGTERM},
		},
		"force stop multi-io": {
			signal:         syscall.SIGKILL,
			expSignalsSent: map[uint32]os.Signal{0: syscall.SIGKILL, 1: syscall.SIGKILL},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)

			var signalsSent sync.Map
			if tc.ioserverCount == 0 {
				tc.ioserverCount = maxIOServers
			}
			if tc.ranks == nil {
				tc.ranks = []system.Rank{}
			}
			svc := newTestMgmtSvcMulti(log, tc.ioserverCount, false)
			if !tc.harnessNotStarted {
				svc.harness.started.SetTrue()
			}
			for i, srv := range svc.harness.Instances() {
				trc := &ioserver.TestRunnerConfig{}
				trc.SignalCb = func(idx uint32, sig os.Signal) { signalsSent.Store(idx, sig) }
				trc.SignalErr = tc.signalErr
				if !tc.harnessNotStarted {
					trc.Running.SetTrue()
				}

				srv.runner = ioserver.NewTestRunner(trc, ioserver.NewConfig())
				srv.SetIndex(uint32(i))

				if tc.missingSB {
					srv._superblock = nil
					continue
				}

				srv._superblock.Rank = new(system.Rank)
				*srv._superblock.Rank = system.Rank(i + 1)
			}

			if tc.ctxTimeout == 0 {
				tc.ctxTimeout = 100 * time.Millisecond
			}
			ctx, shutdown := context.WithTimeout(context.Background(), tc.ctxTimeout)
			defer shutdown()
			gotRankErrs, gotErr := svc.harness.StopInstances(ctx, tc.signal, tc.ranks...)
			CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
			if diff := cmp.Diff(
				fmt.Sprintf("%v", tc.expRankErrs), fmt.Sprintf("%v", gotRankErrs)); diff != "" {
				t.Fatalf("unexpected rank errors (-want, +got):\n%s\n", diff)
			}

			var numSignalsSent int
			signalsSent.Range(func(_, _ interface{}) bool {
				numSignalsSent++
				return true
			})
			AssertEqual(t, len(tc.expSignalsSent), numSignalsSent, "number of signals sent")

			for expKey, expValue := range tc.expSignalsSent {
				value, found := signalsSent.Load(expKey)
				if !found {
					t.Fatalf("rank %d was not sent %s signal", expKey, expValue)
				}
				if diff := cmp.Diff(expValue, value); diff != "" {
					t.Fatalf("unexpected signals sent (-want, +got):\n%s\n", diff)
				}
			}
		})
	}
}
