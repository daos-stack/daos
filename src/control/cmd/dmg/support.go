//
// (C) Copyright 2022-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"time"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/support"
)

// supportCmd is the struct representing the top-level support subcommand.
type supportCmd struct {
	CollectLog collectLogCmd `command:"collect-log" description:"Collect logs from servers"`
}

// collectLogCmd is the struct representing the command to collect the Logs/config for support purpose
type collectLogCmd struct {
	baseCmd
	cfgCmd
	ctlInvokerCmd
	hostListCmd
	cmdutil.JSONOutputCmd
	support.CollectLogSubCmd
	bld strings.Builder
	support.LogTypeSubCmd
}

// gRPC call to initiate the rsync and copy the logs to Admin (central location).
func (cmd *collectLogCmd) rsyncLog() error {
	hostName, err := support.GetHostName()
	if err != nil {
		return err
	}

	req := &control.CollectLogReq{
		TargetFolder:         cmd.TargetFolder,
		AdminNode:            hostName,
		LogFunction:          support.RsyncLogEnum,
		FileTransferExecArgs: cmd.FileTransferExecArgs,
	}
	cmd.Debugf("Rsync logs from servers to %s:%s ", hostName, cmd.TargetFolder)
	resp, err := control.CollectLog(cmd.MustLogCtx(), cmd.ctlInvoker, req)
	if err != nil && cmd.StopOnError {
		return err
	}
	if len(resp.GetHostErrors()) > 0 {
		if err := pretty.UpdateErrorSummary(resp, "rsync", &cmd.bld); err != nil {
			return err
		}
		return resp.Errors()
	}

	return nil
}

// gRPC call to Archive the logs on individual servers.
func (cmd *collectLogCmd) archLogsOnServer() error {
	hostName, err := support.GetHostName()
	if err != nil {
		return err
	}

	req := &control.CollectLogReq{
		TargetFolder: cmd.TargetFolder,
		AdminNode:    hostName,
		LogFunction:  support.ArchiveLogsEnum,
	}
	cmd.Debugf("Archiving the Log Folder %s", cmd.TargetFolder)
	resp, err := control.CollectLog(cmd.MustLogCtx(), cmd.ctlInvoker, req)
	if err != nil && cmd.StopOnError {
		return err
	}
	if len(resp.GetHostErrors()) > 0 {
		if err := pretty.UpdateErrorSummary(resp, "archive", &cmd.bld); err != nil {
			return err
		}
		return resp.Errors()
	}

	return nil
}

// Execute is run when supportCmd activates.
func (cmd *collectLogCmd) Execute(_ []string) error {
	// Default log collection set
	var LogCollection = map[int32][]string{}
	var DmgInfoCollection = map[int32][]string{}

	err := cmd.DateTimeValidate()
	if err != nil {
		return err
	}

	// Only collect the specific logs Admin,Control or Engine.
	// This will ignore the system information collection.
	if cmd.LogType != "" {
		LogCollection[support.CollectServerLogEnum], err = cmd.LogTypeValidate()
		if err != nil {
			return err
		}
	} else {
		// Default collect everything from servers
		LogCollection[support.CollectSystemCmdEnum] = support.SystemCmd
		LogCollection[support.CollectDaosServerCmdEnum] = support.DaosServerCmd
		LogCollection[support.CopyServerConfigEnum] = []string{""}
		LogCollection[support.CollectServerLogEnum], err = cmd.LogTypeValidate()
		if err != nil {
			return err
		}

		// dmg command info collection set
		DmgInfoCollection[support.CollectDmgCmdEnum] = support.DmgCmd
		DmgInfoCollection[support.CollectDmgDiskInfoEnum] = []string{""}
	}

	// set of support collection steps to show in progress bar
	progress := support.ProgressBar{
		Total:     len(LogCollection) + len(DmgInfoCollection) + 1, // Extra 1 is for rsync operation.
		NoDisplay: cmd.JSONOutputEnabled(),
	}

	// Add custom log location
	if cmd.ExtraLogsDir != "" {
		LogCollection[support.CollectExtraLogsDirEnum] = []string{""}
		progress.Total++
	}

	// Increase progress counter for Archive if enabled
	if cmd.Archive {
		progress.Total++
	}
	progress.Steps = 100 / progress.Total

	// Default TargetFolder location where logs will be copied.
	// Included Date and time stamp to the log folder.
	if cmd.TargetFolder == "" {
		folderName := fmt.Sprintf("daos_support_server_logs_%s", time.Now().Format(time.RFC3339))
		cmd.TargetFolder = filepath.Join(os.TempDir(), folderName)
	}
	cmd.Infof("Support logs will be copied to %s", cmd.TargetFolder)
	if err := os.Mkdir(cmd.TargetFolder, 0700); err != nil && !os.IsExist(err) {
		return err
	}

	// Check if DAOS Management Service is up and running
	params := support.CollectLogsParams{}
	params.Config = cmd.cfgCmd.config.Path
	params.LogFunction = support.CollectDmgCmdEnum
	params.TargetFolder = cmd.TargetFolder
	params.LogCmd = "dmg system query"
	if cmd.cfgCmd.config.TransportConfig.AllowInsecure {
		params.LogCmd += " -i"
	}

	err = support.CollectSupportLog(cmd.Logger, params)

	if err != nil {
		return err
	}

	// Copy log/config file to TargetFolder on all servers
	for logFunc, logCmdSet := range LogCollection {
		for _, logCmd := range logCmdSet {
			cmd.Debugf("Log Function %d -- Log Collect Cmd %s ", logFunc, logCmd)
			ctx := cmd.MustLogCtx()
			req := &control.CollectLogReq{
				TargetFolder:         cmd.TargetFolder,
				ExtraLogsDir:         cmd.ExtraLogsDir,
				LogFunction:          logFunc,
				LogCmd:               logCmd,
				LogStartDate:         cmd.LogStartDate,
				LogEndDate:           cmd.LogEndDate,
				LogStartTime:         cmd.LogStartTime,
				LogEndTime:           cmd.LogEndTime,
				StopOnError:          cmd.StopOnError,
				FileTransferExecArgs: cmd.FileTransferExecArgs,
			}
			req.SetHostList(cmd.hostlist)

			resp, err := control.CollectLog(ctx, cmd.ctlInvoker, req)
			if err != nil && cmd.StopOnError {
				return err
			}
			if len(resp.GetHostErrors()) > 0 {
				if err := pretty.UpdateErrorSummary(resp, logCmd, &cmd.bld); err != nil {
					return err
				}

				if cmd.StopOnError {
					return resp.Errors()
				}
			}
		}
		fmt.Print(progress.Display())
	}

	// Run dmg command info collection set
	params = support.CollectLogsParams{}
	params.Config = cmd.cfgCmd.config.Path
	params.TargetFolder = cmd.TargetFolder
	params.ExtraLogsDir = cmd.ExtraLogsDir
	params.JsonOutput = cmd.JSONOutputEnabled()
	params.Hostlist = strings.Join(cmd.hostlist, " ")
	for logFunc, logCmdSet := range DmgInfoCollection {
		for _, logCmd := range logCmdSet {
			params.LogFunction = logFunc
			params.LogCmd = logCmd

			err := support.CollectSupportLog(cmd.Logger, params)
			if err != nil {
				if cmd.StopOnError {
					return err
				}
			}
		}
		fmt.Print(progress.Display())
	}

	params.FileTransferExecArgs = cmd.FileTransferExecArgs
	// R sync the logs from servers
	rsyncerr := cmd.rsyncLog()
	fmt.Print(progress.Display())
	if rsyncerr != nil && cmd.StopOnError {
		return rsyncerr
	}

	// Archive the logs
	if cmd.Archive {
		// Archive the logs on Admin Node
		cmd.Debugf("Archiving the Log Folder on Admin Node%s", cmd.TargetFolder)
		err := support.ArchiveLogs(cmd.Logger, params)
		if err != nil && cmd.StopOnError {
			return err
		}

		// Archive the logs on Server node via gRPC in case of rsync failure and logs can not be
		// copied to central/Admin node.
		if rsyncerr != nil {
			err = cmd.archLogsOnServer()
			if err != nil && cmd.StopOnError {
				return err
			}
		}
		fmt.Print(progress.Display())
	}

	fmt.Print(progress.Display())

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(nil, err)
	}

	// Print the support command summary.
	if len(cmd.bld.String()) == 0 {
		fmt.Println("Summary : All Commands Successfully Executed")
	} else {
		fmt.Println("Summary :")
		cmd.Info(cmd.bld.String())
	}

	return nil
}
