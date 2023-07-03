//
// (C) Copyright 2022-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"os"
	"path/filepath"

	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/lib/support"
)

// supportCmd is the struct representing the top-level support subcommand.
type supportCmd struct {
	CollectLog collectLogCmd `command:"collect-log" description:"Collect logs from server"`
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
	progress := support.ProgressBar{
		Total:     len(LogCollection),
		NoDisplay: false,
	}

	if cmd.Archive {
		progress.Total++
	}

	// Copy custom log folder
	if cmd.ExtraLogsDir != "" {
		LogCollection[support.CollectExtraLogsDirEnum] = []string{""}
		progress.Total++
	}

	if cmd.TargetFolder == "" {
		cmd.TargetFolder = filepath.Join(os.TempDir(), "daos_support_server_logs")
	}
	cmd.Infof("Support logs will be copied to %s", cmd.TargetFolder)

	progress.Steps = 100 / progress.Total
	params := support.CollectLogsParams{}
	params.Config = cmd.configPath()
	params.TargetFolder = cmd.TargetFolder
	params.ExtraLogsDir = cmd.ExtraLogsDir
	for logFunc, logCmdSet := range LogCollection {
		for _, logCmd := range logCmdSet {
			cmd.Debugf("Log Function Enum = %d -- Log Collect Cmd = %s ", logFunc, logCmd)
			params.LogFunction = logFunc
			params.LogCmd = logCmd

			err := support.CollectSupportLog(cmd.Logger, params)
			if err != nil {
				fmt.Println(err)
				if cmd.Stop {
					return err
				}
			}
		}
		fmt.Printf(progress.Display())
	}

	if cmd.Archive {
		cmd.Debugf("Archiving the Log Folder %s", cmd.TargetFolder)
		err := support.ArchiveLogs(cmd.Logger, params)
		if err != nil {
			return err
		}

		// FIXME: DAOS-13290 Workaround for files held open
		for i := 1; i < 3; i++ {
			os.RemoveAll(cmd.TargetFolder)
		}
	}

	fmt.Printf(progress.Display())

	return nil
}
