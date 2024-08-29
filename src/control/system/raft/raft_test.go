//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package raft

import (
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
