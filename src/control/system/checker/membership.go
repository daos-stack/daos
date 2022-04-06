//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package checker

import (
	"context"
	"fmt"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
)

var _ Checker = (*MemberChecker)(nil)

type (
	MemberDataSource interface {
		AllMembers() ([]*system.Member, error)
		MemberRanks(...system.MemberState) ([]system.Rank, error)
	}

	MemberChecker struct {
		log     logging.Logger
		Members MemberDataSource
	}
)

func NewMemberChecker(log logging.Logger, pools MemberDataSource) *MemberChecker {
	return &MemberChecker{
		log:     log,
		Members: pools,
	}
}

func (c *MemberChecker) checkMembers(ctx context.Context, db FindingStore) error {
	c.log.Debug("running member checks")

	members, err := c.Members.AllMembers()
	if err != nil {
		return err
	}

	checkableState := system.MemberStateAdminExcluded | system.MemberStateCheckerStarted
	unCheckable := system.MustCreateRankSet("")
	for _, member := range members {
		if member.State&checkableState == 0 {
			c.log.Debugf("member %s state (%s): not checkable", member.Rank.String(), member.State)
			unCheckable.Add(member.Rank)
		}
	}

	if unCheckable.Count() > 0 {
		if err := db.AddCheckerFinding(NewMemberFinding(0,
			fmt.Sprintf("%d ranks not in checkable state: %s", unCheckable.Count(), unCheckable.String()),
		)); err != nil {
			return err
		}
	}

	return nil
}

func (c *MemberChecker) RunPassChecks(ctx context.Context, pass Pass, db FindingStore) error {
	switch pass {
	case PassInit:
		return c.checkMembers(ctx, db)
	default:
		c.log.Debugf("unimplemented pass: %q", pass)
	}

	return nil
}
