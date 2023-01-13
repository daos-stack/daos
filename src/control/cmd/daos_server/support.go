//
// (C) Copyright 2022-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"os"

	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/lib/support"
)

type SupportCmd struct {
	CollectLog collectLogCmd `command:"collectlog" description:"Collect logs from server"`
}

// collectLogCmd is the struct representing the command to collect the Logs/config for support purpose
type collectLogCmd struct {
	cfgCmd
	cmdutil.LogCmd
	support.CollectLogSubCmd
}

func (cmd *collectLogCmd) Execute(_ []string) error {
	var LogCollection = map[int32][]string{
		support.CopyServerConfigEnum:     {""},
		support.CollectSystemCmdEnum:     support.SystemCmd,
		support.CollectServerLogEnum:     support.ServerLog,
		support.CollectDaosServerCmdEnum: support.DaosServerCmd,
	}

	// Default 4 steps of log/conf collection.
	progress := support.ProgressBar{1, 4, 0, false}

	if cmd.Archive {
		progress.Total++
	}

	// Copy the custom log location
	if cmd.CustomLogs != "" {
		LogCollection[support.CollectCustomLogsEnum] = []string{""}
		progress.Total++
	}

	if cmd.TargetFolder == "" {
		cmd.TargetFolder = "/tmp/daos_support_server_logs"
	}
	cmd.Infof("Support logs will be copied to %s", cmd.TargetFolder)

	progress.Steps = 100 / progress.Total
	params := support.Params{}
	params.Config = cmd.configPath()
	params.TargetFolder = cmd.TargetFolder
	params.CustomLogs = cmd.CustomLogs
	for logfunc, logcmdset := range LogCollection {
		for _, logcmd := range logcmdset {
			cmd.Debugf("Log Function Enum = %s -- Log Collect Cmd %s ", logfunc, logcmd)
			params.LogFunction = logfunc
			params.LogCmd = logcmd

			err := support.CollectSupportLog(cmd.Logger, params)
			if err != nil {
				fmt.Println(err)
				if cmd.Stop {
					return err
				}
			}
		}
		fmt.Printf(support.PrintProgress(&progress))
	}

	if cmd.Archive {
		cmd.Debugf("Archiving the Log Folder %s", cmd.TargetFolder)
		err := support.ArchiveLogs(cmd.Logger, params)
		if err != nil {
			return err
		}

		for i := 1; i < 3; i++ {
			os.RemoveAll(cmd.TargetFolder)
		}
	}

	fmt.Printf(support.PrintProgressEnd(&progress))

	return nil
}
