//
// (C) Copyright 2020-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pretty

import (
	"fmt"
	"io"
	"strings"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
)

const msgNoPools = "No pools in system"

func printPoolTiers(suss []*daos.StorageUsageStats, w *txtfmt.ErrWriter, fullStats bool) {
	mdOnSSD := false
	for tierIdx, tierStats := range suss {
		if tierIdx >= int(daos.StorageMediaTypeMax) {
			tierStats.MediaType = daos.StorageMediaTypeMax // Print unknown type tier.
		}

		switch {
		case tierIdx == 0 && tierStats.MediaType == daos.StorageMediaTypeNvme:
			// MD-on-SSD mode.
			// TODO: Print VOS index aggregate file size across pool as distinct from
			//       Meta-blob aggregate size.
			if fullStats {
				fmt.Fprintf(w, "- Total memory-file size: %s\n",
					humanize.Bytes(tierStats.Total))
			}
			fmt.Fprintf(w, "- Metadata storage:\n")
			mdOnSSD = true
		case mdOnSSD:
			fmt.Fprintf(w, "- Data storage:\n")
		default:
			fmt.Fprintf(w, "- Storage tier %d (%s):\n", tierIdx,
				strings.ToUpper(tierStats.MediaType.String()))
		}

		fmt.Fprintf(w, "  Total size: %s\n", humanize.Bytes(tierStats.Total))
		if fullStats {
			fmt.Fprintf(w, "  Free: %s, min:%s, max:%s, mean:%s\n",
				humanize.Bytes(tierStats.Free), humanize.Bytes(tierStats.Min),
				humanize.Bytes(tierStats.Max), humanize.Bytes(tierStats.Mean))
		} else {
			fmt.Fprintf(w, "  Free: %s\n", humanize.Bytes(tierStats.Free))
		}
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
	} else {
		fmt.Fprintln(w, "- No rebuild status available.")
	}

	if pi.QueryMask.HasOption(daos.PoolQueryOptionSpace) && pi.TierStats != nil {
		fmt.Fprintln(w, "Pool space info:")
		fmt.Fprintf(w, "- Target count:%d\n", pi.ActiveTargets)
		printPoolTiers(pi.TierStats, w, true)
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
		printPoolTiers(pqti.Space, w, false)
	}

	return w.Err
}
