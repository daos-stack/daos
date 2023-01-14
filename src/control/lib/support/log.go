//
// (C) Copyright 2022-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package support

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"os/exec"
	"path/filepath"
	"strings"

	"github.com/pkg/errors"
	"gopkg.in/yaml.v2"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/lib/hardware/hwprov"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/config"
)

const (
	CopyServerConfigEnum int32 = iota
	CollectSystemCmdEnum
	CollectServerLogEnum
	CollectExtraLogsDirEnum
	CollectDaosServerCmdEnum
	CollectDmgCmdEnum
	CollectDmgDiskInfoEnum
	CollectAgentCmdEnum
	CollectClientLogEnum
	CollectAgentLogEnum
	CopyAgentConfigEnum
	RsyncLogEnum
)

type CollectLogSubCmd struct {
	Stop         bool   `short:"s" long:"stop-on-error" description:"Stop the collect-log command on very first error"`
	TargetFolder string `short:"t" long:"target" description:"Target Folder location where log will be copied"`
	Archive      bool   `short:"z" long:"archive" description:"Archive the log/config files"`
	ExtraLogsDir string `short:"c" long:"extra-logs-dir" description:"Collect the Logs from given directory"`
}

// Folder names to copy logs and configs
const (
	dmgSystemLogs    = "DmgSystemLogs"    // Copy the dmg command output for DAOS system
	dmgNodeLogs      = "DmgNodeLogs"      // Copy the dmg command output specific to the storage.
	daosAgentCmdInfo = "DaosAgentCmdInfo" // Copy the daos_agent command output specific to the node.
	genSystemInfo    = "GenSystemInfo"    // Copy the system related information
	serverLogs       = "ServerLogs"       // Copy the server/control and helper logs
	clientLogs       = "ClientLogs"       // Copy the server/control and helper logs
	DaosServerConfig = "DaosServerConfig" // Copy the server config
	agentConfig      = "AgentConfig"      // Copy the Agent config
	agentLogs        = "AgentLogs"        // Copy the Agent log
	extraLogs        = "ExtraLogs"        // Copy the Custom logs
)

const DmgListDeviceCmd = "dmg storage query list-devices"
const DmgDeviceHealthCmd = "dmg storage query device-health"

var DmgCmd = []string{
	"dmg system get-prop",
	"dmg system query",
	"dmg system list-pools",
	"dmg system leader-query",
	"dmg system get-attr",
	"dmg network scan",
	"dmg storage scan",
	"dmg storage scan -n",
	"dmg storage scan -m",
	"dmg storage query list-pools -v",
	"dmg storage query usage",
}

var AgentCmd = []string{
	"daos_agent version",
	"daos_agent net-scan",
	"daos_agent dump-topology",
}

var SystemCmd = []string{
	//"iperf3 --help",
	"dmesg",
	"lspci -D",
	"top -bcn1 -w512",
}

var ServerLog = []string{
	"EngineLog",
	"ControlLog",
	"HelperLog",
}

var DaosServerCmd = []string{
	"daos_server version",
	"daos_metrics",
	"dump-topology",
}

type ProgressBar struct {
	Start     int  // start int number
	Total     int  // end int number
	Steps     int  // Int number be increased per steps
	NoDisplay bool // Option to skip progress bar if Json output is enabled
}

type CollectLogsParams struct {
	Config       string
	Hostlist     string
	TargetFolder string
	ExtraLogsDir string
	JsonOutput   bool
	LogFunction  int32
	LogCmd       string
}

type logCopy struct {
	cmd    string
	option string
}

// Print the progress bar during log collect command
func PrintProgress(progBar *ProgressBar) string {
	if !(progBar.NoDisplay) {
		progBar.Start = progBar.Start + 1
		printString := fmt.Sprintf("\r[%-100s] %8d/%d", strings.Repeat("=", progBar.Steps*progBar.Start), progBar.Start, progBar.Total)
		return printString
	}

	return ""
}

// Print the progress End once the log collection completed.
func PrintProgressEnd(progBar *ProgressBar) string {
	if !(progBar.NoDisplay) {
		return fmt.Sprintf("\r[%-100s] %8d/%d\n", strings.Repeat("=", 100), progBar.Total, progBar.Total)
	}

	return ""
}

// Check if daos_engine process is running and return the bool value accordingly.
func checkEngineState(log logging.Logger) (bool, error) {
	_, err := exec.Command("bash", "-c", "pidof daos_engine").Output()
	if err != nil {
		return false, errors.Wrap(err, "daos_engine is not running on server")
	}

	return true, nil
}

// Get the server config from the running daos engine
func getRunningConf(log logging.Logger) (string, error) {
	running_config := ""
	runState, err := checkEngineState(log)
	if err != nil {
		return "", err
	}

	if runState {
		cmd := "ps -eo args | grep daos_engine | head -n 1 | grep -oP '(?<=-d )[^ ]*'"
		stdout, err := exec.Command("bash", "-c", cmd).Output()
		if err != nil {
			return "", errors.Wrap(err, "daos_engine is not running on server")
		}
		running_config = filepath.Join(strings.TrimSpace(string(stdout)), config.ConfigOut)
	}

	return running_config, nil
}

// Get the server config, either from the running daos engine or default
func getServerConf(log logging.Logger, opts ...CollectLogsParams) (string, error) {
	cfgPath, err := getRunningConf(log)

	if err != nil {
		return "", err
	}

	if cfgPath == "" {
		cfgPath = filepath.Join(config.DefaultServer().SocketDir, config.ConfigOut)
	}

	log.Debugf(" -- Server Config File is %s", cfgPath)
	return cfgPath, nil
}

// Copy file from source to destination
func cpLogFile(src, dst string, log logging.Logger) error {
	log_file_name := filepath.Base(src)
	log.Debugf(" -- Copy File %s to %s\n", log_file_name, dst)

	err := common.CpFile(src, filepath.Join(dst, log_file_name))
	if err != nil {
		return errors.Wrap(err, "unable to Copy File")
	}

	return nil
}

// Create the local folder on each servers
func createFolder(target string, log logging.Logger) error {
	// Create the folder if it's not exist
	if _, err := os.Stat(target); os.IsNotExist(err) {
		log.Debugf("Log folder is not Exists, so creating %s", target)

		if err := os.MkdirAll(target, 0777); err != nil && !os.IsExist(err) {
			return errors.Wrapf(err, "failed to create log directory %s", target)
		}
	}

	return nil
}

// Get the system hostname
func GetHostName() (string, error) {
	hn, err := exec.Command("hostname", "-s").Output()
	if err != nil {
		return "", errors.Wrapf(err, "Error running hostname -s command %s", hn)
	}
	out := strings.Split(string(hn), "\n")

	return out[0], nil
}

// Copy Command output to the file
func cpOutputToFile(target string, log logging.Logger, cp ...logCopy) (string, error) {
	// Run command and copy output to the file
	// executing as sub shell enables pipes in cmd string
	runCmd := strings.Join([]string{cp[0].cmd, cp[0].option}, " ")
	out, err := exec.Command("sh", "-c", runCmd).CombinedOutput()
	if err != nil {
		return "", errors.New(string(out))
	}

	log.Debugf("Collecting DAOS command output = %s > %s ", runCmd, target)
	cmd := strings.ReplaceAll(cp[0].cmd, " -", "_")
	cmd = strings.ReplaceAll(cmd, " ", "_")
	if err := ioutil.WriteFile(filepath.Join(target, cmd), out, 0644); err != nil {
		return "", errors.Wrapf(err, "failed to write %s", filepath.Join(target, cmd))
	}

	return string(out), nil
}

// Create the Archive of logs.
func ArchiveLogs(log logging.Logger, opts ...CollectLogsParams) error {
	var buf bytes.Buffer
	err := common.FolderCompress(opts[0].TargetFolder, &buf)
	if err != nil {
		return err
	}

	// write to the the .tar.gzip
	tarFileName := fmt.Sprintf("%s.tar.gz", opts[0].TargetFolder)
	log.Debugf("Archiving the log folder %s", tarFileName)
	fileToWrite, err := os.OpenFile(tarFileName, os.O_CREATE|os.O_RDWR, os.FileMode(0755))
	if err != nil {
		return err
	}
	defer fileToWrite.Close()

	_, err = io.Copy(fileToWrite, &buf)
	if err != nil {
		return err
	}

	return nil
}

// Create the individual folder on each server based on hostname
func createHostFolder(dst string, log logging.Logger) (string, error) {
	hn, err := GetHostName()
	if err != nil {
		return "", err
	}

	targetLocation := filepath.Join(dst, hn)
	err = createFolder(targetLocation, log)
	if err != nil {
		return "", err
	}

	return targetLocation, nil
}

// Create the individual log folder on each server
func createHostLogFolder(dst string, log logging.Logger, opts ...CollectLogsParams) (string, error) {
	targetLocation, err := createHostFolder(opts[0].TargetFolder, log)
	if err != nil {
		return "", err
	}

	targetDst := filepath.Join(targetLocation, dst)
	err = createFolder(targetDst, log)
	if err != nil {
		return "", err
	}

	return targetDst, nil

}

// Get all the servers name from the dmg query
func getSysNameFromQuery(configPath string, log logging.Logger) ([]string, error) {
	var hostNames []string

	dName, err := exec.Command("sh", "-c", "domainname").Output()
	if err != nil {
		return nil, errors.Wrapf(err, "Error running command domainname with %s", dName)
	}
	domainName := strings.Split(string(dName), "\n")

	cmd := strings.Join([]string{"dmg", "system", "query", "-v", "-o", configPath}, " ")
	out, err := exec.Command("sh", "-c", cmd).Output()
	if err != nil {
		return nil, errors.Wrapf(err, "Error running command %s with %s", cmd, out)
	}
	temp := strings.Split(string(out), "\n")

	if len(temp) > 0 {
		for _, hn := range temp[2 : len(temp)-2] {
			hn = strings.ReplaceAll(strings.Fields(hn)[3][1:], domainName[0], "")
			hn = strings.TrimSuffix(hn, ".")
			hostNames = append(hostNames, hn)
		}
	} else {
		return nil, errors.Wrapf(err, "No system found for command %s", cmd)
	}

	return hostNames, nil
}

// Rsync the logs from individual servers to Admin node
func rsyncLog(log logging.Logger, opts ...CollectLogsParams) error {
	targetLocation, err := createHostFolder(opts[0].TargetFolder, log)
	if err != nil {
		return err
	}

	cmd := strings.Join([]string{
		"rsync",
		"-av",
		"--blocking-io",
		"--remove-source-files",
		targetLocation,
		opts[0].LogCmd + ":" + opts[0].TargetFolder}, " ")

	rsyncCmd := exec.Command("sh", "-c", cmd)
	var stdout, stderr bytes.Buffer
	rsyncCmd.Stdout = &stdout
	rsyncCmd.Stderr = &stderr
	err = rsyncCmd.Run()
	outStr, errStr := string(stdout.Bytes()), string(stderr.Bytes())
	if err != nil {
		return errors.Wrapf(err, "Error running command %s with %s", rsyncCmd, err)
	}
	log.Infof("rsyncCmd:= %s stdout:\n%s\nstderr:\n%s\n", rsyncCmd, outStr, errStr)

	return nil
}

// Collect the custom log folder
func collectExtraLogsDir(log logging.Logger, opts ...CollectLogsParams) error {
	log.Infof("Log will be collected from custom location %s", opts[0].ExtraLogsDir)

	hn, err := GetHostName()
	if err != nil {
		return err
	}

	customLogFolder := filepath.Join(opts[0].TargetFolder, hn, extraLogs)
	err = createFolder(customLogFolder, log)
	if err != nil {
		return err
	}

	err = common.CpDir(opts[0].ExtraLogsDir, customLogFolder)
	if err != nil {
		return err
	}

	return nil
}

// Collect the disk info using dmg command from each server.
func collectDmgDiskInfo(log logging.Logger, opts ...CollectLogsParams) error {
	var hostNames []string
	var output string

	hostNames, err := getSysNameFromQuery(opts[0].Config, log)
	if err != nil {
		return err
	}
	if len(opts[0].Hostlist) > 0 {
		hostNames = strings.Fields(opts[0].Hostlist)
	}

	for _, hostName := range hostNames {
		// Copy all the devices information for each server
		dmg := logCopy{}
		dmg.cmd = DmgListDeviceCmd
		dmg.option = strings.Join([]string{"-o", opts[0].Config, "-l", hostName}, " ")
		targetDmgLog := filepath.Join(opts[0].TargetFolder, hostName, dmgNodeLogs)

		// Create the Folder.
		err := createFolder(targetDmgLog, log)
		if err != nil {
			return err
		}

		output, err = cpOutputToFile(targetDmgLog, log, dmg)
		if err != nil {
			return err
		}

		// Get each device health information from each server
		for _, v1 := range strings.Split(output, "\n") {
			if strings.Contains(v1, "UUID") {
				device := strings.Fields(v1)[0][5:]
				health := logCopy{}
				health.cmd = strings.Join([]string{DmgDeviceHealthCmd, "-u", device}, " ")
				health.option = strings.Join([]string{"-l", hostName, "-o", opts[0].Config}, " ")
				_, err = cpOutputToFile(targetDmgLog, log, health)
				if err != nil {
					return err
				}
			}
		}
	}

	return nil
}

// Run command and copy the output to file.
func collectCmdOutput(folderName string, log logging.Logger, opts ...CollectLogsParams) error {
	nodeLocation, err := createHostLogFolder(folderName, log, opts...)
	if err != nil {
		return err
	}

	agent := logCopy{}
	agent.cmd = opts[0].LogCmd
	_, err = cpOutputToFile(nodeLocation, log, agent)
	if err != nil {
		return err
	}

	return nil
}

// Collect client side log
func collectClientLog(log logging.Logger, opts ...CollectLogsParams) error {
	clientLogFile := os.Getenv("D_LOG_FILE")
	if clientLogFile != "" {
		clientLogLocation, err := createHostLogFolder(clientLogs, log, opts...)
		if err != nil {
			return err
		}

		matches, _ := filepath.Glob(clientLogFile + "*")
		for _, logfile := range matches {
			err := cpLogFile(logfile, clientLogLocation, log)
			if err != nil {
				return err
			}
		}
	}

	return nil
}

// Collect Agent log
func collectAgentLog(log logging.Logger, opts ...CollectLogsParams) error {
	// Create the individual folder on each client
	targetAgentLog, err := createHostLogFolder(agentLogs, log, opts...)
	if err != nil {
		return err
	}

	agentFile, err := ioutil.ReadFile(opts[0].Config)
	if err != nil {
		return err
	}

	data := make(map[interface{}]interface{})
	err = yaml.Unmarshal(agentFile, &data)
	if err != nil {
		return err
	}

	err = cpLogFile(fmt.Sprintf("%s", data["log_file"]), targetAgentLog, log)
	if err != nil {
		return err
	}

	return nil
}

// Copy Agent config file.
func copyAgentConfig(log logging.Logger, opts ...CollectLogsParams) error {
	// Create the individual folder on each client
	targetConfig, err := createHostLogFolder(agentConfig, log, opts...)
	if err != nil {
		return err
	}

	err = cpLogFile(opts[0].Config, targetConfig, log)
	if err != nil {
		return err
	}

	return nil
}

// Collect the output of all dmg command and copy into individual file.
func collectDmgCmd(log logging.Logger, opts ...CollectLogsParams) error {
	targetDmgLog := filepath.Join(opts[0].TargetFolder, dmgSystemLogs)
	err := createFolder(targetDmgLog, log)
	if err != nil {
		return err
	}

	dmg := logCopy{}
	dmg.cmd = opts[0].LogCmd
	dmg.option = strings.Join([]string{"-o", opts[0].Config}, " ")

	if opts[0].JsonOutput {
		dmg.option = strings.Join([]string{dmg.option, "-j"}, " ")
	}

	_, err = cpOutputToFile(targetDmgLog, log, dmg)
	if err != nil {
		return err
	}

	return nil
}

// Copy server config file.
func copyServerConfig(log logging.Logger, opts ...CollectLogsParams) error {
	cfgPath, err := getServerConf(log, opts...)

	serverConfig := config.DefaultServer()
	serverConfig.SetPath(cfgPath)
	serverConfig.Load()
	// Create the individual folder on each server
	targetConfig, err := createHostLogFolder(DaosServerConfig, log, opts...)
	if err != nil {
		return err
	}

	err = cpLogFile(cfgPath, targetConfig, log)
	if err != nil {
		return err
	}

	// Rename the file if it's hidden
	result := common.IsHidden(filepath.Base(cfgPath))
	if result {
		hiddenConf := filepath.Join(targetConfig, filepath.Base(cfgPath))
		nonhiddenConf := filepath.Join(targetConfig, filepath.Base(cfgPath)[1:])
		os.Rename(hiddenConf, nonhiddenConf)
	}

	return nil
}

// Collect all server side logs
func collectServerLog(log logging.Logger, opts ...CollectLogsParams) error {
	var cfgPath string

	if opts[0].Config != "" {
		cfgPath = opts[0].Config
	} else {
		cfgPath, _ = getServerConf(log)
	}
	serverConfig := config.DefaultServer()
	serverConfig.SetPath(cfgPath)
	serverConfig.Load()

	targetServerLogs, err := createHostLogFolder(serverLogs, log, opts...)
	if err != nil {
		return err
	}

	switch opts[0].LogCmd {
	case "EngineLog":
		for i := range serverConfig.Engines {
			matches, _ := filepath.Glob(serverConfig.Engines[i].LogFile + "*")
			for _, logfile := range matches {
				err = cpLogFile(logfile, targetServerLogs, log)
				if err != nil {
					return err
				}
			}
		}
	case "ControlLog":
		err = cpLogFile(serverConfig.ControlLogFile, targetServerLogs, log)
		if err != nil {
			return err
		}
	case "HelperLog":
		err = cpLogFile(serverConfig.HelperLogFile, targetServerLogs, log)
		if err != nil {
			return err
		}
	}

	return nil
}

// Collect daos metrics.
func collectDaosMetrics(daosNodeLocation string, log logging.Logger, opts ...CollectLogsParams) error {
	engineRunState, err := checkEngineState(log)
	if err != nil {
		return err
	}

	if engineRunState {
		daos := logCopy{}
		var cfgPath string
		if opts[0].Config != "" {
			cfgPath = opts[0].Config
		} else {
			cfgPath, _ = getServerConf(log)
		}
		serverConfig := config.DefaultServer()
		serverConfig.SetPath(cfgPath)
		serverConfig.Load()

		for i := range serverConfig.Engines {
			engineId := fmt.Sprintf("%d", i)
			daos.cmd = strings.Join([]string{"daos_metrics", "-S", engineId}, " ")

			_, err := cpOutputToFile(daosNodeLocation, log, daos)
			if err != nil {
				return err
			}
		}
	} else {
		return errors.New("-- FAIL -- Daos Engine is not Running, so daos_metrics will not be collected")
	}

	return nil
}

// Collect output of system side option of daos_server command.
func collectDaosServerCmd(log logging.Logger, opts ...CollectLogsParams) error {
	daosNodeLocation, err := createHostLogFolder(dmgNodeLogs, log, opts...)
	if err != nil {
		return err
	}

	switch opts[0].LogCmd {
	case "daos_metrics":
		err = collectDaosMetrics(daosNodeLocation, log, opts...)
		if err != nil {
			return err
		}
	case "dump-topology":
		hwlog := logging.NewCommandLineLogger()
		hwProv := hwprov.DefaultTopologyProvider(hwlog)
		topo, err := hwProv.GetTopology(context.Background())
		if err != nil {
			return err
		}
		f, err := os.Create(filepath.Join(daosNodeLocation, "daos_server_dump-topology"))
		if err != nil {
			return err
		}
		defer f.Close()
		hardware.PrintTopology(topo, f)
	}

	return nil
}

// Common Entry/Exit point function.
func CollectSupportLog(log logging.Logger, opts ...CollectLogsParams) error {
	switch opts[0].LogFunction {
	case CopyServerConfigEnum:
		return copyServerConfig(log, opts...)
	case CollectSystemCmdEnum:
		return collectCmdOutput(genSystemInfo, log, opts...)
	case CollectServerLogEnum:
		return collectServerLog(log, opts...)
	case CollectExtraLogsDirEnum:
		return collectExtraLogsDir(log, opts...)
	case CollectDaosServerCmdEnum:
		return collectDaosServerCmd(log, opts...)
	case CollectDmgCmdEnum:
		return collectDmgCmd(log, opts...)
	case CollectDmgDiskInfoEnum:
		return collectDmgDiskInfo(log, opts...)
	case CollectAgentCmdEnum:
		return collectCmdOutput(daosAgentCmdInfo, log, opts...)
	case CollectClientLogEnum:
		return collectClientLog(log, opts...)
	case CollectAgentLogEnum:
		return collectAgentLog(log, opts...)
	case CopyAgentConfigEnum:
		return copyAgentConfig(log, opts...)
	case RsyncLogEnum:
		return rsyncLog(log, opts...)
	}

	return nil
}
