//
// (C) Copyright 2022 Intel Corporation.
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
	CollectLog collectLogCmd `command:"collectlog" description:"Collect logs from client"`
}

// collectLogCmd is the struct representing the command to collect the log from client side.
type collectLogCmd struct {
	configCmd
	cmdutil.LogCmd
	ConfigPath string `short:"o" long:"config-path" required:"1" description:"Path to agent configuration file"`
	support.CollectLogSubCmd
}

func (cmd *collectLogCmd) Execute(_ []string) error {
	var LogCollection = map[int32][]string{
		support.CopyAgentConfigEnum:  {""},
		support.CollectAgentLogEnum:  {""},
		support.CollectAgentCmdEnum:  support.AgentCmd,
		support.CollectClientLogEnum: {""},
		support.CollectSystemCmdEnum: support.SystemCmd,
	}

	// Default 3 steps of log/conf collection.
	progress := support.ProgressBar{1, 5, 0, false}

	if cmd.Archive {
		progress.Total++
	}

	// Copy the custom log location
	if cmd.CustomLogs != "" {
		LogCollection[support.CollectCustomLogsEnum] = []string{""}
		progress.Total++
	}

	if cmd.TargetFolder == "" {
		cmd.TargetFolder = "/tmp/daos_support_client_logs"
	}
	cmd.Infof("Support Logs will be copied to %s", cmd.TargetFolder)

	progress.Steps = 100 / progress.Total
	params := support.Params{}
	params.TargetFolder = cmd.TargetFolder
	params.CustomLogs = cmd.CustomLogs
	params.Config = cmd.ConfigPath
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
