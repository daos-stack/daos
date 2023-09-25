//
// (C) Copyright 2022-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package support

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/config"
)

const mockSocketDir = "/tmp/mock_socket_dir/"

func TestSupport_Display(t *testing.T) {
	progress := ProgressBar{
		Start:     1,
		Total:     7,
		NoDisplay: false,
	}

	for name, tc := range map[string]struct {
		Start     int
		Steps     int
		NoDisplay bool
		expResult string
	}{
		"Valid Step count progress": {
			Start:     2,
			Steps:     7,
			NoDisplay: false,
			expResult: "\r[=====================                                                                               ]        3/7",
		},
		"Valid progress end": {
			Start:     7,
			Steps:     7,
			NoDisplay: false,
			expResult: "\r[====================================================================================================]        7/7\n",
		},
		"No Progress Bar if JsonOutput is Enabled": {
			Start:     2,
			Steps:     7,
			NoDisplay: true,
			expResult: "",
		},
	} {
		t.Run(name, func(t *testing.T) {
			progress.Start = tc.Start
			progress.Steps = tc.Steps
			progress.NoDisplay = tc.NoDisplay
			gotOutput := progress.Display()
			test.AssertEqual(t, tc.expResult, gotOutput, "")
		})
	}
}

func TestSupport_checkEngineState(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	for name, tc := range map[string]struct {
		expResult bool
		expErr    error
	}{
		"When process is not running": {
			expResult: false,
			expErr:    errors.New("daos_engine is not running on server: exit status 1"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotOutput, gotErr := checkEngineState(log)
			test.AssertEqual(t, tc.expResult, gotOutput, "Result is not as expected")
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}

func TestSupport_getRunningConf(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	for name, tc := range map[string]struct {
		expResult string
		expErr    error
	}{
		"default config is null if no engine is running": {
			expResult: "",
			expErr:    errors.New("daos_engine is not running on server: exit status 1"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotOutput, gotErr := getRunningConf(log)
			test.AssertEqual(t, tc.expResult, gotOutput, "Result is not as expected")
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}

func TestSupport_getServerConf(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	for name, tc := range map[string]struct {
		expResult string
		expErr    error
	}{
		"default config path if no engine is running": {
			expResult: config.ConfigOut,
			expErr:    nil,
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotOutput, gotErr := getServerConf(log)
			test.AssertEqual(t, tc.expResult, filepath.Base(gotOutput), "daos server config file is not what we expected")
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}

func TestSupport_cpLogFile(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	srcTestDir, srcCleanup := test.CreateTestDir(t)
	defer srcCleanup()
	srcPath := test.CreateTestFile(t, srcTestDir, "Temp File\n")

	dstTestDir, dstCleanup := test.CreateTestDir(t)
	defer dstCleanup()

	for name, tc := range map[string]struct {
		src    string
		dst    string
		expErr error
	}{
		"Copy file to valid Directory": {
			src:    srcPath,
			dst:    dstTestDir,
			expErr: nil,
		},
		"Copy file to in valid Directory": {
			src:    srcPath,
			dst:    dstTestDir + "/tmp",
			expErr: errors.New("unable to Copy File"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotErr := cpLogFile(tc.src, tc.dst, log)
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}

func TestSupport_createFolder(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)
	targetTestDir, targetCleanup := test.CreateTestDir(t)
	defer targetCleanup()
	srcPath := test.CreateTestFile(t, targetTestDir, "Temp File\n")

	for name, tc := range map[string]struct {
		target string
		expErr error
	}{
		"Create the Valid directory": {
			target: targetTestDir + "/test1",
			expErr: nil,
		},
		"Create the directory with existing file name": {
			target: srcPath + "/file1",
			expErr: errors.New("mkdir " + srcPath + ": not a directory"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotErr := createFolder(tc.target, log)
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}

func TestSupport_GetHostName(t *testing.T) {
	hostName, _ := os.Hostname()
	for name, tc := range map[string]struct {
		expResult string
		expErr    error
	}{
		"Check Valid Hostname": {
			expResult: hostName,
			expErr:    nil,
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotOutput, gotErr := GetHostName()
			test.CmpErr(t, tc.expErr, gotErr)

			if !strings.Contains(tc.expResult, gotOutput) {
				t.Errorf("Hostname '%s' is not part of full hostname '%s')",
					gotOutput, tc.expResult)
			}
		})
	}
}

func TestSupport_cpOutputToFile(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)
	targetTestDir, targetCleanup := test.CreateTestDir(t)
	defer targetCleanup()

	hostName, _ := os.Hostname()
	logCp := logCopy{}

	for name, tc := range map[string]struct {
		target    string
		cmd       string
		option    string
		expResult string
		expErr    error
	}{
		"Check valid Command without option": {
			target:    targetTestDir,
			cmd:       "hostname",
			option:    "",
			expResult: hostName,
			expErr:    nil,
		},
		"Check valid Command with option": {
			target:    targetTestDir,
			cmd:       "hostname",
			option:    "-d",
			expResult: hostName,
			expErr:    nil,
		},
		"Check invalid Command": {
			target:    targetTestDir,
			cmd:       "hostnamefoo",
			option:    "",
			expResult: "",
			expErr:    errors.New("command not found"),
		},
		"Check valid Command with invalid target directory": {
			target:    targetTestDir + "/dir1",
			cmd:       "hostname",
			option:    "",
			expResult: "",
			expErr:    errors.New("failed to write"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			logCp.cmd = tc.cmd
			logCp.option = tc.option
			gotOutput, gotErr := cpOutputToFile(tc.target, log, logCp)
			gotOutput = strings.TrimRight(gotOutput, "\n")
			if !strings.Contains(tc.expResult, gotOutput) {
				t.Errorf("Hostname '%s' is not part of full hostname '%s')",
					gotOutput, tc.expResult)
			}
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}

func TestSupport_ArchiveLogs(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)
	targetTestDir, targetCleanup := test.CreateTestDir(t)
	defer targetCleanup()
	test.CreateTestFile(t, targetTestDir, "Temp Log File\n")

	arcLog := CollectLogsParams{}

	for name, tc := range map[string]struct {
		targetFolder string
		expErr       error
	}{
		"Directory with valid log file": {
			targetFolder: targetTestDir,
			expErr:       nil,
		},
		"Invalid Directory": {
			targetFolder: targetTestDir + "/foo/bar",
			expErr:       errors.New("no such file or directory"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			arcLog.TargetFolder = tc.targetFolder
			gotErr := ArchiveLogs(log, arcLog)
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}

func TestSupport_createHostLogFolder(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)
	targetTestDir, targetCleanup := test.CreateTestDir(t)
	defer targetCleanup()
	srcPath := test.CreateTestFile(t, targetTestDir, "Temp File\n")

	collLogParams := CollectLogsParams{}

	for name, tc := range map[string]struct {
		dst          string
		targetFolder string
		expErr       error
	}{
		"Create the valid Log directory": {
			dst:          dmgSystemLogs,
			targetFolder: targetTestDir,
			expErr:       nil,
		},
		"Create the invalid Log directory": {
			dst:          dmgSystemLogs,
			targetFolder: srcPath + "/file1",
			expErr:       errors.New("mkdir " + srcPath + ": not a directory"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			collLogParams.TargetFolder = tc.targetFolder
			_, gotErr := createHostLogFolder(tc.dst, log, collLogParams)
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}

func TestSupport_rsyncLog(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)
	targetTestDir, targetCleanup := test.CreateTestDir(t)
	defer targetCleanup()
	srcPath := test.CreateTestFile(t, targetTestDir, "Temp File\n")
	hostName, _ := os.Hostname()

	rsLog := CollectLogsParams{}

	for name, tc := range map[string]struct {
		targetFolder string
		AdminNode    string
		expErr       error
	}{
		"rsync to invalid Target directory": {
			targetFolder: targetTestDir + "/foo/bar",
			AdminNode:    hostName + ":/tmp/foo/bar/",
			expErr:       errors.New("Error running command"),
		},
		"rsync invalid log directory": {
			targetFolder: srcPath + "/file1",
			AdminNode:    hostName,
			expErr:       errors.New("not a directory"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			rsLog.TargetFolder = tc.targetFolder
			rsLog.AdminNode = tc.AdminNode
			gotErr := rsyncLog(log, rsLog)
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}

func TestSupport_collectExtraLogsDir(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)
	targetTestDir, targetCleanup := test.CreateTestDir(t)
	defer targetCleanup()
	extraLogDir, extraLogCleanup := test.CreateTestDir(t)
	defer extraLogCleanup()
	srcPath := test.CreateTestFile(t, targetTestDir, "Temp File\n")
	test.CreateTestFile(t, extraLogDir, "Extra Log File\n")

	rsLog := CollectLogsParams{}

	for name, tc := range map[string]struct {
		targetFolder string
		ExtraLogsDir string
		expErr       error
	}{
		"Copy valid log directory": {
			targetFolder: targetTestDir,
			ExtraLogsDir: extraLogDir,
			expErr:       nil,
		},
		"Copy to invalid target directory": {
			targetFolder: srcPath + "/file1",
			ExtraLogsDir: extraLogDir,
			expErr:       errors.New("not a directory"),
		},
		"Copy invalid extra log directory": {
			targetFolder: targetTestDir,
			ExtraLogsDir: extraLogDir + "foo/bar",
			expErr:       errors.New("no such file or directory"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			rsLog.TargetFolder = tc.targetFolder
			rsLog.ExtraLogsDir = tc.ExtraLogsDir
			gotErr := collectExtraLogsDir(log, rsLog)
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}

func TestSupport_collectCmdOutput(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)
	targetTestDir, targetCleanup := test.CreateTestDir(t)
	defer targetCleanup()
	srcPath := test.CreateTestFile(t, targetTestDir, "Temp File\n")

	logCp := CollectLogsParams{}

	for name, tc := range map[string]struct {
		folderName   string
		targetFolder string
		cmd          string
		expErr       error
	}{
		"Valid command output": {
			targetFolder: targetTestDir,
			folderName:   genSystemInfo,
			cmd:          "hostname",
			expErr:       nil,
		},
		"Invalid command output": {
			targetFolder: targetTestDir,
			folderName:   genSystemInfo,
			cmd:          "hostname-cmd-notfound",
			expErr:       errors.New("command not found"),
		},
		"Invalid targetFolder": {
			targetFolder: srcPath + "/file1",
			folderName:   genSystemInfo,
			cmd:          "hostname",
			expErr:       errors.New("not a directory"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			logCp.LogCmd = tc.cmd
			logCp.TargetFolder = tc.targetFolder
			gotErr := collectCmdOutput(tc.folderName, log, logCp)
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}

func TestSupport_collectClientLog(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)
	targetTestDir, targetCleanup := test.CreateTestDir(t)
	defer targetCleanup()
	srcPath := test.CreateTestFile(t, targetTestDir, "Temp File\n")

	collLogParams := CollectLogsParams{}
	os.Setenv("D_LOG_FILE", srcPath)

	for name, tc := range map[string]struct {
		targetFolder string
		expErr       error
	}{
		"Collect valid client log": {
			targetFolder: targetTestDir,
			expErr:       nil,
		},
		"Collect invalid client log": {
			targetFolder: srcPath + "/file1",
			expErr:       errors.New("mkdir " + srcPath + ": not a directory"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			collLogParams.TargetFolder = tc.targetFolder
			gotErr := collectClientLog(log, collLogParams)
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}

func TestSupport_collectAgentLog(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)
	targetTestDir, targetCleanup := test.CreateTestDir(t)
	defer targetCleanup()

	agentTestDir, agentCleanup := test.CreateTestDir(t)
	defer agentCleanup()

	srcPath := test.CreateTestFile(t, targetTestDir, "Temp File\n")
	agentConfig := test.CreateTestFile(t, agentTestDir, "log_file: "+srcPath+"\n")
	agentInvalidConfig := test.CreateTestFile(t, agentTestDir, "invalid_log_file: "+srcPath+"\n")

	collLogParams := CollectLogsParams{}

	for name, tc := range map[string]struct {
		targetFolder string
		config       string
		expErr       error
	}{
		"Valid agent log collect": {
			targetFolder: targetTestDir,
			config:       agentConfig,
			expErr:       nil,
		},
		"Invalid agent log entry in yaml": {
			targetFolder: targetTestDir,
			config:       agentInvalidConfig,
			expErr:       errors.New("no such file or directory"),
		},
		"Without agent file": {
			targetFolder: targetTestDir,
			config:       "",
			expErr:       errors.New("no such file or directory"),
		},
		"Invalid agent yaml file format": {
			targetFolder: targetTestDir,
			config:       srcPath,
			expErr:       errors.New("unmarshal errors"),
		},
		"Invalid Agent target folder": {
			targetFolder: srcPath + "/file1",
			config:       agentConfig,
			expErr:       errors.New("mkdir " + srcPath + ": not a directory"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			collLogParams.TargetFolder = tc.targetFolder
			collLogParams.Config = tc.config
			gotErr := collectAgentLog(log, collLogParams)
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}

func TestSupport_copyAgentConfig(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)
	targetTestDir, targetCleanup := test.CreateTestDir(t)
	defer targetCleanup()
	srcPath := test.CreateTestFile(t, targetTestDir, "Temp File\n")
	agentConfig := test.CreateTestFile(t, targetTestDir, "log_file: "+srcPath+"\n")

	collLogParams := CollectLogsParams{}

	for name, tc := range map[string]struct {
		targetFolder string
		config       string
		expErr       error
	}{
		"Valid agent log collect": {
			targetFolder: targetTestDir,
			config:       agentConfig,
			expErr:       nil,
		},
		"Invalid Agent log folder": {
			targetFolder: srcPath + "/file1",
			config:       agentConfig,
			expErr:       errors.New("mkdir " + srcPath + ": not a directory"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			collLogParams.TargetFolder = tc.targetFolder
			collLogParams.Config = tc.config
			gotErr := copyAgentConfig(log, collLogParams)
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}

func TestSupport_copyServerConfig(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)
	targetTestDir, targetCleanup := test.CreateTestDir(t)
	defer targetCleanup()
	srcPath := test.CreateTestFile(t, targetTestDir, "Temp File\n")
	serverConfig := test.CreateTestFile(t, targetTestDir, "log_file: "+srcPath+"\n")

	collLogParams := CollectLogsParams{}
	defaultSeverConfig, _ := getServerConf(log, collLogParams)

	for name, tc := range map[string]struct {
		createFile   bool
		targetFolder string
		config       string
		expErr       error
	}{
		"Copy server file which is not available": {
			createFile:   false,
			targetFolder: targetTestDir,
			config:       mockSocketDir + config.ConfigOut + "notavailable",
			expErr:       errors.New("no such file or directory"),
		},
		"Copy to Invalid folder": {
			createFile:   false,
			targetFolder: srcPath + "/file1",
			config:       serverConfig,
			expErr:       errors.New("mkdir " + srcPath + ": not a directory"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			if tc.createFile {
				data := []byte("hello\nDAOS\n")
				if err := os.WriteFile(defaultSeverConfig, data, 0644); err != nil {
					t.Fatalf(err.Error())
				}
			}
			collLogParams.TargetFolder = tc.targetFolder
			collLogParams.Config = tc.config
			gotErr := copyServerConfig(log, collLogParams)
			test.CmpErr(t, tc.expErr, gotErr)

			if tc.createFile {
				if err := os.Remove(defaultSeverConfig); err != nil {
					t.Fatalf(err.Error())
				}
			}
		})
	}
}

func TestSupport_collectServerLog(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)
	targetTestDir, targetCleanup := test.CreateTestDir(t)
	defer targetCleanup()
	engineLog0 := test.CreateTestFile(t, targetTestDir, "Engine Log 0")
	engineLog1 := test.CreateTestFile(t, targetTestDir, "Engine Log 1")
	controlLog := test.CreateTestFile(t, targetTestDir, "Control Log")
	helperLog := test.CreateTestFile(t, targetTestDir, "Helper Log")

	MockValidServerConfig := `port: 10001
transport_config:
  allow_insecure: false
  client_cert_dir: /etc/daos/certs/clients
  ca_cert: /etc/daos/certs/daosCA.crt
  cert: /etc/daos/certs/server.crt
  key: /etc/daos/certs/server.key
engines:
- targets: 12
  nr_xs_helpers: 2
  first_core: 0
  log_file: ` + engineLog0 + `
  storage:
  - class: dcpm
    scm_mount: /mnt/daos0
    scm_list:
    - /dev/pmem0
  - class: nvme
    bdev_list:
    - "0000:00:00.0"
    - "0000:01:00.0"
    - "0000:02:00.0"
    - "0000:03:00.0"
  provider: ofi+verbs
  fabric_iface: ib0
  fabric_iface_port: 31416
  pinned_numa_node: 0
- targets: 6
  nr_xs_helpers: 0
  first_core: 0
  log_file: ` + engineLog1 + `
  storage:
  - class: dcpm
    scm_mount: /mnt/daos1
    scm_list:
    - /dev/pmem1
  - class: nvme
    bdev_list:
    - "0000:04:00.0"
    - "0000:05:00.0"
    - "0000:06:00.0"
  provider: ofi+verbs
  fabric_iface: ib1
  fabric_iface_port: 32416
  pinned_numa_node: 1
disable_vfio: false
disable_vmd: false
enable_hotplug: false
nr_hugepages: 6144
disable_hugepages: false
control_log_mask: INFO
control_log_file: ` + controlLog + `
helper_log_file: ` + helperLog + `
core_dump_filter: 19
name: daos_server
socket_dir: /var/run/daos_server
provider: ofi+verbs
access_points:
- hostX:10002
fault_cb: ""
hyperthreads: false
`

	MockInvalidServerConfig := `port: 10001
transport_config:
  allow_insecure: false
  client_cert_dir: /etc/daos/certs/clients
  ca_cert: /etc/daos/certs/daosCA.crt
  cert: /etc/daos/certs/server.crt
  key: /etc/daos/certs/server.key
engines:
- targets: 12
  nr_xs_helpers: 2
  first_core: 0
  log_file: ` + targetTestDir + ` /dir1/invalid_engine0.log
  storage:
  - class: dcpm
    scm_mount: /mnt/daos0
    scm_list:
    - /dev/pmem0
  - class: nvme
    bdev_list:
    - "0000:00:00.0"
    - "0000:01:00.0"
    - "0000:02:00.0"
    - "0000:03:00.0"
  provider: ofi+verbs
  fabric_iface: ib0
  fabric_iface_port: 31416
  pinned_numa_node: 0
- targets: 6
  nr_xs_helpers: 0
  first_core: 0
  log_file: ` + targetTestDir + ` /dir1/invalid_engine1.log
  storage:
  - class: dcpm
    scm_mount: /mnt/daos1
    scm_list:
    - /dev/pmem1
  - class: nvme
    bdev_list:
    - "0000:04:00.0"
    - "0000:05:00.0"
    - "0000:06:00.0"
  provider: ofi+verbs
  fabric_iface: ib1
  fabric_iface_port: 32416
  pinned_numa_node: 1
disable_vfio: false
disable_vmd: false
enable_hotplug: false
nr_hugepages: 6144
disable_hugepages: false
control_log_mask: INFO
control_log_file: ` + targetTestDir + ` /dir1/invalid_control.log
helper_log_file: ` + targetTestDir + ` /dir1/invalid_helper.log
core_dump_filter: 19
name: daos_server
socket_dir: /var/run/daos_server
provider: ofi+verbs
access_points:
- hostX:10002
fault_cb: ""
hyperthreads: false
`

	MockZeroEngineServerConfig := `port: 10001
transport_config:
  allow_insecure: false
  client_cert_dir: /etc/daos/certs/clients
  ca_cert: /etc/daos/certs/daosCA.crt
  cert: /etc/daos/certs/server.crt
  key: /etc/daos/certs/server.key
`

	validConfig := test.CreateTestFile(t, targetTestDir, MockValidServerConfig)
	invalidConfig := test.CreateTestFile(t, targetTestDir, MockInvalidServerConfig)
	zeroEngineConfig := test.CreateTestFile(t, targetTestDir, MockZeroEngineServerConfig)
	collLogParams := CollectLogsParams{}

	for name, tc := range map[string]struct {
		logCmd       string
		targetFolder string
		config       string
		expErr       error
	}{
		"Copy Server Logs": {
			targetFolder: targetTestDir,
			config:       validConfig,
			logCmd:       "EngineLog",
			expErr:       nil,
		},
		"Copy Control Logs": {
			targetFolder: targetTestDir,
			config:       validConfig,
			logCmd:       "ControlLog",
			expErr:       nil,
		},
		"Copy Helper Logs": {
			targetFolder: targetTestDir,
			config:       validConfig,
			logCmd:       "HelperLog",
			expErr:       nil,
		},
		"Copy Invalid Control Logs": {
			targetFolder: targetTestDir,
			config:       invalidConfig,
			logCmd:       "ControlLog",
			expErr:       errors.New("no such file or directory"),
		},
		"Copy Invalid Helper Logs": {
			targetFolder: targetTestDir,
			config:       invalidConfig,
			logCmd:       "HelperLog",
			expErr:       errors.New("no such file or directory"),
		},
		"Copy Server Logs with invalid config": {
			targetFolder: targetTestDir,
			config:       zeroEngineConfig,
			logCmd:       "EngineLog",
			expErr:       errors.New("Engine count is 0 from server config"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			collLogParams.TargetFolder = tc.targetFolder
			collLogParams.Config = tc.config
			collLogParams.LogCmd = tc.logCmd
			gotErr := collectServerLog(log, collLogParams)
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}
