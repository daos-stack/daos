package main

type CommandContext struct {
	CContext          *DdbContext
	jsonOutput        bool
	jsonOutputHandled bool
}

type SuperBlock struct {
	PoolUuid             string `json:"uuid"`
	ContCount            int    `json:"cont_count"`
	NvmeSize             uint64 `json:"nvmeSize"`
	ScmSize              uint64 `json:"scmSize"`
	TotalBlocks          uint64 `json:"total_blocks"`
	DurableFormatVersion int    `json:"durable_format_version"`
	BlockSize            uint64 `json:"block_size"`
	HdrBlocks            uint64 `json:"hdr_blocks"`
}
