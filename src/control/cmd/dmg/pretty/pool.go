//
// (C) Copyright 2020-2021 Intel Corporation.
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

package pretty

import (
	"fmt"
	"io"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
)

// PrintPoolQueryResponse generates a human-readable representation of the supplied
// PoolQueryResp struct and writes it to the supplied io.Writer.
func PrintPoolQueryResponse(pqr *control.PoolQueryResp, out io.Writer, opts ...PrintConfigOption) error {
	if pqr == nil {
		return errors.Errorf("nil %T", pqr)
	}
	w := txtfmt.NewErrWriter(out)

	// Maintain output compatibility with the `daos pool query` output.
	fmt.Fprintf(w, "Pool %s, ntarget=%d, disabled=%d, leader=%d, version=%d\n",
		pqr.UUID, pqr.TotalTargets, pqr.DisabledTargets, pqr.Leader, pqr.Version)
	fmt.Fprintln(w, "Pool space info:")
	fmt.Fprintf(w, "- Target(VOS) count:%d\n", pqr.ActiveTargets)
	if pqr.Scm != nil {
		fmt.Fprintln(w, "- SCM:")
		fmt.Fprintf(w, "  Total size: %s\n", humanize.Bytes(pqr.Scm.Total))
		fmt.Fprintf(w, "  Free: %s, min:%s, max:%s, mean:%s\n",
			humanize.Bytes(pqr.Scm.Free), humanize.Bytes(pqr.Scm.Min),
			humanize.Bytes(pqr.Scm.Max), humanize.Bytes(pqr.Scm.Mean))
	}
	if pqr.Nvme != nil {
		fmt.Fprintln(w, "- NVMe:")
		fmt.Fprintf(w, "  Total size: %s\n", humanize.Bytes(pqr.Nvme.Total))
		fmt.Fprintf(w, "  Free: %s, min:%s, max:%s, mean:%s\n",
			humanize.Bytes(pqr.Nvme.Free), humanize.Bytes(pqr.Nvme.Min),
			humanize.Bytes(pqr.Nvme.Max), humanize.Bytes(pqr.Nvme.Mean))
	}
	if pqr.Rebuild != nil {
		if pqr.Rebuild.Status == 0 {
			fmt.Fprintf(w, "Rebuild %s, %d objs, %d recs\n",
				pqr.Rebuild.State, pqr.Rebuild.Objects, pqr.Rebuild.Records)
		} else {
			fmt.Fprintf(w, "Rebuild failed, rc=%d, status=%d\n", pqr.Status, pqr.Rebuild.Status)
		}
	}

	return w.Err
}

// PrintPoolCreateResponse generates a human-readable representation of the pool create
// response and prints it to the supplied io.Writer.
func PrintPoolCreateResponse(pcr *control.PoolCreateResp, out io.Writer, opts ...PrintConfigOption) error {
	if pcr == nil {
		return errors.New("nil response")
	}

	ratio := 1.0
	if pcr.NvmeBytes > 0 {
		ratio = float64(pcr.ScmBytes) / float64(pcr.NvmeBytes)
	}

	if len(pcr.TgtRanks) == 0 {
		return errors.New("create response had 0 target ranks")
	}

	numRanks := uint64(len(pcr.TgtRanks))
	title := fmt.Sprintf("Pool created with %0.2f%%%% SCM/NVMe ratio", ratio*100)
	_, err := fmt.Fprintln(out, txtfmt.FormatEntity(title, []txtfmt.TableRow{
		{"UUID": pcr.UUID},
		{"Service Ranks": FormatRanks(pcr.SvcReps)},
		{"Storage Ranks": FormatRanks(pcr.TgtRanks)},
		{"Total Size": humanize.Bytes((pcr.ScmBytes + pcr.NvmeBytes) * numRanks)},
		{"SCM": fmt.Sprintf("%s (%s / rank)", humanize.Bytes(pcr.ScmBytes*numRanks), humanize.Bytes(pcr.ScmBytes))},
		{"NVMe": fmt.Sprintf("%s (%s / rank)", humanize.Bytes(pcr.NvmeBytes*numRanks), humanize.Bytes(pcr.NvmeBytes))},
	}))

	return err
}
