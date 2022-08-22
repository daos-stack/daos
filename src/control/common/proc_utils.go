//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package common

import (
	"io/ioutil"
	"os"
	"path/filepath"
	"strconv"
	"strings"

	"github.com/pkg/errors"
)

func getProcPids(procDir, procName string) (pids []int, _ error) {
	allProcs, err := filepath.Glob(procDir + "/*/status")
	if err != nil {
		return nil, errors.Wrap(err, "failed to read process list")
	}
	for _, proc := range allProcs {
		data, err := ioutil.ReadFile(proc)
		if err != nil {
			return nil, errors.Wrap(err, "failed to read process command line")
		}
		lines := strings.Split(string(data), "\n")
		if len(lines) == 0 {
			return nil, errors.Errorf("invalid process status file: %s", proc)
		}
		namePair := strings.SplitN(lines[0], ":", 2)
		if len(namePair) < 2 {
			return nil, errors.Errorf("invalid process status file: %s", proc)
		}
		if strings.TrimSpace(namePair[1]) != procName {
			continue
		}

		pid, err := strconv.Atoi(filepath.Base(filepath.Dir(proc)))
		if err != nil {
			continue
		}
		pids = append(pids, pid)
	}

	return
}

// GetProcPids returns a list of pids for the given process name.
func GetProcPids(procName string) (pids []int, _ error) {
	return getProcPids("/proc", procName)
}

func getProcName(pid int, procDir string) (string, error) {
	exe, err := os.Readlink(procDir + "/" + strconv.Itoa(pid) + "/exe")
	if err != nil {
		return "", errors.Wrap(err, "failed to read executable path")
	}
	return filepath.Base(exe), nil
}

func checkDupeProcess(pid int, procDir string) error {
	name, err := getProcName(pid, procDir)
	if err != nil {
		return errors.Wrap(err, "failed to get process name")
	}
	pids, err := getProcPids(procDir, name)
	if err != nil {
		return errors.Wrap(err, "failed to read process list")
	}

	for _, otherPid := range pids {
		if otherPid == pid {
			continue
		}

		return errors.Errorf("another %s process is already running (pid: %d)", name, otherPid)
	}

	return nil
}

// CheckDupeProcess checks to see if another process with the same name as
// ours is running.
func CheckDupeProcess() error {
	return checkDupeProcess(os.Getpid(), "/proc")
}
