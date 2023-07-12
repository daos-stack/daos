package main

import (
	"encoding/json"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/dustin/go-humanize"
)

func printSuperBlock(log *logging.LeveledLogger, sb *SuperBlock, jsonPrint bool) error {
	if jsonPrint {
		data, err := json.MarshalIndent(struct {
			Response interface{} `json:"response"`
			Error    *string     `json:"error"`
			Status   int         `json:"status"`
		}{sb, nil, 0}, "", "  ")
		if err != nil {
			log.Errorf("unable to marshal json: %s\n", err.Error())
			return err
		}
		log.Infof("%s", data)
	} else {
		log.Infof("Pool UUID: %s\n", sb.PoolUuid)
		log.Infof("Format Version: %d\n", sb.DurableFormatVersion)
		log.Infof("Containers: %d\n", sb.ContCount)
		log.Infof("SCM Size: %s", humanize.Bytes(sb.ScmSize))
		log.Infof("NVME Size: %s", humanize.Bytes(sb.NvmeSize))
		log.Infof("Block Size: %s", humanize.Bytes(sb.BlockSize))
		log.Infof("Reserved Blocks: %d\n", sb.HdrBlocks)
		log.Infof("Block Device Capacity: %s", humanize.Bytes(sb.TotalBlocks))
	}

	return nil
}
