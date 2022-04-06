//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package checker

import (
	"context"
	"fmt"

	"github.com/google/uuid"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
)

var _ Checker = (*PoolChecker)(nil)

type (
	PoolDataSource interface {
		PoolServiceList(all bool) ([]*system.PoolService, error)
		FindPoolServiceByUUID(uuid uuid.UUID) (*system.PoolService, error)
		UpdatePoolService(ps *system.PoolService) error
	}

	EngineChecker interface {
		CallEngineChecker(ctx context.Context, req *EngineCheckerReq) (*EngineCheckerResp, error)
	}

	PoolChecker struct {
		log     logging.Logger
		Pools   PoolDataSource
		Members MemberDataSource
		Engine  EngineChecker
	}

	EngineCheckerReq struct {
		Pass      Pass
		Ranks     []system.Rank
		PoolUUIDs []uuid.UUID
	}

	EngineCheckerResult struct {
		Pass     Pass
		Findings []*Finding
		Pool     *system.PoolService
	}

	EngineCheckerResp struct {
		Results []*EngineCheckerResult
	}
)

func NewPoolChecker(log logging.Logger, pools PoolDataSource, members MemberDataSource, engine EngineChecker) *PoolChecker {
	return &PoolChecker{
		log:     log,
		Pools:   pools,
		Members: members,
		Engine:  engine,
	}
}

func (c *PoolChecker) runPoolListPass(ctx context.Context, db FindingStore, ranks []system.Rank) error {
	req := &EngineCheckerReq{
		Pass:  PassPoolList,
		Ranks: ranks,
	}

	// Call down into the local engine checker to initiate the pool
	// service scan across the ranks.
	resp, err := c.Engine.CallEngineChecker(ctx, req)
	if err != nil {
		return err
	}

	// Check the scan results against the MS.
	for _, result := range resp.Results {
		// First, populate the findings DB with any results from the engine side of the scan.
		for _, finding := range result.Findings {
			if err := db.AddCheckerFinding(AnnotateFinding(finding)); err != nil {
				return err
			}
		}

		// Next, check the scanned pool service is in the MS.
		ps, err := c.Pools.FindPoolServiceByUUID(result.Pool.PoolUUID)
		if err != nil {
			if !system.IsPoolNotFound(err) {
				return err
			}
			c.log.Debugf("pool service %q not found in MS", result.Pool.PoolUUID)
			if err = db.AddCheckerFinding(NewPoolFinding(FindingCodeScannedPoolMissing,
				fmt.Sprintf("Scanned pool service %s not found in MS", result.Pool.PoolUUID),
			)); err != nil {
				return err
			}
			continue
		}

		// Verify that the labels match.
		if ps.PoolLabel != result.Pool.PoolLabel {
			c.log.Debugf("pool service %q label mismatch: MS=%q, scan=%q", ps.PoolUUID, ps.PoolLabel, result.Pool.PoolLabel)
			if err = db.AddCheckerFinding(
				NewPoolFinding(FindingCodePoolLabelMismatch, ps.PoolUUID.String(), ps.PoolLabel, result.Pool.PoolLabel),
			); err != nil {
				return err
			}
		}

		// Update the MS entry with the scanned service ranks, as this is
		// the canonical source of that information.
		ps.Replicas = result.Pool.Replicas
		if err = c.Pools.UpdatePoolService(ps); err != nil {
			return err
		}

		// TODO: Verify storage details.
	}

	// Finally, check the MS for any pools that are not in the scan results.
	scanPools := make(map[uuid.UUID]struct{})
	for _, result := range resp.Results {
		scanPools[result.Pool.PoolUUID] = struct{}{}
	}

	msPools, err := c.Pools.PoolServiceList(true)
	if err != nil {
		return err
	}

	for _, ps := range msPools {
		if _, found := scanPools[ps.PoolUUID]; found {
			continue
		}

		c.log.Debugf("pool service %q not found in scan results", ps.PoolUUID)
		if err = db.AddCheckerFinding(
			NewPoolFinding(FindingCodeMsPoolMissing, ps.PoolUUID.String(), ps.PoolLabel),
		); err != nil {
			return err
		}
	}

	return nil
}

func (c *PoolChecker) runPoolMembersPass(ctx context.Context, db FindingStore, ranks []system.Rank) error {
	pools, err := c.Pools.PoolServiceList(true)
	if err != nil {
		return err
	}

	req := &EngineCheckerReq{
		Pass:  PassPoolMembers,
		Ranks: ranks,
	}

	for _, ps := range pools {
		req.PoolUUIDs = append(req.PoolUUIDs, ps.PoolUUID)
	}

	// Call down into the local engine checker to initiate the pool
	// membership scan across the ranks.
	resp, err := c.Engine.CallEngineChecker(ctx, req)
	if err != nil {
		return err
	}

	for _, result := range resp.Results {
		// Populate the findings DB with any results from the engine side of the scan.
		for _, finding := range result.Findings {
			if err := db.AddCheckerFinding(AnnotateFinding(finding)); err != nil {
				return err
			}
		}
	}

	return nil
}

func (c *PoolChecker) runPoolCleanupPass(ctx context.Context, db FindingStore, ranks []system.Rank) error {
	pools, err := c.Pools.PoolServiceList(true)
	if err != nil {
		return err
	}

	req := &EngineCheckerReq{
		Pass:  PassPoolCleanup,
		Ranks: ranks,
	}

	for _, ps := range pools {
		req.PoolUUIDs = append(req.PoolUUIDs, ps.PoolUUID)
	}

	// Call down into the local engine checker to initiate the pool
	// cleanup pass on the ranks.
	resp, err := c.Engine.CallEngineChecker(ctx, req)
	if err != nil {
		return err
	}

	for _, result := range resp.Results {
		// Populate the findings DB with any results from the engine side of the scan.
		for _, finding := range result.Findings {
			if err := db.AddCheckerFinding(AnnotateFinding(finding)); err != nil {
				return err
			}
		}
	}

	return nil
}

func (c *PoolChecker) RunPassChecks(ctx context.Context, pass Pass, db FindingStore) error {
	// Get the set of system ranks where the checkers are running.
	ranks, err := c.Members.MemberRanks(system.MemberStateCheckerStarted)
	if err != nil {
		return err
	}

	switch pass {
	case PassPoolList:
		return c.runPoolListPass(ctx, db, ranks)
	case PassPoolMembers:
		return c.runPoolMembersPass(ctx, db, ranks)
	case PassPoolCleanup:
		return c.runPoolCleanupPass(ctx, db, ranks)
	default:
		c.log.Debugf("unimplemented pass: %q", pass)
	}

	return nil
}
