// (C) Copyright 2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
package main

import (
	"github.com/dustin/go-humanize"

	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/logging"
)

func printSuperBlock(log *logging.LeveledLogger, sb *daos.PoolSuperblock) {
	log.Infof("Pool UUID: %s\n", sb.PoolUuid.String())
	log.Infof("Format Version: %d\n", sb.DurableFormatVersion)
	log.Infof("Containers: %d\n", sb.ContCount)
	log.Infof("SCM Size: %s", humanize.Bytes(sb.ScmSize))
	log.Infof("NVME Size: %s", humanize.Bytes(sb.NvmeSize))
	log.Infof("Block Size: %s", humanize.Bytes(sb.BlockSize))
	log.Infof("Reserved Blocks: %d\n", sb.HdrBlocks)
	log.Infof("Block Device Capacity: %s", humanize.Bytes(sb.TotalBlocks))
}
