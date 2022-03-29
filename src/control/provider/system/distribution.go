//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system

import (
	"bufio"
	"fmt"
	"os"
	"strconv"
	"strings"

	"github.com/pkg/errors"
)

type (
	// KernelVersion is the kernel version.
	KernelVersion struct {
		Major int
		Minor int
		Patch int
	}

	// DistributionVersion represents a version of a Linux distribution.
	DistributionVersion struct {
		StringVersion string `json:"string_version"`
		Major         int    `json:"major"`
		Minor         int    `json:"minor"`
		Patch         int    `json:"patch"`
	}

	// Distribution represents a Linux distribution.
	Distribution struct {
		ID      string              `json:"id"`
		Name    string              `json:"name"`
		Version DistributionVersion `json:"version"`
		Kernel  KernelVersion       `json:"kernel"`
	}

	openFunc func(string) (*os.File, error)
)

func (dv DistributionVersion) String() string {
	if dv.StringVersion != "" {
		return dv.StringVersion
	}
	return fmt.Sprintf("%d.%d.%d", dv.Major, dv.Minor, dv.Patch)
}

func (d Distribution) String() string {
	return fmt.Sprintf("%s (%s)", d.Name, d.Version)
}

func parseDistributionFile(df *os.File) (*Distribution, error) {
	distribution := new(Distribution)

	var key, val string
	scn := bufio.NewScanner(df)
	for scn.Scan() {
		keyVal := strings.Split(scn.Text(), "=")
		if len(keyVal) != 2 {
			continue
		}
		key = strings.TrimSpace(keyVal[0])
		val = strings.TrimSpace(strings.Trim(keyVal[1], `"`))

		switch key {
		case "ID":
			distribution.ID = val
		case "NAME":
			distribution.Name = val
		case "VERSION":
			if distribution.Version.StringVersion == "" {
				distribution.Version.StringVersion = val
			}
		case "VERSION_ID":
			distribution.Version.StringVersion = val
		}
	}
	if err := scn.Err(); err != nil {
		return nil, errors.Wrapf(err, "failed to parse %s", df.Name())
	}

	return distribution, nil
}

func parseMajorMinorPatch(version string) (maj int, min int, pat int) {
	mmp := strings.Split(version, ".")
	for i, v := range mmp {
		switch i {
		case 0:
			maj, _ = strconv.Atoi(v)
		case 1:
			min, _ = strconv.Atoi(v)
		case 2:
			pat, _ = strconv.Atoi(v)
		}
	}
	return
}

func getKernelVersion(kernel *KernelVersion, open openFunc) {
	f, err := open("/proc/version")
	if err != nil {
		return
	}
	defer f.Close()

	scn := bufio.NewScanner(f)
	for scn.Scan() {
		fields := strings.Split(scn.Text(), " ")
		for i, field := range fields {
			if field == "version" {
				kernel.Major, kernel.Minor, kernel.Patch = parseMajorMinorPatch(fields[i+1])
				break
			}
		}
		break
	}
}

func getDistributionRelease(fileName string, dv *DistributionVersion, open openFunc) {
	f, err := open(fileName)
	if err != nil {
		return
	}
	defer f.Close()

	scn := bufio.NewScanner(f)
	for scn.Scan() {
		fields := strings.Fields(scn.Text())
		for i, field := range fields {
			if field == "release" {
				dv.Major, dv.Minor, dv.Patch = parseMajorMinorPatch(fields[i+1])
				break
			}
		}
		break
	}
	if dv.Major > 0 {
		dv.StringVersion = ""
	}
}

func getDistribution(open openFunc) Distribution {
	defaultDistro := Distribution{
		ID:   "unknown",
		Name: "unknown",
	}

	f, err := open("/etc/os-release")
	if err != nil {
		return defaultDistro
	}
	defer f.Close()

	dist, err := parseDistributionFile(f)
	if err != nil {
		return defaultDistro
	}

	getKernelVersion(&dist.Kernel, open)

	switch dist.ID {
	// The centos and redhat cases here are actually targeted at only the 7.x
	// releases.  The default case should handle all EL8s.
	case "centos":
		getDistributionRelease("/etc/centos-release", &dist.Version, open)
	case "redhat":
		getDistributionRelease("/etc/redhat-release", &dist.Version, open)
	default:
		fields := strings.Fields(dist.Version.StringVersion)
		if len(fields) > 0 {
			v := &dist.Version
			v.Major, v.Minor, v.Patch = parseMajorMinorPatch(fields[0])
			if dist.Version.Major > 0 {
				dist.Version.StringVersion = ""
			}
		}
	}

	return *dist
}

// GetDistribution returns information about the current Linux distribution.
func GetDistribution() Distribution {
	return getDistribution(os.Open)
}
