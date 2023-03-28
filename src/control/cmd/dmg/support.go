//
// (C) Copyright 2022-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"bytes"
	"context"
	"fmt"
	"os"
	"os/exec"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/support"
)

// NetCmd is the struct representing the top-level network subcommand.
type supportCmd struct {
	CollectLog collectLogCmd `command:"collect-log" description:"Collect logs from servers"`
}

// collectLogCmd is the struct representing the command to collect the logs from the servers for support purpose
type collectLogCmd struct {
	baseCmd
	cfgCmd
	ctlInvokerCmd
	hostListCmd
	jsonOutputCmd
	support.CollectLogSubCmd
}

// Copy Admin logs to TargetHost (central location) if option is provided.
func (cmd *collectLogCmd) rsyncAdminLog() error {

	runcmd := strings.Join([]string{
		"rsync",
		"-av",
		"--blocking-io",
		cmd.TargetFolder + "/",
		cmd.TargetHost + ":" + cmd.TargetFolder}, " ")

	cmd.Debugf("Rsync Admin Log to TargetHost = %s", runcmd)
	rsyncCmd := exec.Command("sh", "-c", runcmd)
	var stdout, stderr bytes.Buffer
	rsyncCmd.Stdout = &stdout
	rsyncCmd.Stderr = &stderr
	err := rsyncCmd.Run()
	outStr, errStr := string(stdout.Bytes()), string(stderr.Bytes())
	if err != nil {
		return errors.Wrapf(err, "Error running command %s with %s", rsyncCmd, err)
	}
	cmd.Debugf("rsyncCmd:= %s stdout:\n%s\nstderr:\n%s\n", rsyncCmd, outStr, errStr)

	return nil
}

// gRPC call to initiate the rsync and copy the logs to Admin/TargetHost (central location).
func (cmd *collectLogCmd) rsyncLog() error {
	hostName, err := support.GetHostName()
	if err != nil {
		return err
	}

	// Admin host will be the central host to collect all the logs if TargetHost option not provided.
	// In that case only server logs will be rsync to Admin host.
	if cmd.TargetHost == "" {
		cmd.TargetHost = hostName
	} else {
		// initiate the rsync and copy the Admin logs to targetHost
		err = cmd.rsyncAdminLog()
		if err != nil && cmd.Stop {
			return err
		}
	}

	req := &control.CollectLogReq{
		TargetFolder: cmd.TargetFolder,
		TargetHost:   cmd.TargetHost,
		LogFunction:  support.RsyncLogEnum,
	}
	cmd.Debugf("Rsync logs from servers to %s:%s ", cmd.TargetHost, cmd.TargetFolder)
	resp, err := control.CollectLog(context.Background(), cmd.ctlInvoker, req)
	if err != nil && cmd.Stop {
		return err
	}
	if len(resp.GetHostErrors()) > 0 {
		var bld strings.Builder
		if err := pretty.PrintResponseErrors(resp, &bld); err != nil {
			return err
		}
		cmd.Info(bld.String())
		return resp.Errors()
	}

	return nil
}

// gRPC call to Archive the logs on individual servers.
func (cmd *collectLogCmd) archLogsOnServer() error {
	req := &control.CollectLogReq{
		TargetFolder: cmd.TargetFolder,
		TargetHost:   cmd.TargetHost,
		LogFunction:  support.ArchiveLogsEnum,
	}
	cmd.Debugf("Archiving the Log Folder %s", cmd.TargetFolder)
	resp, err := control.CollectLog(context.Background(), cmd.ctlInvoker, req)
	if err != nil && cmd.Stop {
		return err
	}
	if len(resp.GetHostErrors()) > 0 {
		var bld strings.Builder
		if err := pretty.PrintResponseErrors(resp, &bld); err != nil {
			return err
		}
		cmd.Info(bld.String())
		return resp.Errors()
	}

	return nil
}

func (cmd *collectLogCmd) Execute(_ []string) error {
	// Default log collection set
	var LogCollection = map[int32][]string{
		support.CollectSystemCmdEnum:     support.SystemCmd,
		support.CollectServerLogEnum:     support.ServerLog,
		support.CollectDaosServerCmdEnum: support.DaosServerCmd,
		support.CopyServerConfigEnum:     {""},
	}

	// dmg command info collection set
	var DmgInfoCollection = map[int32][]string{
		support.CollectDmgCmdEnum:      support.DmgCmd,
		support.CollectDmgDiskInfoEnum: {""},
	}

	// set of support collection steps to show in progress bar
	progress := support.ProgressBar{
		Total:     len(LogCollection) + len(DmgInfoCollection) + 1, // Extra 1 is for rsync operation.
		NoDisplay: cmd.jsonOutputEnabled(),
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

	// Check if DAOS Management Service is up and running
	params := support.CollectLogsParams{}
	params.Config = cmd.cfgCmd.config.Path
	params.LogFunction = support.CollectDmgCmdEnum
	params.LogCmd = "dmg system query"

	err := support.CollectSupportLog(cmd.Logger, params)

	if err != nil {
		return errors.Wrap(err, "DAOS Management Service is down")
	}

	if cmd.TargetFolder == "" {
		cmd.TargetFolder = "/tmp/daos_support_server_logs"
	}
	cmd.Infof("Support logs will be copied to %s", cmd.TargetFolder)
	if err := os.Mkdir(cmd.TargetFolder, 0700); err != nil && !os.IsExist(err) {
		return err
	}

	// Copy log/config file to TargetFolder on all servers
	for logFunc, logCmdSet := range LogCollection {
		for _, logCmd := range logCmdSet {
			cmd.Debugf("Log Function %d -- Log Collect Cmd %s ", logFunc, logCmd)
			ctx := context.Background()
			req := &control.CollectLogReq{
				TargetFolder: cmd.TargetFolder,
				ExtraLogsDir: cmd.ExtraLogsDir,
				LogFunction:  logFunc,
				LogCmd:       logCmd,
			}
			req.SetHostList(cmd.hostlist)

			resp, err := control.CollectLog(ctx, cmd.ctlInvoker, req)
			if err != nil && cmd.Stop {
				return err
			}
			if len(resp.GetHostErrors()) > 0 {
				var bld strings.Builder
				if err := pretty.PrintResponseErrors(resp, &bld); err != nil {
					return err
				}
				cmd.Info(bld.String())
				if cmd.Stop {
					return resp.Errors()
				}
			}
		}
		fmt.Printf(progress.Display())
	}

	// Run dmg command info collection set
	params = support.CollectLogsParams{}
	params.Config = cmd.cfgCmd.config.Path
	params.TargetFolder = cmd.TargetFolder
	params.ExtraLogsDir = cmd.ExtraLogsDir
	params.JsonOutput = cmd.jsonOutputEnabled()
	params.Hostlist = strings.Join(cmd.hostlist, " ")
	for logFunc, logCmdSet := range DmgInfoCollection {
		for _, logCmd := range logCmdSet {
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

	// Rsync the logs from servers
	rsyncerr := cmd.rsyncLog()
	fmt.Printf(progress.Display())
	if rsyncerr != nil && cmd.Stop {
		return rsyncerr
	}

	// Archive the logs
	if cmd.Archive {
		// Archive the logs on Admin Node
		cmd.Debugf("Archiving the Log Folder on Admin Node%s", cmd.TargetFolder)
		err := support.ArchiveLogs(cmd.Logger, params)
		if err != nil && cmd.Stop {
			return err
		}

		// Archive the logs on Server node via gRPC in case of rsync failure and logs can not be
		// copied to central/Admin node.
		if rsyncerr != nil {
			err = cmd.archLogsOnServer()
			if err != nil && cmd.Stop {
				return err
			}
		}
		fmt.Printf(progress.Display())
	}

	fmt.Printf(progress.Display())

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(nil, err)
	}

	return nil
}
