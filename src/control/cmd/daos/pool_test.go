//
// (C) Copyright 2021-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"testing"

	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/golang/mock/gomock"

	"github.com/daos-stack/daos/src/control/common/test"
)

func TestDaos_PoolQuery(t *testing.T) {
	for name, tc := range map[string]struct {
		arg              daos.PoolQueryMask
		expEnabledCount  int
		expDisabledCount int
		expSuspectCount  int
		expErr           error
	}{
		"default Query mask": {
			arg:              daos.DefaultPoolQueryMask,
			expEnabledCount:  0,
			expDisabledCount: 2,
			expSuspectCount:  0,
		},
		"Health Query mask": {
			arg:              daos.HealthOnlyPoolQueryMask,
			expEnabledCount:  0,
			expDisabledCount: 2,
			expSuspectCount:  3,
		},
		"Query All mask": {
			arg:              daos.PoolQueryMask(^uint64(0)),
			expEnabledCount:  1,
			expDisabledCount: 2,
			expSuspectCount:  3,
		},
		"Query nothing": {
			arg:              daos.PoolQueryMask(uint64(0)),
			expEnabledCount:  0,
			expDisabledCount: 0,
			expSuspectCount:  0,
		},
	} {
		t.Run(name, func(t *testing.T) {
			ctrl := gomock.NewController(t)
			defer ctrl.Finish()

			mockDAOS := NewMockDAOSInterface(ctrl)
			poolInfo, gotErr := MockQueryPool(mockDAOS, tc.arg)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			test.AssertEqual(t, tc.expEnabledCount, poolInfo.EnabledRanks.Count(), "unexpected enabled count")
			test.AssertEqual(t, tc.expDisabledCount, poolInfo.DisabledRanks.Count(), "unexpected disabled count")
			test.AssertEqual(t, tc.expSuspectCount, poolInfo.DeadRanks.Count(), "unexpected suspect count")
		})
	}

}
