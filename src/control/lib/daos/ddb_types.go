// (C) Copyright 2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
package daos

import (
	"github.com/google/uuid"
)

// PoolSuperblock The pool super block used for the superblock_dump command
type PoolSuperblock struct {
	PoolUuid             uuid.UUID `json:"uuid"`
	ContCount            int       `json:"cont_count"`
	NvmeSize             uint64    `json:"nvme_size"`
	ScmSize              uint64    `json:"scm_size"`
	TotalBlocks          uint64    `json:"total_blocks"`
	DurableFormatVersion int       `json:"durable_format_version"`
	BlockSize            uint64    `json:"block_size"`
	HdrBlocks            uint64    `json:"hdr_blocks"`
}
