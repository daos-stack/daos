//
// (C) Copyright 2022-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package scm

import (
	"fmt"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// TestNdctl_parseNamespaces verified expected output from ndctl utility
// can be converted into native storage ScmNamespaces type.
func TestNdctl_parseNamespaces(t *testing.T) {
	// template for `ndctl list -N` output
	listTmpl := `{
   "dev":"namespace%d.0",
   "mode":"fsdax",
   "map":"dev",
   "size":3183575302144,
   "uuid":"842fc847-28e0-4bb6-8dfc-d24afdba1528",
   "raw_uuid":"dedb4b28-dc4b-4ccd-b7d1-9bd475c91264",
   "sector_size":512,
   "blockdev":"pmem%d",
   "numa_node":%d
}`

	for name, tc := range map[string]struct {
		in            string
		expNamespaces storage.ScmNamespaces
		expErr        error
	}{
		"empty": {
			expNamespaces: storage.ScmNamespaces{},
		},
		"single": {
			in: fmt.Sprintf(listTmpl, 0, 0, 0),
			expNamespaces: storage.ScmNamespaces{
				{
					Name:        "namespace0.0",
					BlockDevice: "pmem0",
					NumaNode:    0,
					Size:        3183575302144,
					UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
				},
			},
		},
		"double": {
			in: strings.Join([]string{
				"[", fmt.Sprintf(listTmpl, 0, 0, 0), ",",
				fmt.Sprintf(listTmpl, 1, 1, 1), "]"}, ""),
			expNamespaces: storage.ScmNamespaces{
				{
					Name:        "namespace0.0",
					BlockDevice: "pmem0",
					NumaNode:    0,
					Size:        3183575302144,
					UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
				},
				{
					Name:        "namespace1.0",
					BlockDevice: "pmem1",
					NumaNode:    1,
					Size:        3183575302144,
					UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
				},
			},
		},
		"malformed": {
			in:     `{"dev":"foo`,
			expErr: errors.New("JSON input"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotNamespaces, gotErr := parseNamespaces(tc.in)

			test.CmpErr(t, tc.expErr, gotErr)
			if diff := cmp.Diff(tc.expNamespaces, gotNamespaces); diff != "" {
				t.Fatalf("unexpected namespace result (-want, +got):\n%s\n", diff)
			}
		})
	}
}

// TestNdctl_getNamespaces tests the internals of prepScm, pass in mock runCmd to verify
// behavior. Don't use mockPrepScm as we want to test prepScm logic.
func TestNdctl_getNamespaces(t *testing.T) {
	commands := []pmemCmd{} // external commands issued
	// ndctl create-namespace command return json format
	nsOut := `{
   "dev":"namespace%d.0",
   "mode":"fsdax",
   "map":"dev",
   "size":3183575302144,
   "uuid":"842fc847-28e0-4bb6-8dfc-d24afdba1528",
   "raw_uuid":"dedb4b28-dc4b-4ccd-b7d1-9bd475c91264",
   "sector_size":512,
   "blockdev":"pmem%d",
   "numa_node":%d
}
`
	oneNs, _ := parseNamespaces(fmt.Sprintf(nsOut, 1, 1, 0))
	twoNsJSON := "[" + fmt.Sprintf(nsOut, 1, 1, 0) + "," + fmt.Sprintf(nsOut, 2, 2, 1) + "]"
	twoNs, _ := parseNamespaces(twoNsJSON)

	for name, tc := range map[string]struct {
		expErrMsg      string
		cmdOut         string
		expNamespaces  storage.ScmNamespaces
		expCommands    []pmemCmd
		lookPathErrMsg string
	}{
		"no namespaces": {
			cmdOut:        "",
			expCommands:   []pmemCmd{cmdListNamespaces},
			expNamespaces: storage.ScmNamespaces{},
		},
		"single pmem device": {
			cmdOut:        fmt.Sprintf(nsOut, 1, 1, 0),
			expCommands:   []pmemCmd{cmdListNamespaces},
			expNamespaces: oneNs,
		},
		"two pmem device": {
			cmdOut:        twoNsJSON,
			expCommands:   []pmemCmd{cmdListNamespaces},
			expNamespaces: twoNs,
		},
		"ndctl not installed": {
			lookPathErrMsg: FaultMissingNdctl.Error(),
			expErrMsg:      FaultMissingNdctl.Error(),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mockLookPath := func(string) (s string, err error) {
				if tc.lookPathErrMsg != "" {
					err = errors.New(tc.lookPathErrMsg)
				}
				return
			}

			mockRun := func(_ logging.Logger, cmd pmemCmd) (string, error) {
				commands = append(commands, cmd)
				return tc.cmdOut, nil
			}

			commands = nil // reset to initial values between tests

			cr, err := newCmdRunner(log, nil, mockRun, mockLookPath)
			if err != nil {
				t.Fatal(err)
			}

			namespaces, err := cr.getNamespaces(sockAny)
			if err != nil {
				if tc.lookPathErrMsg != "" {
					test.ExpectError(t, err, tc.lookPathErrMsg, name)
					return
				}
				t.Fatal(name + ": GetPmemNamespaces: " + err.Error())
			}

			test.AssertEqual(t, commands, tc.expCommands, name+": unexpected list of commands run")
			test.AssertEqual(t, namespaces, tc.expNamespaces, name+": unexpected list of pmem device file names")
		})
	}
}

func TestNdctl_getNdctlRegions(t *testing.T) {
	commands := []pmemCmd{} // external commands issued
	for name, tc := range map[string]struct {
		expErr      error
		cmdOut      string
		expRegions  NdctlRegions
		expCommands []pmemCmd
		lookPathErr error
	}{
		"ndctl not installed": {
			lookPathErr: FaultMissingNdctl,
			expErr:      FaultMissingNdctl,
		},
		"no regions": {
			cmdOut:      "",
			expCommands: []pmemCmd{cmdListNdctlRegions},
			expRegions:  NdctlRegions{},
		},
		"two regions": {
			cmdOut:      ndctlRegionsDual,
			expCommands: []pmemCmd{cmdListNdctlRegions},
			expRegions: NdctlRegions{
				{
					Dev:               "region1",
					Size:              1082331758592,
					Align:             16777216,
					AvailableSize:     1082331758592,
					Type:              "pmem",
					NumaNode:          0,
					ISetID:            13312958398157623569,
					PersistenceDomain: "memory_controller",
				},
				{
					Dev:               "region0",
					Size:              1082331758592,
					Align:             16777216,
					AvailableSize:     1082331758592,
					Type:              "pmem",
					NumaNode:          1,
					ISetID:            13312958398157623568,
					PersistenceDomain: "memory_controller",
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mockLookPath := func(string) (string, error) {
				return "", tc.lookPathErr
			}

			mockRun := func(_ logging.Logger, cmd pmemCmd) (string, error) {
				commands = append(commands, cmd)
				return tc.cmdOut, nil
			}

			commands = nil // reset to initial values between tests

			cr, err := newCmdRunner(log, nil, mockRun, mockLookPath)
			if err != nil {
				t.Fatal(err)
			}

			gotRegions, gotErr := cr.getNdctlRegions(sockAny)
			test.CmpErr(t, tc.expErr, gotErr)
			if diff := cmp.Diff(tc.expRegions, gotRegions); diff != "" {
				t.Fatalf("unexpected result (-want, +got):\n%s\n", diff)
			}
		})
	}
}
