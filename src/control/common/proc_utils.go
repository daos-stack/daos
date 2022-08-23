//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package common

import (
	"fmt"
	"os"
	"path/filepath"
	"strconv"

	"github.com/pkg/errors"
)

func subStr(in string, max int) string {
	if len(in) > max {
		return in[:max]
	}
	return in
}

func getProcPids(procDir, procName string) (pids []int, _ error) {
	allProcs, err := filepath.Glob(filepath.Join(procDir, "*", "stat"))
	if err != nil {
		return nil, errors.Wrap(err, "failed to read process list")
	}

	maxProcName := 15 // TASK_COMM_LEN-1 in linux/sched.h
	var pid int
	var name string
	for _, proc := range allProcs {
		data, err := os.ReadFile(proc)
		if err != nil {
			return nil, errors.Wrap(err, "failed to read process file")
		}
		stat := string(data)
		if _, err := fmt.Sscanf(stat, "%d %s", &pid, &name); err != nil {
			return nil, errors.Wrapf(err, "failed to parse process stat file %q", stat)
		}
		if len(name) < 2 {
			continue
		}
		if subStr(name[1:len(name)-1], maxProcName) != subStr(procName, maxProcName) {
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
