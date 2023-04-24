//
// (C) Copyright 2022-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package common

import (
	"os"
	"path/filepath"
	"strconv"
	"strings"

	"github.com/pkg/errors"
)

func readProcName(procPath string) (string, error) {
	data, err := os.ReadFile(procPath)
	if err != nil {
		return "", errors.Wrapf(err, "failed to read %q", procPath)
	}
	if len(data) == 0 {
		return "", errors.Wrapf(err, "%q was empty", procPath)
	}
	return filepath.Base(strings.Split(string(data), "\x00")[0]), nil
}

func getProcPids(procDir, searchName string) (pids []int, _ error) {
	if searchName == "" {
		return nil, nil
	}

	allProcs, err := filepath.Glob(filepath.Join(procDir, "*", "cmdline"))
	if err != nil {
		return nil, errors.Wrap(err, "failed to read process list")
	}

	for _, proc := range allProcs {
		procName, err := readProcName(proc)
		if err != nil {
			continue
		}

		if procName != searchName {
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
func GetProcPids(procName string) ([]int, error) {
	return getProcPids("/proc", procName)
}

func getProcName(pid int, procDir string) (string, error) {
	return readProcName(filepath.Join(procDir, strconv.Itoa(pid), "cmdline"))
}

// GetProcName returns the name of the process with the given pid.
func GetProcName(pid int) (string, error) {
	return getProcName(pid, "/proc")
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
