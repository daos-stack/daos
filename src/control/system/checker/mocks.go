//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package checker

import (
	"math/rand"
	"time"

	chkpb "github.com/daos-stack/daos/src/control/common/proto/chk"
	"github.com/daos-stack/daos/src/control/common/test"
)

func MockFinding(idx ...int) *Finding {
	if len(idx) == 0 {
		idx = []int{rand.Int()}
	}
	return &Finding{
		CheckReport: chkpb.CheckReport{
			Seq:       uint64(idx[0]),
			Class:     chkpb.CheckInconsistClass(rand.Int31n(int32(len(chkpb.CheckInconsistClass_name)))),
			Action:    chkpb.CheckInconsistAction(rand.Int31n(int32(len(chkpb.CheckInconsistAction_name)))),
			Rank:      uint32(idx[0]),
			Target:    uint32(idx[0]),
			PoolUuid:  test.MockUUID(int32(idx[0])),
			ContUuid:  test.MockUUID(int32(idx[0])),
			Timestamp: time.Now().String(),
			ActChoices: []chkpb.CheckInconsistAction{
				chkpb.CheckInconsistAction(rand.Int31n(int32(len(chkpb.CheckInconsistAction_name)))),
				chkpb.CheckInconsistAction(rand.Int31n(int32(len(chkpb.CheckInconsistAction_name)))),
				chkpb.CheckInconsistAction(rand.Int31n(int32(len(chkpb.CheckInconsistAction_name)))),
			},
		},
	}
}
