//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system

import (
	"os"
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestSystem_getDistribution(t *testing.T) {
	for name, tc := range map[string]struct {
		fileMap map[string]string
		expDist Distribution
	}{
		"centos-7.9": {
			fileMap: map[string]string{
				"/etc/os-release":     "distros/centos7.9-os-rel",
				"/etc/centos-release": "distros/centos7.9-rel",
				"/proc/version":       "distros/centos7.9-proc-ver",
			},
			expDist: Distribution{
				ID:   "centos",
				Name: "CentOS Linux",
				Version: DistributionVersion{
					Major: 7,
					Minor: 9,
					Patch: 2009,
				},
				Kernel: KernelVersion{
					Major: 3,
					Minor: 10,
				},
			},
		},
		"centos-8.3": {
			fileMap: map[string]string{
				"/etc/os-release":     "distros/centos8.3-os-rel",
				"/etc/centos-release": "distros/centos8.3-rel",
				"/proc/version":       "distros/centos8.3-proc-ver",
			},
			expDist: Distribution{
				ID:   "centos",
				Name: "CentOS Linux",
				Version: DistributionVersion{
					Major: 8,
					Minor: 3,
					Patch: 2011,
				},
				Kernel: KernelVersion{
					Major: 4,
					Minor: 18,
				},
			},
		},
		"rocky-8.5": {
			fileMap: map[string]string{
				"/etc/os-release": "distros/rocky8.5-os-rel",
			},
			expDist: Distribution{
				ID:   "rocky",
				Name: "Rocky Linux",
				Version: DistributionVersion{
					Major: 8,
					Minor: 5,
				},
			},
		},
		"ubuntu-20.04": {
			fileMap: map[string]string{
				"/etc/os-release": "distros/ubuntu20.04-os-rel",
			},
			expDist: Distribution{
				ID:   "ubuntu",
				Name: "Ubuntu",
				Version: DistributionVersion{
					Major: 20,
					Minor: 4,
				},
			},
		},
		"opensuse-15.2": {
			fileMap: map[string]string{
				"/etc/os-release": "distros/opensuse15.2-os-rel",
			},
			expDist: Distribution{
				ID:   "opensuse-leap",
				Name: "openSUSE Leap",
				Version: DistributionVersion{
					Major: 15,
					Minor: 2,
				},
			},
		},
		"sles-15sp2": {
			fileMap: map[string]string{
				"/etc/os-release": "distros/sles15sp2-os-rel",
			},
			expDist: Distribution{
				ID:   "sles",
				Name: "SLES",
				Version: DistributionVersion{
					Major: 15,
					Minor: 2,
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			openFunc := func(name string) (*os.File, error) {
				f, ok := tc.fileMap[name]
				if !ok {
					return nil, os.ErrNotExist
				}

				return os.Open(f)
			}

			gotDist := getDistribution(openFunc)
			if diff := cmp.Diff(tc.expDist, gotDist); diff != "" {
				t.Fatalf("Unexpected distribution (-want +got):\n%s", diff)
			}
		})
	}
}
