//
// (C) Copyright 2022-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package support

import (
	"bufio"
	"bytes"
	"context"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"
	"time"

	"github.com/pkg/errors"
	"gopkg.in/yaml.v2"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/lib/hardware/defaults/topology"
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
	ArchiveLogsEnum
)

type CollectLogSubCmd struct {
	StopOnError          bool   `short:"s" long:"stop-on-error" description:"Stop the collect-log command on very first error"`
	TargetFolder         string `short:"t" long:"target-folder" description:"Target Folder location where log will be copied"`
	Archive              bool   `short:"z" long:"archive" description:"Archive the log/config files"`
	ExtraLogsDir         string `short:"c" long:"extra-logs-dir" description:"Collect the Logs from given directory"`
	LogStartDate         string `short:"D" long:"start-date" description:"Specify the start date, the day from log will be collected, Format: MM-DD"`
	LogEndDate           string `short:"F" long:"end-date" description:"Specify the end date, the day till the log will be collected, Format: MM-DD"`
	LogStartTime         string `short:"S" long:"log-start-time" description:"Specify the log collection start time, Format: HH:MM:SS"`
	LogEndTime           string `short:"E" long:"log-end-time" description:"Specify the log collection end time, Format: HH:MM:SS"`
	FileTransferExecArgs string `short:"T" long:"transfer-args" description:"Extra arguments for alternate file transfer tool"`
}

type LogTypeSubCmd struct {
	LogType string `short:"e" long:"log-type" description:"collect specific logs only admin,control,server and ignore everything else"`
}

const (
	MMDDYYYY        = "1-2-2006"
	HHMMSS          = "15:4:5"
	MMDDHHMMSS      = "1/2-15:4:5"
	MMDDYYYY_HHMMSS = "1-2-2006 15:4:5"
	YYYYMMDD_HHMMSS = "2006/1/2 15:4:5"
)

// Folder names to copy logs and configs
const (
	dmgSystemLogs    = "DmgSystemLogs"    // Copy the dmg command output for DAOS system
	dmgNodeLogs      = "DmgNodeLogs"      // Copy the dmg command output specific to the server.
	daosAgentCmdInfo = "DaosAgentCmdInfo" // Copy the daos_agent command output specific to the node.
	genSystemInfo    = "GenSystemInfo"    // Copy the system related information
	engineLogs       = "EngineLogs"       // Copy the engine logs
	controlLogs      = "ControlLogs"      // Copy the control logs
	adminLogs        = "AdminLogs"        // Copy the helper logs
	clientLogs       = "ClientLogs"       // Copy the DAOS client logs
	DaosServerConfig = "DaosServerConfig" // Copy the server config
	agentConfig      = "AgentConfig"      // Copy the Agent config
	agentLogs        = "AgentLogs"        // Copy the Agent log
	extraLogs        = "ExtraLogs"        // Copy the Custom logs
)

const DmgListDeviceCmd = "dmg storage query list-devices"
const DmgDeviceHealthCmd = "dmg storage query list-devices --health"

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
	"dmesg",
	"df -h",
	"mount",
	"ps axf",
	"top -bcn1 -w512",
	"lspci -D",
	"sysctl -a",
	"printenv",
	"rpm -qa --qf '(%{INSTALLTIME:date}): %{NAME}-%{VERSION}\n'",
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
	Steps     int  // number to be increased per step
	NoDisplay bool // Option to skip progress bar if Json output is enabled
}

type CollectLogsParams struct {
	Config               string
	Hostlist             string
	TargetFolder         string
	AdminNode            string
	ExtraLogsDir         string
	JsonOutput           bool
	LogFunction          int32
	LogCmd               string
	LogStartDate         string
	LogEndDate           string
	LogStartTime         string
	LogEndTime           string
	StopOnError          bool
	FileTransferExecArgs string
}

type logCopy struct {
	cmd    string
	option string
}

// Verify if the date and time argument is valid and return error if it's invalid
func (cmd *CollectLogSubCmd) DateTimeValidate() error {
	if cmd.LogStartDate != "" || cmd.LogEndDate != "" {
		startDate, err := time.Parse(MMDDYYYY, cmd.LogStartDate)
		if err != nil {
			return errors.New("Invalid date, please provide the startDate in MM-DD-YYYY format")
		}

		endDate, err := time.Parse(MMDDYYYY, cmd.LogEndDate)
		if err != nil {
			return errors.New("Invalid date, please provide the endDate in MM-DD-YYYY format")
		}

		if startDate.After(endDate) {
			return errors.New("start-date can not be after end-date")
		}
	}

	if cmd.LogStartTime != "" {
		_, err := time.Parse(HHMMSS, cmd.LogStartTime)
		if err != nil {
			return errors.New("Invalid log-start-time, please provide the time in HH:MM:SS format")
		}
	}

	if cmd.LogEndTime != "" {
		_, err := time.Parse(HHMMSS, cmd.LogEndTime)
		if err != nil {
			return errors.New("Invalid log-end-time, please provide the time in HH:MM:SS format")
		}
	}

	return nil
}

// Verify LogType argument is valid.Return error, if it's not matching as describer in help
func (cmd *LogTypeSubCmd) LogTypeValidate() ([]string, error) {
	if cmd.LogType == "" {
		return ServerLog, nil
	}

	logType := []string{}
	logTypeIn := strings.FieldsFunc(cmd.LogType, logTypeSplit)

	for _, value := range logTypeIn {
		if value != "admin" && value != "control" && value != "server" {
			return nil, errors.New("Invalid log-type, please use admin,control,server log-type only")
		}

		switch value {
		case "admin":
			logType = append(logType, "HelperLog")
		case "control":
			logType = append(logType, "ControlLog")
		case "server":
			logType = append(logType, "EngineLog")
		}
	}

	return logType, nil
}

func logTypeSplit(r rune) bool {
	return r == ','
}

// Print the progress while collect-log command is in progress
func (p *ProgressBar) Display() string {
	if !(p.NoDisplay) {
		// Return the progress End string.
		if p.Start == p.Total {
			printString := fmt.Sprintf("\r[%-100s] %8d/%d\n", strings.Repeat("=", 100), p.Start, p.Total)
			return printString
		}
		// Return the current progress string.
		p.Start = p.Start + 1
		printString := fmt.Sprintf("\r[%-100s] %8d/%d", strings.Repeat("=", p.Steps*p.Start), p.Start, p.Total)
		return printString
	}

	return ""
}

// Check if daos_engine process is running on server and return the bool value accordingly.
func checkEngineState(log logging.Logger) (bool, error) {
	_, err := exec.Command("bash", "-c", "pidof daos_engine").Output()
	if err != nil {
		return false, errors.Wrap(err, "daos_engine is not running on server")
	}

	return true, nil
}

// Get the server config from the running daos engine process
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

	if cfgPath == "" {
		cfgPath = filepath.Join(config.DefaultServer().SocketDir, config.ConfigOut)
	}

	if err != nil {
		return cfgPath, nil
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

// Copy Command output to the file
func cpOutputToFile(target string, log logging.Logger, cp ...logCopy) (string, error) {
	// Run command and copy output to the file
	// executing as sub shell enables pipes in cmd string
	runCmd := strings.Join([]string{cp[0].cmd, cp[0].option}, " ")
	out, err := exec.Command("sh", "-c", runCmd).CombinedOutput()
	if err != nil {
		return "", errors.New(string(out))
	}

	cmd := strings.ReplaceAll(cp[0].cmd, " -", "_")
	cmd = strings.ReplaceAll(cmd, " ", "_")
	log.Debugf("Collecting DAOS command output = %s > %s ", runCmd, filepath.Join(target, cmd))

	if err := os.WriteFile(filepath.Join(target, cmd), out, 0644); err != nil {
		return "", errors.Wrapf(err, "failed to write %s", filepath.Join(target, cmd))
	}

	return string(out), nil
}

// Create the Archive of log folder.
func ArchiveLogs(log logging.Logger, opts ...CollectLogsParams) error {
	var buf bytes.Buffer
	err := common.FolderCompress(opts[0].TargetFolder, &buf)
	if err != nil {
		return err
	}

	// write to the the .tar.gz
	tarFileName := fmt.Sprintf("%s.tar.gz", opts[0].TargetFolder)
	log.Debugf("Archiving the log folder %s", tarFileName)
	fileToWrite, err := os.OpenFile(tarFileName, os.O_CREATE|os.O_RDWR, os.FileMode(0600))
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

// Get the system hostname
func GetHostName() (string, error) {
	hn, err := exec.Command("hostname", "-s").Output()
	if err != nil {
		return "", errors.Wrapf(err, "Error running hostname -s command %s", hn)
	}
	out := strings.Split(string(hn), "\n")

	return out[0], nil
}

// Create the local folder on each servers
func createFolder(target string, log logging.Logger) error {
	if _, err := os.Stat(target); err != nil {
		log.Debugf("Log folder is not Exists, so creating %s", target)

		if err := os.MkdirAll(target, 0700); err != nil {
			return err
		}
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

// Create the TargetFolder on each server
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

func customCopy(log logging.Logger, opts CollectLogsParams, fileTransferExec string) error {
	cmd := strings.Join([]string{
		fileTransferExec,
		opts.TargetFolder,
		opts.FileTransferExecArgs},
		" ")

	out, err := exec.Command("sh", "-c", cmd).Output()
	if err != nil {
		return errors.Wrapf(err, "Error running command %s %s", cmd, string(out))
	}
	log.Infof("customCopy:= %s stdout:\n%s\n\n", cmd, string(out))
	return nil
}

// R sync logs from individual servers to Admin node
func rsyncLog(log logging.Logger, opts ...CollectLogsParams) error {
	var cfgPath string
	if opts[0].Config != "" {
		cfgPath = opts[0].Config
	} else {
		cfgPath, _ = getServerConf(log)
	}

	if cfgPath != "" {
		serverConfig := config.DefaultServer()
		serverConfig.SetPath(cfgPath)
		if err := serverConfig.Load(log); err == nil {
			if serverConfig.SupportConfig.FileTransferExec != "" {
				return customCopy(log, opts[0], serverConfig.SupportConfig.FileTransferExec)
			}
		}
	}

	targetLocation, err := createHostFolder(opts[0].TargetFolder, log)
	if err != nil {
		return err
	}

	cmd := strings.Join([]string{
		"rsync",
		"-av",
		"--blocking-io",
		targetLocation,
		opts[0].AdminNode + ":" + opts[0].TargetFolder},
		" ")

	out, err := exec.Command("sh", "-c", cmd).Output()
	if err != nil {
		return errors.Wrapf(err, "Error running command %s %s", cmd, string(out))
	}
	log.Infof("rsyncCmd:= %s stdout:\n%s\n\n", cmd, string(out))

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
			err = cpLinesFromLog(log, logfile, clientLogLocation, opts...)
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

	agentFile, err := os.ReadFile(opts[0].Config)
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
	var cfgPath string

	if opts[0].Config != "" {
		cfgPath = opts[0].Config
	} else {
		cfgPath, _ = getServerConf(log)
	}

	serverConfig := config.DefaultServer()
	serverConfig.SetPath(cfgPath)
	serverConfig.Load(log)
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

// Calculate the start/end time provided by user.
func getDateTime(log logging.Logger, opts ...CollectLogsParams) (time.Time, time.Time, error) {
	// Default Start time, in case no start time provides on start dates.This will copy log start of the day.
	if opts[0].LogStartTime == "" {
		opts[0].LogStartTime = "00:00:00"
	}

	// Default End time, in case no End time provides.This will copy log till the End of the day.
	if opts[0].LogEndTime == "" {
		opts[0].LogEndTime = "23:59:59"
	}

	startTimeStr := fmt.Sprintf("%s %s", opts[0].LogStartDate, opts[0].LogStartTime)
	endTimeStr := fmt.Sprintf("%s %s", opts[0].LogEndDate, opts[0].LogEndTime)

	actStartTime, err := time.Parse(MMDDYYYY_HHMMSS, startTimeStr)
	if err != nil {
		return time.Time{}, time.Time{}, err
	}

	actEndTime, err := time.Parse(MMDDYYYY_HHMMSS, endTimeStr)
	if err != nil {
		return time.Time{}, time.Time{}, err
	}

	return actStartTime, actEndTime, nil
}

// Copy only specific lines from the server logs based on the Start/End date and time, provided by user.
func cpLinesFromLog(log logging.Logger, srcFile string, destFile string, opts ...CollectLogsParams) error {

	// Copy the full log file in case of no dates provided
	if opts[0].LogStartDate == "" && opts[0].LogEndDate == "" {
		return cpLogFile(srcFile, destFile, log)
	}

	// Get the start/end time provided by user for comparison.
	actStartTime, actEndTime, err := getDateTime(log, opts...)
	if err != nil {
		return err
	}

	// Create the new empty file, which will be used to copy the matching log lines.
	logFileName := filepath.Join(destFile, filepath.Base(srcFile))
	writeFile, err := os.Create(logFileName)
	if err != nil {
		return err
	}
	defer writeFile.Close()

	// Open log file for reading.
	readFile, err := os.Open(srcFile)
	if err != nil {
		return err
	}
	defer readFile.Close()

	// Loop through each line and identify the date and time of each log line.
	// Compare the date/time stamp against user provided date/time.
	scanner := bufio.NewScanner(readFile)
	var cpLogLine bool
	if opts[0].LogCmd == "EngineLog" {
		// Remove year as engine log does not store the year information.
		actStartTime, _ = time.Parse(MMDDHHMMSS, actStartTime.Format(MMDDHHMMSS))
		actEndTime, _ = time.Parse(MMDDHHMMSS, actEndTime.Format(MMDDHHMMSS))

		var validDateTime = regexp.MustCompile(`^\d\d\/\d\d-\d\d:\d\d:\d\d.\d\d`)
		for scanner.Scan() {
			lineData := scanner.Text()
			lineDataSlice := strings.Split(lineData, " ")

			// Verify if log line has date/time stamp and copy line if it's in range.
			if validDateTime.MatchString(lineData) == false {
				if cpLogLine {
					_, err = writeFile.WriteString(lineData + "\n")
					if err != nil {
						return err
					}
				}
				continue
			}

			dateTime := strings.Split(lineDataSlice[0], "-")
			timeOnly := strings.Split(dateTime[1], ".")
			expDateTime := fmt.Sprintf("%s-%s", dateTime[0], timeOnly[0])
			expLogTime, _ := time.Parse(MMDDHHMMSS, expDateTime)

			// Copy line, if the log line has time stamp between the given range of start/end date and time.
			if expLogTime.After(actStartTime) && expLogTime.Before(actEndTime) {
				cpLogLine = true
				_, err = writeFile.WriteString(lineData + "\n")
				if err != nil {
					return err
				}
			}

			if expLogTime.After(actEndTime) {
				return nil
			}
		}

		if err := scanner.Err(); err != nil {
			return err
		}

		return nil
	}

	// Copy log line for Helper and Control log
	for scanner.Scan() {
		var validDateTime = regexp.MustCompile(`\d\d\d\d/\d\d/\d\d \d\d:\d\d:\d\d`)
		lineData := scanner.Text()

		// Verify if log line has date/time stamp and copy line if it's in range.
		if validDateTime.MatchString(lineData) == false {
			if cpLogLine {
				_, err = writeFile.WriteString(lineData + "\n")
				if err != nil {
					return err
				}
			}
			continue
		}

		data := validDateTime.FindAllString(lineData, -1)
		expLogTime, _ := time.Parse(YYYYMMDD_HHMMSS, data[0])
		// Copy line, if the log line has time stamp between the given range of start/end date and time.
		if expLogTime.After(actStartTime) && expLogTime.Before(actEndTime) {
			cpLogLine = true
			_, err = writeFile.WriteString(lineData + "\n")
			if err != nil {
				return err
			}
		}
		if expLogTime.After(actEndTime) {
			return nil
		}
	}

	if err := scanner.Err(); err != nil {
		return err
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
	serverConfig.Load(log)

	switch opts[0].LogCmd {
	case "EngineLog":
		if len(serverConfig.Engines) == 0 {
			return errors.New("Engine count is 0 from server config")
		}

		targetServerLogs, err := createHostLogFolder(engineLogs, log, opts...)
		if err != nil {
			return err
		}

		for i := range serverConfig.Engines {
			matches, _ := filepath.Glob(serverConfig.Engines[i].LogFile + "*")
			for _, logFile := range matches {
				err = cpLinesFromLog(log, logFile, targetServerLogs, opts...)
				if err != nil && opts[0].StopOnError {
					return err
				}
			}
		}

	case "ControlLog":
		targetControlLogs, err := createHostLogFolder(controlLogs, log, opts...)
		if err != nil {
			return err
		}

		err = cpLinesFromLog(log, serverConfig.ControlLogFile, targetControlLogs, opts...)
		if err != nil {
			return err
		}

	case "HelperLog":
		targetAdminLogs, err := createHostLogFolder(adminLogs, log, opts...)
		if err != nil {
			return err
		}

		err = cpLinesFromLog(log, serverConfig.HelperLogFile, targetAdminLogs, opts...)
		if err != nil {
			return err
		}
	}

	return nil
}

// Collect daos server metrics.
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
		serverConfig.Load(log)

		for i := range serverConfig.Engines {
			engineId := fmt.Sprintf("%d", i)
			daos.cmd = strings.Join([]string{"daos_metrics", "-S", engineId}, " ")

			_, err := cpOutputToFile(daosNodeLocation, log, daos)
			if err != nil {
				log.Errorf("Failed to run %s: %v", daos.cmd, err)
			}
		}
	} else {
		return errors.New("-- FAIL -- Daos Engine is not Running, so daos_metrics will not be collected")
	}

	return nil
}

// Collect system side info of daos_server command.
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
		hwProv := topology.DefaultProvider(hwlog)
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
	default:
		daos := logCopy{}
		daos.cmd = opts[0].LogCmd
		_, err := cpOutputToFile(daosNodeLocation, log, daos)
		if err != nil {
			return err
		}
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
	case ArchiveLogsEnum:
		return ArchiveLogs(log, opts...)
	}

	return nil
}
