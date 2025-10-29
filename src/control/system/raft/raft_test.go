//
// (C) Copyright 2024 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package raft

import (
	"net"
	"testing"
	"time"

	"github.com/hashicorp/raft"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
)

func TestRaft_Database_Barrier(t *testing.T) {
	for name, tc := range map[string]struct {
		raftSvcCfg *mockRaftServiceConfig
		expErr     error
	}{
		"raft svc barrier failed": {
			raftSvcCfg: &mockRaftServiceConfig{
				BarrierReturn: &mockRaftFuture{
					err: errors.New("mock error"),
				},
			},
			expErr: errors.New("mock error"),
		},
		"raft svc leadership error": {
			raftSvcCfg: &mockRaftServiceConfig{
				BarrierReturn: &mockRaftFuture{
					err: raft.ErrNotLeader,
				},
			},
			expErr: &system.ErrNotLeader{},
		},
		"success": {
			raftSvcCfg: &mockRaftServiceConfig{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			db, err := NewDatabase(log, &DatabaseConfig{})
			if err != nil {
				t.Fatal(err)
			}
			db.raft.setSvc(newMockRaftService(tc.raftSvcCfg, (*fsm)(db)))
			db.initialized.SetTrue()

			err = db.Barrier()
			test.CmpErr(t, tc.expErr, err)
		})
	}
}

func TestRaft_Database_WaitForLeaderStepUp(t *testing.T) {
	for name, tc := range map[string]struct {
		stepUpDelay time.Duration
	}{
		"no delay": {},
		"10 ms": {
			stepUpDelay: 10 * time.Millisecond,
		},
		"25 ms": {
			stepUpDelay: 25 * time.Millisecond,
		},
		"500 ms": {
			stepUpDelay: 500 * time.Millisecond,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			db := MockDatabase(t, log)
			db.initialized.SetTrue()
			db.steppingUp.SetTrue()
			fullDelay := tc.stepUpDelay + 10*time.Millisecond
			go func() {
				time.Sleep(fullDelay)
				db.steppingUp.SetFalse()
			}()

			start := time.Now()
			db.WaitForLeaderStepUp()
			duration := time.Since(start)

			test.AssertTrue(t, duration >= tc.stepUpDelay, "")

			// Wiggle room for the total duration: 10ms for loop interval + a little more for potentially slow machines
			test.AssertTrue(t, duration <= tc.stepUpDelay+50*time.Millisecond, "")
		})
	}
}

func TestRaft_recoverIfReplicasRemoved(t *testing.T) {
	testReplicaAddrs := func(n int) []*net.TCPAddr {
		addrs := make([]*net.TCPAddr, 0, n)
		for i := 0; i < n; i++ {
			addrs = append(addrs, system.MockControlAddr(t, uint32(i)))
		}
		return addrs
	}

	testRaftServers := func(addrs ...*net.TCPAddr) []raft.Server {
		srvs := make([]raft.Server, 0, len(addrs))
		for _, a := range addrs {
			srvs = append(srvs, raft.Server{
				ID:      raft.ServerID(a.String()),
				Address: raft.ServerAddress(a.String()),
			})
		}
		return srvs
	}

	for name, tc := range map[string]struct {
		replicaAddrs     []*net.TCPAddr
		getCfgResult     raft.Configuration
		getCfgErr        error
		recoverErr       error
		expRecoverCalled bool
		expErr           error
	}{
		"get config fails": {
			replicaAddrs: testReplicaAddrs(1),
			getCfgErr:    errors.New("mock getCfg"),
			expErr:       errors.New("mock getCfg"),
		},
		"single server, no change": {
			replicaAddrs: testReplicaAddrs(1),
			getCfgResult: raft.Configuration{
				Servers: testRaftServers(testReplicaAddrs(1)...),
			},
		},
		"multi server, no change": {
			replicaAddrs: testReplicaAddrs(5),
			getCfgResult: raft.Configuration{
				Servers: testRaftServers(testReplicaAddrs(5)...),
			},
		},
		"server added": {
			replicaAddrs: testReplicaAddrs(6),
			getCfgResult: raft.Configuration{
				Servers: testRaftServers(testReplicaAddrs(5)...),
			},
		},
		"server removed": {
			replicaAddrs: testReplicaAddrs(3),
			getCfgResult: raft.Configuration{
				Servers: testRaftServers(testReplicaAddrs(4)...),
			},
			expRecoverCalled: true,
		},
		"recover error": {
			replicaAddrs: testReplicaAddrs(3),
			getCfgResult: raft.Configuration{
				Servers: testRaftServers(testReplicaAddrs(4)...),
			},
			recoverErr:       errors.New("mock recover"),
			expErr:           errors.New("mock recover"),
			expRecoverCalled: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, _ := logging.NewTestLogger(t.Name())
			ctx := test.MustLogContext(t, log)

			mockGetCfg := func(_ logging.Logger, _ *DatabaseConfig) (raft.Configuration, error) {
				return tc.getCfgResult, tc.getCfgErr
			}

			recoverCalled := false
			mockRecover := func(_ logging.Logger, _ *DatabaseConfig) error {
				recoverCalled = true
				return tc.recoverErr
			}

			db := MockDatabaseWithCfg(t, logging.FromContext(ctx), &DatabaseConfig{
				Replicas: tc.replicaAddrs,
			})

			err := db.recoverIfReplicasRemoved(mockGetCfg, mockRecover)

			test.CmpErr(t, tc.expErr, err)
			test.AssertEqual(t, tc.expRecoverCalled, recoverCalled, "")
		})
	}
}
