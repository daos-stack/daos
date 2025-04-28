//
// (C) Copyright 2022-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package support

import (
	"fmt"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"testing"
	"time"

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
			option:    "-s",
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

func TestSupport_customCopy(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)
	targetTestDir, targetCleanup := test.CreateTestDir(t)
	defer targetCleanup()

	rsLog := CollectLogsParams{}

	// Get the current PATH environment variable.
	oldPathEnv := os.Getenv("PATH")
	defer os.Setenv("PATH", oldPathEnv)
	if err := os.Setenv("PATH", fmt.Sprintf("%s:%s", oldPathEnv, targetTestDir)); err != nil {
		t.Fatal(err)
	}

	binaryPath := filepath.Join(targetTestDir, "daos_alt_rsync")
	if err := os.WriteFile(binaryPath, []byte("#!/bin/bash\necho \"Hello, world!\"\n"), 0755); err != nil {
		t.Fatalf("Failed to create custom binary: %v", err)
	}

	validConfigPath := filepath.Join(targetTestDir, "daos_server_configset.yaml")
	if err := os.WriteFile(validConfigPath, []byte("support_config:\n  file_transfer_exec: "+binaryPath+"\n"), 0755); err != nil {
		t.Fatalf("Failed to create valid config file: %v", err)
	}

	invalidConfigPath := filepath.Join(targetTestDir, "daos_server_bad.yaml")
	if err := os.WriteFile(invalidConfigPath, []byte("support_config:\n  file_transfer_exec: foo\n"), 0755); err != nil {
		t.Fatalf("Failed to create invalid config file: %v", err)
	}

	for name, tc := range map[string]struct {
		configPath string
		expErr     error
	}{
		"valid path": {
			configPath: validConfigPath,
			expErr:     nil,
		},
		"non existent binary": {
			configPath: invalidConfigPath,
			expErr:     errors.New("Error running command"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			rsLog.Config = tc.configPath
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
					t.Fatal(err.Error())
				}
			}
			collLogParams.TargetFolder = tc.targetFolder
			collLogParams.Config = tc.config
			gotErr := copyServerConfig(log, collLogParams)
			test.CmpErr(t, tc.expErr, gotErr)

			if tc.createFile {
				if err := os.Remove(defaultSeverConfig); err != nil {
					t.Fatal(err.Error())
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

func TestSupport_DateTimeValidate(t *testing.T) {
	for name, tc := range map[string]struct {
		logStartDate string
		logEndDate   string
		logStartTime string
		logEndTime   string
		expErr       error
	}{
		"Empty Date and Time": {
			expErr: nil,
		},
		"Valid StartDate No EndDate": {
			logStartDate: "12-01-2024",
			expErr:       errors.New("Invalid date, please provide the endDate in MM-DD-YYYY format"),
		},
		"No StartDate Valid EndDate": {
			logEndDate: "12-31-2024",
			expErr:     errors.New("Invalid date, please provide the startDate in MM-DD-YYYY format"),
		},
		"Invalid StartDate No EndDate": {
			logStartDate: "44-22-2024",
			expErr:       errors.New("Invalid date, please provide the startDate in MM-DD-YYYY format"),
		},
		"Invalid EndDate": {
			logStartDate: "12-01-2024",
			logEndDate:   "44-22-2024",
			expErr:       errors.New("Invalid date, please provide the endDate in MM-DD-YYYY format"),
		},
		"StartDate after EndDate": {
			logStartDate: "10-01-2024",
			logEndDate:   "05-06-2024",
			expErr:       errors.New("start-date can not be after end-date"),
		},
		"Valid StartDate and EndDate": {
			logStartDate: "12-01-2024",
			logEndDate:   "12-31-2024",
			expErr:       nil,
		},
		"Valid StartTime No EndTime": {
			logStartTime: "13:15:59",
			expErr:       nil,
		},
		"No StartTime valid EndTime": {
			logEndTime: "20:30:50",
			expErr:     nil,
		},
		"Invalid StartTime": {
			logStartTime: "25:99:67",
			expErr:       errors.New("Invalid log-start-time, please provide the time in HH:MM:SS format"),
		},
		"Invalid EndTime": {
			logStartTime: "13:15:59",
			logEndTime:   "25:99:67",
			expErr:       errors.New("Invalid log-end-time, please provide the time in HH:MM:SS format"),
		},
		"Valid StartTime EndTime": {
			logStartTime: "13:15:59",
			logEndTime:   "20:30:50",
			expErr:       nil,
		},
		"Valid Date Time": {
			logStartDate: "12-01-2024",
			logEndDate:   "12-31-2024",
			logStartTime: "13:15:59",
			logEndTime:   "20:30:50",
			expErr:       nil,
		},
	} {
		t.Run(name, func(t *testing.T) {
			var params CollectLogSubCmd
			params.LogStartDate = tc.logStartDate
			params.LogEndDate = tc.logEndDate
			params.LogStartTime = tc.logStartTime
			params.LogEndTime = tc.logEndTime
			err := params.DateTimeValidate()
			test.CmpErr(t, tc.expErr, err)
			if err != nil {
				return
			}
		})
	}
}

func TestSupport_LogTypeValidate(t *testing.T) {
	for name, tc := range map[string]struct {
		logType    string
		expLogType []string
		expErr     error
	}{
		"empty": {
			expLogType: ServerLog,
			expErr:     nil,
		},
		"Invalid LogType": {
			logType:    "INVALID_LOG",
			expLogType: nil,
			expErr:     errors.New("Invalid log-type, please use admin,control,server log-type only"),
		},
		"LogType Admin": {
			logType:    "admin",
			expLogType: []string{"HelperLog"},
			expErr:     nil,
		},
		"LogType Control": {
			logType:    "control",
			expLogType: []string{"ControlLog"},
			expErr:     nil,
		},
		"LogType Server": {
			logType:    "server",
			expLogType: []string{"EngineLog"},
			expErr:     nil,
		},
		"LogType Admin Control": {
			logType:    "admin,control",
			expLogType: []string{"HelperLog", "ControlLog"},
			expErr:     nil,
		},
		"LogType Admin Control Server": {
			logType:    "admin,control,server",
			expLogType: []string{"HelperLog", "ControlLog", "EngineLog"},
			expErr:     nil,
		},
	} {
		t.Run(name, func(t *testing.T) {
			var params LogTypeSubCmd
			params.LogType = tc.logType
			logType, err := params.LogTypeValidate()
			test.CmpErr(t, tc.expErr, err)
			if err != nil {
				return
			}

			if reflect.DeepEqual(logType, tc.expLogType) == false {
				t.Fatalf("logType Expected:%s Got:%s", tc.expLogType, logType)
			}

		})
	}
}

func TestSupport_cpLinesFromLog(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)
	targetTestDir, targetCleanup := test.CreateTestDir(t)
	defer targetCleanup()

	srcPath := test.CreateTestFile(t, targetTestDir, "Temp File\n")
	dstTestDir, dstCleanup := test.CreateTestDir(t)
	defer dstCleanup()

	collLogParams := CollectLogsParams{}

	DummyEngineLog := `01/01-01:01:01.90 system-01 LOG LINE 1
02/02-04:04:04.90 system-02 LOG LINE 2
03/03-06:06:06.90 system-02 LOG LINE 3
04/04-08:08:08.90 system-02 LOG LINE 4
05/05-10:10:10.90 system-02 LOG LINE 5
06/06-12:12:12.90 system-02 LOG LINE 6
07/07-14:14:14.90 system-02 LOG LINE 7
LINE WITHOUT DATE AND TIME
08/08-16:16:16.90 system-02 LOG LINE 8
09/09-18:18:18.90 system-02 LOG LINE 9
10/10-20:20:20.90 system-02 LOG LINE 10
11/11-22:22:22.90 system-02 LOG LINE 11
12/12-23:59:59.90 system-02 LOG LINE 12
`
	MockEngineLogFile := test.CreateTestFile(t, targetTestDir, DummyEngineLog)

	DummyControlLog := `hostname INFO 2023/01/01 01:01:01 LOG LINE 1
hostname INFO 2023/02/02 04:04:04 LOG LINE 2
hostname INFO 2023/03/03 06:06:06 LOG LINE 3
hostname INFO 2023/04/04 08:08:08 LOG LINE 4
hostname INFO 2023/05/05 10:10:10 LOG LINE 5
hostname INFO 2023/06/06 12:12:12 LOG LINE 6
hostname INFO 2023/07/07 14:14:14 LOG LINE 7
LINE WITHOUT DATE AND TIME
hostname INFO 2023/08/08 16:16:16 LOG LINE 8
hostname INFO 2023/09/09 18:18:18 LOG LINE 9
hostname INFO 2023/10/10 20:20:20 LOG LINE 10
hostname INFO 2023/11/11 22:22:22 LOG LINE 11
hostname INFO 2023/12/12 23:59:59 LOG LINE 12
`
	MockControlLogFile := test.CreateTestFile(t, targetTestDir, DummyControlLog)

	DummyAdminLog := `INFO 2023/01/01 01:01:01.441231 LOG LINE 1
INFO 2023/02/02 04:04:04.441232 LOG LINE 2
INFO 2023/03/03 06:06:06.441233 LOG LINE 3
INFO 2023/04/04 08:08:08.441234 LOG LINE 4
INFO 2023/05/05 10:10:10.441235 LOG LINE 5
INFO 2023/06/06 12:12:12.441235 LOG LINE 6
INFO 2023/07/07 14:14:14.441236 LOG LINE 7
LINE WITHOUT DATE AND TIME
INFO 2023/08/08 16:16:16.441237 LOG LINE 8
INFO 2023/09/09 18:18:18.441238 LOG LINE 9
INFO 2023/10/10 20:20:20.441239 LOG LINE 10
INFO 2023/11/11 22:22:22.441240 LOG LINE 11
INFO 2023/12/12 23:59:59.441241 LOG LINE 12
`
	MockAdminLogFile := test.CreateTestFile(t, targetTestDir, DummyAdminLog)

	for name, tc := range map[string]struct {
		logStartDate string
		logEndDate   string
		logStartTime string
		logEndTime   string
		srcFile      string
		destFile     string
		expErr       error
		verifyLog    string
		logCmd       string
	}{
		"No startDate and EndDate": {
			logStartDate: "",
			logEndDate:   "",
			srcFile:      srcPath,
			destFile:     dstTestDir,
			expErr:       nil,
		},
		"Invalid Destination Directory": {
			logStartDate: "",
			logEndDate:   "",
			srcFile:      srcPath,
			destFile:     dstTestDir + "/tmp",
			expErr:       errors.New("unable to Copy File"),
		},
		"Invalid Source File": {
			logStartDate: "01-01-2023",
			logEndDate:   "12-31-2023",
			srcFile:      srcPath + "unknownFile",
			destFile:     dstTestDir,
			expErr:       errors.New("no such file or directory"),
		},
		"Valid date without any time": {
			logStartDate: "01-01-2023",
			logEndDate:   "12-31-2023",
			srcFile:      srcPath,
			destFile:     dstTestDir,
			expErr:       nil,
		},
		"Verify the content of Engine log line based on date": {
			logStartDate: "04-01-2023",
			logEndDate:   "08-08-2023",
			srcFile:      MockEngineLogFile,
			destFile:     dstTestDir,
			logCmd:       "EngineLog",
			expErr:       nil,
			verifyLog:    "08/08-16:16:16.90 system-02 LOG LINE 8",
		},
		"Verify the content of Engine log line based on date and time": {
			logStartDate: "09-09-2023",
			logEndDate:   "11-11-2023",
			logStartTime: "12:00:00",
			logEndTime:   "23:23:23",
			srcFile:      MockEngineLogFile,
			destFile:     dstTestDir,
			logCmd:       "EngineLog",
			expErr:       nil,
			verifyLog:    "11/11-22:22:22.90 system-02 LOG LINE 11",
		},
		"Verify the content of Control log line based on date": {
			logStartDate: "04-01-2023",
			logEndDate:   "08-08-2023",
			srcFile:      MockControlLogFile,
			destFile:     dstTestDir,
			logCmd:       "ControlLog",
			expErr:       nil,
			verifyLog:    "hostname INFO 2023/08/08 16:16:16 LOG LINE 8",
		},
		"Verify the content of Control log line based on date and time": {
			logStartDate: "09-09-2023",
			logEndDate:   "11-11-2023",
			logStartTime: "12:00:00",
			logEndTime:   "23:23:23",
			srcFile:      MockControlLogFile,
			destFile:     dstTestDir,
			logCmd:       "ControlLog",
			expErr:       nil,
			verifyLog:    "hostname INFO 2023/11/11 22:22:22 LOG LINE 11",
		},
		"Verify the content of Admin log line based on date": {
			logStartDate: "04-01-2023",
			logEndDate:   "08-08-2023",
			srcFile:      MockAdminLogFile,
			destFile:     dstTestDir,
			logCmd:       "HelperLog",
			expErr:       nil,
			verifyLog:    "INFO 2023/08/08 16:16:16.441237 LOG LINE 8",
		},
		"Verify the content of Admin log line based on date and time": {
			logStartDate: "09-09-2023",
			logEndDate:   "11-11-2023",
			logStartTime: "12:00:00",
			logEndTime:   "23:23:23",
			srcFile:      MockAdminLogFile,
			destFile:     dstTestDir,
			logCmd:       "HelperLog",
			expErr:       nil,
			verifyLog:    "INFO 2023/11/11 22:22:22.441240 LOG LINE 11",
		},
	} {
		t.Run(name, func(t *testing.T) {
			collLogParams.LogStartDate = tc.logStartDate
			collLogParams.LogEndDate = tc.logEndDate
			collLogParams.LogStartTime = tc.logStartTime
			collLogParams.LogEndTime = tc.logEndTime
			collLogParams.LogCmd = tc.logCmd
			gotErr := cpLinesFromLog(log, tc.srcFile, tc.destFile, collLogParams)
			test.CmpErr(t, tc.expErr, gotErr)

			if tc.verifyLog != "" {
				readFile := filepath.Join(tc.destFile, filepath.Base(tc.srcFile))
				b, err := os.ReadFile(readFile)
				if err != nil {
					t.Fatal(err.Error())
				}

				if strings.Contains(string(b), tc.verifyLog) == false {
					t.Fatalf("Expected log line:=%s can not be found in File:=%s", tc.verifyLog, readFile)
				}

			}
		})
	}
}

func TestSupport_getDateTime(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	collLogParams := CollectLogsParams{}

	for name, tc := range map[string]struct {
		logStartDate string
		logEndDate   string
		logStartTime string
		logEndTime   string
		expStartTime string
		expEndTime   string
		expErr       error
	}{
		"No StartTime": {
			logStartDate: "1-2-2023",
			logEndDate:   "1-3-2023",
			expErr:       nil,
		},
		"No EndTime": {
			logStartDate: "1-2-2023",
			logEndDate:   "1-3-2023",
			logStartTime: "10:10:10",
			expStartTime: "01-02-2023 10:10:10",
			expEndTime:   "01-03-2023 23:59:59",
			expErr:       nil,
		},
		"Valid Date and Invalid Start Time": {
			logStartDate: "1-2-2023",
			logEndDate:   "1-3-2023",
			logStartTime: "99:99:99",
			logEndTime:   "12:12:12",
			expErr:       errors.New("parsing time \"1-2-2023 99:99:99\": hour out of range"),
		},
		"Valid Date and Invalid End Time": {
			logStartDate: "1-2-2023",
			logEndDate:   "1-3-2023",
			logStartTime: "10:10:10",
			logEndTime:   "99:99:99",
			expErr:       errors.New("parsing time \"1-3-2023 99:99:99\": hour out of range"),
		},
		"Valid Date and Time": {
			logStartDate: "1-2-2023",
			logEndDate:   "1-3-2023",
			logStartTime: "10:10:10",
			logEndTime:   "12:12:12",
			expStartTime: "01-02-2023 10:10:10",
			expEndTime:   "01-03-2023 12:12:12",
			expErr:       nil,
		},
	} {
		t.Run(name, func(t *testing.T) {
			collLogParams.LogStartDate = tc.logStartDate
			collLogParams.LogEndDate = tc.logEndDate
			collLogParams.LogStartTime = tc.logStartTime
			collLogParams.LogEndTime = tc.logEndTime
			startTime, endTime, gotErr := getDateTime(log, collLogParams)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expStartTime != "" {
				tmpStartTime, _ := time.Parse(MMDDYYYY_HHMMSS, tc.expStartTime)
				if tmpStartTime.Equal(startTime) == false {
					t.Fatalf("Expected StartTime:=%s But Got :=%s", tmpStartTime, startTime)
				}
			}
			if tc.expEndTime != "" {
				tmpEndTime, _ := time.Parse(MMDDYYYY_HHMMSS, tc.expEndTime)
				if tmpEndTime.Equal(endTime) == false {
					t.Fatalf("Expected EndTime:=%s But Got :=%s", tmpEndTime, endTime)
				}
			}
		})
	}
}
