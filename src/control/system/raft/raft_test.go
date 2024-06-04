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
		raftSvcCfg  *mockRaftServiceConfig
		stepUpDelay time.Duration
		expErr      error
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
		"waits for step up": {
			raftSvcCfg:  &mockRaftServiceConfig{},
			stepUpDelay: 500 * time.Millisecond,
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
			db.steppingUp.SetTrue()
			fullDelay := tc.stepUpDelay + 10*time.Millisecond
			go func() {
				time.Sleep(fullDelay)
				db.steppingUp.SetFalse()
			}()

			start := time.Now()
			err = db.Barrier()
			duration := time.Since(start)

			test.CmpErr(t, tc.expErr, err)
			test.AssertTrue(t, duration >= tc.stepUpDelay, "")
		})
	}
}
