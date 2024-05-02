//
// (C) Copyright 2020-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pretty

import (
	"fmt"
	"io"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
)

const msgNoPools = "No pools in system"

func getTierNameText(tierIdx int) string {
	switch tierIdx {
	case int(daos.StorageMediaTypeScm):
		return fmt.Sprintf("- Storage tier %d (SCM):", tierIdx)
	case int(daos.StorageMediaTypeNvme):
		return fmt.Sprintf("- Storage tier %d (NVMe):", tierIdx)
	default:
		return fmt.Sprintf("- Storage tier %d (unknown):", tierIdx)
	}
}

// PrintPoolInfo generates a human-readable representation of the supplied
// PoolQueryResp struct and writes it to the supplied io.Writer.
func PrintPoolInfo(pi *daos.PoolInfo, out io.Writer) error {
	if pi == nil {
		return errors.Errorf("nil %T", pi)
	}
	w := txtfmt.NewErrWriter(out)

	// Maintain output compatibility with the `daos pool query` output.
	fmt.Fprintf(w, "Pool %s, ntarget=%d, disabled=%d, leader=%d, version=%d, state=%s\n",
		pi.UUID, pi.TotalTargets, pi.DisabledTargets, pi.ServiceLeader, pi.Version, pi.State)

	if pi.PoolLayoutVer != pi.UpgradeLayoutVer {
		fmt.Fprintf(w, "Pool layout out of date (%d < %d) -- see `dmg pool upgrade` for details.\n",
			pi.PoolLayoutVer, pi.UpgradeLayoutVer)
	}
	fmt.Fprintln(w, "Pool health info:")
	if pi.EnabledRanks != nil && pi.EnabledRanks.Count() > 0 {
		fmt.Fprintf(w, "- Enabled ranks: %s\n", pi.EnabledRanks)
	}
	if pi.DisabledRanks != nil && pi.DisabledRanks.Count() > 0 {
		fmt.Fprintf(w, "- Disabled ranks: %s\n", pi.DisabledRanks)
	}
	if pi.Rebuild != nil {
		if pi.Rebuild.Status == 0 {
			fmt.Fprintf(w, "- Rebuild %s, %d objs, %d recs\n",
				pi.Rebuild.State, pi.Rebuild.Objects, pi.Rebuild.Records)
		} else {
			fmt.Fprintf(w, "- Rebuild failed, status=%d\n", pi.Rebuild.Status)
		}
	}

	if pi.QueryMask.HasOption("space") && pi.TierStats != nil {
		fmt.Fprintln(w, "Pool space info:")
		fmt.Fprintf(w, "- Target(VOS) count:%d\n", pi.ActiveTargets)
		for tierIdx, tierStats := range pi.TierStats {
			fmt.Fprintln(w, getTierNameText(tierIdx))
			fmt.Fprintf(w, "  Total size: %s\n", humanize.Bytes(tierStats.Total))
			fmt.Fprintf(w, "  Free: %s, min:%s, max:%s, mean:%s\n",
				humanize.Bytes(tierStats.Free), humanize.Bytes(tierStats.Min),
				humanize.Bytes(tierStats.Max), humanize.Bytes(tierStats.Mean))
		}
	}
	return w.Err
}

// PrintPoolQueryTargetInfo generates a human-readable representation of the supplied
// PoolQueryTargetResp struct and writes it to the supplied io.Writer.
func PrintPoolQueryTargetInfo(pqti *daos.PoolQueryTargetInfo, out io.Writer) error {
	if pqti == nil {
		return errors.Errorf("nil %T", pqti)
	}
	w := txtfmt.NewErrWriter(out)

	// Maintain output compatibility with the `daos pool query-targets` output.
	fmt.Fprintf(w, "Target: type %s, state %s\n", pqti.Type, pqti.State)
	if pqti.Space != nil {
		for tierIdx, tierUsage := range pqti.Space {
			fmt.Fprintln(w, getTierNameText(tierIdx))
			fmt.Fprintf(w, "  Total size: %s\n", humanize.Bytes(tierUsage.Total))
			fmt.Fprintf(w, "  Free: %s\n", humanize.Bytes(tierUsage.Free))
		}
	}

	return w.Err
}
