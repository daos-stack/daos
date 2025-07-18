//
// (C) Copyright 2019-2024 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"bufio"
	"fmt"
	"os"
	"os/user"
	"path/filepath"
	"strings"
	"testing"

	"github.com/dustin/go-humanize"
	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
)

func Test_Dmg_PoolTierRatioFlag(t *testing.T) {
	for name, tc := range map[string]struct {
		input     string
		expRatios []float64
		expString string
		expErr    error
	}{
		"empty": {
			expErr: errors.New("no tier ratio specified"),
		},
		"invalid": {
			input:  "ABCD",
			expErr: errors.New("invalid tier ratio \"ABCD\""),
		},
		"less than 100%": {
			input:  "10,80",
			expErr: errors.New("must add up to"),
		},
		"total more than 100%": {
			input:  "30,90",
			expErr: errors.New("must add up to"),
		},
		"non-numeric": {
			input:  "0.3,foo",
			expErr: errors.New("invalid"),
		},
		"negative adds up to 100": {
			input:  "-30,130",
			expErr: errors.New("0-100"),
		},
		"0,0": {
			input:  "0,0",
			expErr: errors.New("must add up to"),
		},
		"%,%": {
			input:  "%,%",
			expErr: errors.New("invalid"),
		},
		"defaults": {
			input:     "defaults",
			expRatios: defaultTierRatios,
			expString: func() string {
				rStrs := make([]string, len(defaultTierRatios))
				for i, ratio := range defaultTierRatios {
					rStrs[i] = pretty.PrintTierRatio(ratio)
				}
				return strings.Join(rStrs, ",")
			}(),
		},
		"0": {
			input:     "0",
			expRatios: []float64{0, 1},
			expString: "0.00%,100.00%",
		},
		"100": {
			input:     "100",
			expRatios: []float64{1},
			expString: "100.00%",
		},
		"single tier padded": {
			input:     "45.3",
			expRatios: []float64{0.453, 0.547},
			expString: "45.30%,54.70%",
		},
		"valid two tiers": {
			input:     "0.3,99.7",
			expRatios: []float64{0.003, 0.997},
			expString: "0.30%,99.70%",
		},
		"valid three tiers": {
			input:     "0.3,69.7,30.0",
			expRatios: []float64{0.003, 0.697, 0.3},
			expString: "0.30%,69.70%,30.00%",
		},
		"valid with %": {
			input:     "7 %,93%",
			expRatios: []float64{0.07, 0.93},
			expString: "7.00%,93.00%",
		},
	} {
		t.Run(name, func(t *testing.T) {
			var trf tierRatioFlag
			if tc.input != "defaults" {
				err := trf.UnmarshalFlag(tc.input)
				test.CmpErr(t, tc.expErr, err)
				if err != nil {
					return
				}
			}
			cmpOpts := []cmp.Option{
				cmpopts.EquateApprox(0.01, 0),
			}
			if diff := cmp.Diff(tc.expRatios, trf.Ratios(), cmpOpts...); diff != "" {
				t.Fatalf("unexpected tier ratio (-want, +got):\n%s\n", diff)
			}
			if diff := cmp.Diff(tc.expString, trf.String()); diff != "" {
				t.Fatalf("unexpected string (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func createACLFile(t *testing.T, dir string, acl *control.AccessControlList) string {
	t.Helper()

	return test.CreateTestFile(t, dir, control.FormatACLDefault(acl))
}

func TestPoolCommands(t *testing.T) {
	testSizeStr := "512GiB"
	testSize := 549755813888
	eUsr, err := user.Current()
	if err != nil {
		t.Fatal(err)
	}
	eGrp, err := user.LookupGroupId(eUsr.Gid)
	if err != nil {
		t.Fatal(err)
	}

	tmpDir, tmpCleanup := test.CreateTestDir(t)
	defer tmpCleanup()

	// Some tests need a valid ACL file
	testACL := &control.AccessControlList{
		Entries: []string{"A::OWNER@:rw", "A:G:GROUP@:rw"},
	}
	testACLFile := createACLFile(t, tmpDir, testACL)

	// An existing file with contents for tests that need to verify overwrite
	testExistingFile := createACLFile(t, tmpDir, testACL)

	// An existing file with write-only perms
	testWriteOnlyFile := createACLFile(t, tmpDir, testACL)
	err = os.Chmod(testWriteOnlyFile, 0222)
	if err != nil {
		t.Fatalf("Couldn't set file writable only")
	}

	testEmptyFile := test.CreateTestFile(t, tmpDir, "")

	// Subdirectory with no write perms
	testNoPermDir := filepath.Join(tmpDir, "badpermsdir")
	if err := os.Mkdir(testNoPermDir, 0444); err != nil {
		t.Fatal(err)
	}

	propWithVal := func(key, val string) *daos.PoolProperty {
		hdlr := daos.PoolProperties()[key]
		prop := hdlr.GetProperty(key)
		if val != "" {
			if err := prop.SetValue(val); err != nil {
				panic(err)
			}
		}
		return prop
	}

	setQueryMask := func(xfrm func(qm *daos.PoolQueryMask)) daos.PoolQueryMask {
		qm := daos.DefaultPoolQueryMask
		xfrm(&qm)
		return qm
	}

	runCmdTests(t, []cmdTest{
		{
			"Pool create with extra argument",
			fmt.Sprintf("pool create --size %s foo bar", testSizeStr),
			"",
			errors.New("unexpected"),
		},
		{
			"Create pool with label prop and argument",
			fmt.Sprintf("pool create --size %s --properties label:foo foo", testSizeStr),
			"",
			errors.New("can't set label property"),
		},
		{
			"Create pool with invalid label",
			fmt.Sprintf("pool create --size %s alfalfa!", testSizeStr),
			"",
			errors.New("invalid label"),
		},
		{
			"Create pool with label argument",
			fmt.Sprintf("pool create --size %s foo", testSizeStr),
			strings.Join([]string{
				printRequest(t, &control.PoolCreateReq{
					TotalBytes: uint64(testSize),
					TierRatio:  []float64{0.06, 0.94},
					User:       eUsr.Username + "@",
					UserGroup:  eGrp.Name + "@",
					Ranks:      []ranklist.Rank{},
					Properties: []*daos.PoolProperty{
						propWithVal("label", "foo"),
					},
				}),
			}, " "),
			nil,
		},
		{
			"Create pool with missing size",
			"pool create label",
			"",
			errPoolCreateIncompatOpts,
		},
		{
			"Create pool with missing label",
			fmt.Sprintf("pool create --size %s", testSizeStr),
			"",
			errors.New("required argument"),
		},
		{
			"Create pool with incompatible arguments (auto nvme-size)",
			fmt.Sprintf("pool create label --size %s --nvme-size %s", testSizeStr, testSizeStr),
			"",
			errPoolCreateIncompatOpts,
		},
		{
			"Create pool with incompatible arguments (auto scm-size)",
			fmt.Sprintf("pool create label --size %s --scm-size %s", testSizeStr, testSizeStr),
			"",
			errPoolCreateIncompatOpts,
		},
		{
			"Create pool with incompatible arguments (% size nranks)",
			"pool create label --size 100% --nranks 16",
			"",
			errors.New("--size=% may not be mixed with --nranks"),
		},
		{
			"Create pool with incompatible arguments (% size tier-ratio)",
			"pool create label --size 100% --tier-ratio 16",
			"",
			errors.New("--size=% may not be mixed with --tier-ratio"),
		},
		{
			"Create pool with invalid arguments (too small ratio)",
			"pool create label --size=0%",
			"",
			errors.New("Creating DAOS pool with invalid full size ratio"),
		},
		{
			"Create pool with invalid arguments (too big ratio)",
			"pool create label --size=101%",
			"",
			errors.New("Creating DAOS pool with invalid full size ratio"),
		},
		{
			"Create pool with incompatible rank arguments (auto)",
			fmt.Sprintf("pool create label --size %s --nranks 16 --ranks 1,2,3", testSizeStr),
			"",
			errors.New("may not be mixed"),
		},
		{
			"Create pool with incompatible arguments (auto with meta-size)",
			fmt.Sprintf("pool create label --size %s --meta-size 32G", testSizeStr),
			"",
			errPoolCreateIncompatOpts,
		},
		{
			"Create pool with incompatible arguments (scm-size with meta-size)",
			fmt.Sprintf("pool create label --scm-size %s --meta-size 32G", testSizeStr),
			"",
			errPoolCreateIncompatOpts,
		},
		{
			"Create pool with incompatible arguments (scm-size with data-size)",
			fmt.Sprintf("pool create label --scm-size %s --data-size 32G", testSizeStr),
			"",
			errPoolCreateIncompatOpts,
		},
		{
			"Create pool with too-large tier-ratio (auto)",
			fmt.Sprintf("pool create label --size %s --tier-ratio 200", testSizeStr),
			"",
			errors.New("0-100"),
		},
		{
			"Create pool with too-small tier-ratio (auto)",
			fmt.Sprintf("pool create label --size %s --tier-ratio 30,30", testSizeStr),
			"",
			errors.New("100"),
		},
		{
			"Create pool with single tier-ratio (auto)",
			fmt.Sprintf("pool create label --size %s --tier-ratio 10", testSizeStr),
			strings.Join([]string{
				printRequest(t, &control.PoolCreateReq{
					TotalBytes: uint64(testSize),
					TierRatio:  []float64{0.1, 0.9},
					User:       eUsr.Username + "@",
					UserGroup:  eGrp.Name + "@",
					Ranks:      []ranklist.Rank{},
					Properties: []*daos.PoolProperty{
						propWithVal("label", "label"),
					},
				}),
			}, " "),
			nil,
		},
		{
			"Create pool with fine-grained tier-ratios (auto)",
			fmt.Sprintf("pool create label --size %s --tier-ratio 3.23,96.77", testSizeStr),
			strings.Join([]string{
				printRequest(t, &control.PoolCreateReq{
					TotalBytes: uint64(testSize),
					TierRatio:  []float64{0.0323, 0.9677},
					User:       eUsr.Username + "@",
					UserGroup:  eGrp.Name + "@",
					Ranks:      []ranklist.Rank{},
					Properties: []*daos.PoolProperty{
						propWithVal("label", "label"),
					},
				}),
			}, " "),
			nil,
		},
		{
			"Create pool with really fine-grained tier-ratios (auto; rounded)",
			fmt.Sprintf("pool create label --size %s --tier-ratio 23.725738953,76.274261047", testSizeStr),
			strings.Join([]string{
				printRequest(t, &control.PoolCreateReq{
					TotalBytes: uint64(testSize),
					TierRatio:  []float64{0.2373, 0.7626999999999999},
					User:       eUsr.Username + "@",
					UserGroup:  eGrp.Name + "@",
					Ranks:      []ranklist.Rank{},
					Properties: []*daos.PoolProperty{
						propWithVal("label", "label"),
					},
				}),
			}, " "),
			nil,
		},
		{
			"Create pool with incompatible arguments (manual)",
			fmt.Sprintf("pool create label --scm-size %s --nranks 42", testSizeStr),
			"",
			errors.New("may not be mixed"),
		},
		{
			"Create pool with incompatible arguments (-s -t)",
			fmt.Sprintf("pool create label --scm-size %s --tier-ratio 6", testSizeStr),
			"",
			errors.New("may not be mixed"),
		},
		{
			"Create pool with incompatible arguments (-n without -s)",
			fmt.Sprintf("pool create label --nvme-size %s", testSizeStr),
			"",
			errors.New("cannot be set without --scm-size"),
		},
		{
			"Create pool with minimal arguments",
			fmt.Sprintf("pool create label --scm-size %s --nsvc 3", testSizeStr),
			strings.Join([]string{
				printRequest(t, &control.PoolCreateReq{
					NumSvcReps: 3,
					User:       eUsr.Username + "@",
					UserGroup:  eGrp.Name + "@",
					Ranks:      []ranklist.Rank{},
					TierBytes:  []uint64{uint64(testSize), 0},
					Properties: []*daos.PoolProperty{
						propWithVal("label", "label"),
					},
				}),
			}, " "),
			nil,
		},
		{
			"Create pool with manual memory file ratio; legacy syntax",
			fmt.Sprintf("pool create label --scm-size %s --mem-ratio 0.25",
				testSizeStr),
			"",
			errors.New("may not be mixed"),
		},
		{
			"Create pool with default memory file ratio; MD-on-SSD syntax",
			fmt.Sprintf("pool create label --meta-size %s --data-size 1024G",
				testSizeStr),
			strings.Join([]string{
				printRequest(t, &control.PoolCreateReq{
					User:      eUsr.Username + "@",
					UserGroup: eGrp.Name + "@",
					Ranks:     []ranklist.Rank{},
					TierBytes: []uint64{
						uint64(testSize),
						1024 * humanize.GByte,
					},
					MemRatio: 1,
					Properties: []*daos.PoolProperty{
						propWithVal("label", "label"),
					},
				}),
			}, " "),
			nil,
		},
		{
			"Create pool with manual memory file ratio; MD-on-SSD syntax; single value",
			fmt.Sprintf("pool create label --meta-size %s --data-size 1024G --mem-ratio 25.5",
				testSizeStr),
			strings.Join([]string{
				printRequest(t, &control.PoolCreateReq{
					User:      eUsr.Username + "@",
					UserGroup: eGrp.Name + "@",
					Ranks:     []ranklist.Rank{},
					TierBytes: []uint64{
						uint64(testSize),
						1024 * humanize.GByte,
					},
					MemRatio: 0.255,
					Properties: []*daos.PoolProperty{
						propWithVal("label", "label"),
					},
				}),
			}, " "),
			nil,
		},
		{
			"Create pool with manual memory file ratio; MD-on-SSD syntax; both tiers",
			fmt.Sprintf("pool create label --meta-size %s --data-size 1024G --mem-ratio 25.5,74.5",
				testSizeStr),
			strings.Join([]string{
				printRequest(t, &control.PoolCreateReq{
					User:      eUsr.Username + "@",
					UserGroup: eGrp.Name + "@",
					Ranks:     []ranklist.Rank{},
					TierBytes: []uint64{
						uint64(testSize),
						1024 * humanize.GByte,
					},
					MemRatio: 0.255,
					Properties: []*daos.PoolProperty{
						propWithVal("label", "label"),
					},
				}),
			}, " "),
			nil,
		},
		{
			"Create pool with manual memory file ratio; MD-on-SSD syntax; three tiers",
			fmt.Sprintf("pool create label --meta-size %s --data-size 1024G --mem-ratio 25.5,25.5,49",
				testSizeStr),
			"",
			errors.New("unexpected mem-ratio"),
		},
		{
			"Create pool with manual memory file ratio; MD-on-SSD syntax; 100% tier",
			fmt.Sprintf("pool create label --meta-size %s --data-size 1024G --mem-ratio 100",
				testSizeStr),
			strings.Join([]string{
				printRequest(t, &control.PoolCreateReq{
					User:      eUsr.Username + "@",
					UserGroup: eGrp.Name + "@",
					Ranks:     []ranklist.Rank{},
					TierBytes: []uint64{
						uint64(testSize),
						1024 * humanize.GByte,
					},
					MemRatio: 1,
					Properties: []*daos.PoolProperty{
						propWithVal("label", "label"),
					},
				}),
			}, " "),
			nil,
		},
		{
			"Create pool with manual ranks",
			fmt.Sprintf("pool create label --size %s --ranks 1,2", testSizeStr),
			strings.Join([]string{
				printRequest(t, &control.PoolCreateReq{
					User:       eUsr.Username + "@",
					UserGroup:  eGrp.Name + "@",
					Ranks:      []ranklist.Rank{1, 2},
					TotalBytes: uint64(testSize),
					TierRatio:  []float64{0.06, 0.94},
					Properties: []*daos.PoolProperty{
						propWithVal("label", "label"),
					},
				}),
			}, " "),
			nil,
		},
		{
			"Create pool with auto storage parameters",
			fmt.Sprintf("pool create label --size %s --tier-ratio 2,98 --nranks 8", testSizeStr),
			strings.Join([]string{
				printRequest(t, &control.PoolCreateReq{
					TotalBytes: uint64(testSize),
					TierRatio:  []float64{0.02, 0.98},
					NumRanks:   8,
					User:       eUsr.Username + "@",
					UserGroup:  eGrp.Name + "@",
					Ranks:      []ranklist.Rank{},
					Properties: []*daos.PoolProperty{
						propWithVal("label", "label"),
					},
				}),
			}, " "),
			nil,
		},
		{
			"Create pool with user and group domains",
			fmt.Sprintf("pool create label --scm-size %s --nsvc 3 --user foo@home --group bar@home", testSizeStr),
			strings.Join([]string{
				printRequest(t, &control.PoolCreateReq{
					NumSvcReps: 3,
					User:       "foo@home",
					UserGroup:  "bar@home",
					Ranks:      []ranklist.Rank{},
					TierBytes:  []uint64{uint64(testSize), 0},
					Properties: []*daos.PoolProperty{
						propWithVal("label", "label"),
					},
				}),
			}, " "),
			nil,
		},
		{
			"Create pool with user but no group",
			fmt.Sprintf("pool create label --scm-size %s --nsvc 3 --user foo", testSizeStr),
			strings.Join([]string{
				printRequest(t, &control.PoolCreateReq{
					NumSvcReps: 3,
					User:       "foo@",
					UserGroup:  eGrp.Name + "@",
					Ranks:      []ranklist.Rank{},
					TierBytes:  []uint64{uint64(testSize), 0},
					Properties: []*daos.PoolProperty{
						propWithVal("label", "label"),
					},
				}),
			}, " "),
			nil,
		},
		{
			"Create pool with group but no user",
			fmt.Sprintf("pool create label --scm-size %s --nsvc 3 --group foo", testSizeStr),
			strings.Join([]string{
				printRequest(t, &control.PoolCreateReq{
					NumSvcReps: 3,
					User:       eUsr.Username + "@",
					UserGroup:  "foo@",
					Ranks:      []ranklist.Rank{},
					TierBytes:  []uint64{uint64(testSize), 0},
					Properties: []*daos.PoolProperty{
						propWithVal("label", "label"),
					},
				}),
			}, " "),
			nil,
		},
		{
			"Create pool with invalid ACL file",
			fmt.Sprintf("pool create label --scm-size %s --acl-file /not/a/real/file", testSizeStr),
			"",
			dmgTestErr("opening ACL file: open /not/a/real/file: no such file or directory"),
		},
		{
			"Create pool with empty ACL file",
			fmt.Sprintf("pool create label --scm-size %s --acl-file %s", testSizeStr, testEmptyFile),
			"",
			dmgTestErr(fmt.Sprintf("ACL file '%s' contains no entries", testEmptyFile)),
		},
		{
			"Create pool with scrubbing",
			fmt.Sprintf("pool create label --scm-size %s --properties=scrub:timed,scrub_freq:1", testSizeStr),
			strings.Join([]string{
				printRequest(t, &control.PoolCreateReq{
					Properties: []*daos.PoolProperty{
						propWithVal("scrub", "timed"),
						propWithVal("scrub_freq", "1"),
						propWithVal("label", "label"),
					},
					User:      eUsr.Username + "@",
					UserGroup: eGrp.Name + "@",
					Ranks:     []ranklist.Rank{},
					TierBytes: []uint64{uint64(testSize), 0},
				}),
			}, " "),
			nil,
		},
		// Exclude testing with multiple ranks is verified at the control API layer.
		{
			"Exclude a target with single target idx",
			"pool exclude 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --ranks 0 --target-idx 1",
			strings.Join([]string{
				printRequest(t, &control.PoolRanksReq{
					ID:        "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
					Ranks:     []ranklist.Rank{0},
					TargetIdx: []uint32{1},
					Force:     false,
				}),
			}, " "),
			nil,
		},
		{
			"Exclude a target with multiple idx",
			"pool exclude -f 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --ranks 0 --target-idx 1,2,3",
			strings.Join([]string{
				printRequest(t, &control.PoolRanksReq{
					ID:        "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
					Ranks:     []ranklist.Rank{0},
					TargetIdx: []uint32{1, 2, 3},
					Force:     true,
				}),
			}, " "),
			nil,
		},
		{
			"Exclude a target with no idx given",
			"pool exclude -f 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --ranks 0",
			strings.Join([]string{
				printRequest(t, &control.PoolRanksReq{
					ID:        "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
					Ranks:     []ranklist.Rank{0},
					TargetIdx: []uint32{},
					Force:     true,
				}),
			}, " "),
			nil,
		},
		// Drain testing with multiple ranks is verified at the control API layer.
		{
			"Drain a target with single target idx",
			"pool drain 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --ranks 0 --target-idx 1",
			strings.Join([]string{
				printRequest(t, &control.PoolRanksReq{
					ID:        "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
					Ranks:     []ranklist.Rank{0},
					TargetIdx: []uint32{1},
				}),
			}, " "),
			nil,
		},
		{
			"Drain a target with multiple idx",
			"pool drain 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --ranks 0 --target-idx 1,2,3",
			strings.Join([]string{
				printRequest(t, &control.PoolRanksReq{
					ID:        "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
					Ranks:     []ranklist.Rank{0},
					TargetIdx: []uint32{1, 2, 3},
				}),
			}, " "),
			nil,
		},
		{
			"Drain a target with no idx given",
			"pool drain 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --ranks 0",
			strings.Join([]string{
				printRequest(t, &control.PoolRanksReq{
					ID:        "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
					Ranks:     []ranklist.Rank{0},
					TargetIdx: []uint32{},
				}),
			}, " "),
			nil,
		},
		/* TODO: Tests need to be fixed after pull pool info */
		{
			"Extend pool with missing arguments",
			"pool extend",
			"",
			errMissingFlag,
		},
		{
			"Extend a pool with a single rank",
			fmt.Sprintf("pool extend 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --ranks=1"),
			strings.Join([]string{
				printRequest(t, &control.PoolExtendReq{
					ID:    "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
					Ranks: []ranklist.Rank{1},
				}),
			}, " "),
			nil,
		},
		{
			"Extend a pool with multiple ranks",
			fmt.Sprintf("pool extend 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --ranks=1,2,3"),
			strings.Join([]string{
				printRequest(t, &control.PoolExtendReq{
					ID:    "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
					Ranks: []ranklist.Rank{1, 2, 3},
				}),
			}, " "),
			nil,
		},
		// Reintegrate testing with multiple ranks is verified at the control API layer.
		{
			"Reintegrate a target with single target idx",
			"pool reintegrate 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --ranks 0 --target-idx 1",
			strings.Join([]string{
				printRequest(t, &control.PoolRanksReq{
					ID:        "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
					Ranks:     []ranklist.Rank{0},
					TargetIdx: []uint32{1},
				}),
			}, " "),
			nil,
		},
		{
			"Reintegrate a target with multiple idx",
			"pool reintegrate 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --ranks 0 --target-idx 1,2,3",
			strings.Join([]string{
				printRequest(t, &control.PoolRanksReq{
					ID:        "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
					Ranks:     []ranklist.Rank{0},
					TargetIdx: []uint32{1, 2, 3},
				}),
			}, " "),
			nil,
		},
		{
			"Reintegrate a target with no idx given",
			"pool reintegrate 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --ranks 0",
			strings.Join([]string{
				printRequest(t, &control.PoolRanksReq{
					ID:        "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
					Ranks:     []ranklist.Rank{0},
					TargetIdx: []uint32{},
				}),
			}, " "),
			nil,
		},
		{
			"Destroy pool with force",
			"pool destroy 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --force",
			strings.Join([]string{
				printRequest(t, &control.PoolDestroyReq{
					ID:    "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
					Force: true,
				}),
			}, " "),
			nil,
		},
		{
			"Destroy pool with recursive",
			"pool destroy 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --recursive",
			strings.Join([]string{
				printRequest(t, &control.PoolDestroyReq{
					ID:        "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
					Recursive: true,
				}),
			}, " "),
			nil,
		},
		{
			"Evict pool",
			"pool evict 031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
			strings.Join([]string{
				printRequest(t, &control.PoolEvictReq{
					ID: "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
				}),
			}, " "),
			nil,
		},
		{
			"List pools",
			"pool list",
			strings.Join([]string{
				printRequest(t, &control.ListPoolsReq{}),
			}, " "),
			nil,
		},
		{
			"List pools with verbose flag",
			"pool list --verbose",
			strings.Join([]string{
				printRequest(t, &control.ListPoolsReq{}),
			}, " "),
			nil,
		},
		{
			"Set pool properties",
			"pool set-prop 031bcaf8-f0f5-42ef-b3c5-ee048676dceb label:foo,space_rb:42",
			strings.Join([]string{
				printRequest(t, &control.PoolSetPropReq{
					ID: "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
					Properties: []*daos.PoolProperty{
						propWithVal("label", "foo"),
						propWithVal("space_rb", "42"),
					},
				}),
			}, " "),
			nil,
		},
		{
			"Set pool properties with pool flag",
			"pool set-prop 031bcaf8-f0f5-42ef-b3c5-ee048676dceb label:foo,space_rb:42",
			strings.Join([]string{
				printRequest(t, &control.PoolSetPropReq{
					ID: "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
					Properties: []*daos.PoolProperty{
						propWithVal("label", "foo"),
						propWithVal("space_rb", "42"),
					},
				}),
			}, " "),
			nil,
		},
		{
			"Set pool rd_fac property",
			"pool set-prop 031bcaf8-f0f5-42ef-b3c5-ee048676dceb rd_fac:1",
			strings.Join([]string{
				printRequest(t, &control.PoolSetPropReq{
					ID: "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
					Properties: []*daos.PoolProperty{
						propWithVal("rd_fac", "1"),
					},
				}),
			}, " "),
			nil,
		},
		{
			"Set pool property invalid property",
			"pool set-prop 031bcaf8-f0f5-42ef-b3c5-ee048676dceb whoops:foo",
			"",
			errors.New("not a settable property"),
		},
		{
			"Set pool property missing value",
			"pool set-prop 031bcaf8-f0f5-42ef-b3c5-ee048676dceb label:",
			"",
			errors.New("invalid property"),
		},
		{
			"Set pool property bad value",
			"pool set-prop 031bcaf8-f0f5-42ef-b3c5-ee048676dceb reclaim:all",
			"",
			errors.New("invalid value"),
		},
		{
			"Set pool perf_domain property is not allowed",
			"pool set-prop 031bcaf8-f0f5-42ef-b3c5-ee048676dceb perf_domain:root",
			"",
			errors.New("can't set perf_domain on existing pool."),
		},
		{
			"Set pool ec_pda property is not allowed",
			"pool set-prop 031bcaf8-f0f5-42ef-b3c5-ee048676dceb ec_pda:1",
			"",
			errors.New("can't set EC performance domain affinity on existing pool."),
		},
		{
			"Set pool rp_pda property is not allowed",
			"pool set-prop 031bcaf8-f0f5-42ef-b3c5-ee048676dceb rp_pda:1",
			"",
			errors.New("can't set RP performance domain affinity on existing pool"),
		},
		{
			"Get pool property",
			"pool get-prop 031bcaf8-f0f5-42ef-b3c5-ee048676dceb label",
			strings.Join([]string{
				printRequest(t, &control.PoolGetPropReq{
					ID: "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
					Properties: []*daos.PoolProperty{
						propWithVal("label", ""),
					},
				}),
			}, " "),
			nil,
		},
		{
			"Get pool ACL",
			"pool get-acl 031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
			strings.Join([]string{
				printRequest(t, &control.PoolGetACLReq{
					ID: "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
				}),
			}, " "),
			nil,
		},
		{
			"Get pool ACL with verbose flag",
			"pool get-acl 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --verbose",
			strings.Join([]string{
				printRequest(t, &control.PoolGetACLReq{
					ID: "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
				}),
			}, " "),
			nil,
		},
		{
			"Get pool ACL with output to bad file",
			"pool get-acl 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --outfile /foo/bar/acl.txt",
			strings.Join([]string{
				printRequest(t, &control.PoolGetACLReq{
					ID: "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
				}),
			}, " "),
			errors.New("open /foo/bar/acl.txt: no such file or directory"),
		},
		{
			"Get pool ACL with output to existing file",
			fmt.Sprintf("pool get-acl 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --outfile %s", testExistingFile),
			strings.Join([]string{
				printRequest(t, &control.PoolGetACLReq{
					ID: "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
				}),
			}, " "),
			errors.New(fmt.Sprintf("file already exists: %s", testExistingFile)),
		},
		{
			"Get pool ACL with output to existing file with write-only perms",
			fmt.Sprintf("pool get-acl 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --outfile %s", testWriteOnlyFile),
			strings.Join([]string{
				printRequest(t, &control.PoolGetACLReq{
					ID: "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
				}),
			}, " "),
			errors.New(fmt.Sprintf("file already exists: %s", testWriteOnlyFile)),
		},
		{
			"Get pool ACL with output to existing file with force",
			fmt.Sprintf("pool get-acl 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --outfile %s --force", testExistingFile),
			strings.Join([]string{
				printRequest(t, &control.PoolGetACLReq{
					ID: "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
				}),
			}, " "),
			nil,
		},
		{
			"Get pool ACL with output to directory with no write perms",
			fmt.Sprintf("pool get-acl 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --outfile %s", filepath.Join(testNoPermDir, "out.txt")),
			strings.Join([]string{
				printRequest(t, &control.PoolGetACLReq{
					ID: "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
				}),
			}, " "),
			errors.New(fmt.Sprintf("open %s: permission denied", filepath.Join(testNoPermDir, "out.txt"))),
		},
		{
			"Overwrite pool ACL with invalid ACL file",
			"pool overwrite-acl 12345678-1234-1234-1234-1234567890ab --acl-file /not/a/real/file",
			"",
			dmgTestErr("opening ACL file: open /not/a/real/file: no such file or directory"),
		},
		{
			"Overwrite pool ACL with empty ACL file",
			fmt.Sprintf("pool overwrite-acl 12345678-1234-1234-1234-1234567890ab --acl-file %s", testEmptyFile),
			"",
			dmgTestErr(fmt.Sprintf("ACL file '%s' contains no entries", testEmptyFile)),
		},
		{
			"Overwrite pool ACL",
			fmt.Sprintf("pool overwrite-acl 12345678-1234-1234-1234-1234567890ab --acl-file %s", testACLFile),
			strings.Join([]string{
				printRequest(t, &control.PoolOverwriteACLReq{
					ID:  "12345678-1234-1234-1234-1234567890ab",
					ACL: testACL,
				}),
			}, " "),
			nil,
		},
		{
			"Update pool ACL with invalid ACL file",
			"pool update-acl 12345678-1234-1234-1234-1234567890ab --acl-file /not/a/real/file",
			"",
			dmgTestErr("opening ACL file: open /not/a/real/file: no such file or directory"),
		},
		{
			"Update pool ACL with empty ACL file",
			fmt.Sprintf("pool update-acl 12345678-1234-1234-1234-1234567890ab --acl-file %s", testEmptyFile),
			"",
			dmgTestErr(fmt.Sprintf("ACL file '%s' contains no entries", testEmptyFile)),
		},
		{
			"Update pool ACL without file or entry",
			"pool update-acl 12345678-1234-1234-1234-1234567890ab",
			"",
			dmgTestErr("either ACL file or entry parameter is required"),
		},
		{
			"Update pool ACL with both file and entry",
			fmt.Sprintf("pool update-acl 12345678-1234-1234-1234-1234567890ab --acl-file %s --entry A::user@:rw", testACLFile),
			"",
			dmgTestErr("either ACL file or entry parameter is required"),
		},
		{
			"Update pool ACL with ACL file",
			fmt.Sprintf("pool update-acl 12345678-1234-1234-1234-1234567890ab --acl-file %s", testACLFile),
			strings.Join([]string{
				printRequest(t, &control.PoolUpdateACLReq{
					ID:  "12345678-1234-1234-1234-1234567890ab",
					ACL: testACL,
				}),
			}, " "),
			nil,
		},
		{
			"Update pool ACL with entry",
			"pool update-acl 12345678-1234-1234-1234-1234567890ab --entry A::user@:rw",
			strings.Join([]string{
				printRequest(t, &control.PoolUpdateACLReq{
					ID:  "12345678-1234-1234-1234-1234567890ab",
					ACL: &control.AccessControlList{Entries: []string{"A::user@:rw"}},
				}),
			}, " "),
			nil,
		},
		{
			"Delete pool ACL without principal flag",
			"pool delete-acl 12345678-1234-1234-1234-1234567890ab",
			"",
			dmgTestErr("the required flag `-p, --principal' was not specified"),
		},
		{
			"Delete pool ACL",
			"pool delete-acl 12345678-1234-1234-1234-1234567890ab --principal OWNER@",
			strings.Join([]string{
				printRequest(t, &control.PoolDeleteACLReq{
					ID:        "12345678-1234-1234-1234-1234567890ab",
					Principal: "OWNER@",
				}),
			}, " "),
			nil,
		},
		{
			"Query pool targets no arguments",
			"pool query-targets mypool",
			"",
			errMissingFlag,
		},
		{
			"Query pool targets no rank argument",
			"pool query-targets mypool --target-idx=1",
			"",
			errMissingFlag,
		},
		{
			"Query pool targets specific indices",
			"pool query-targets mypool --rank=1 --target-idx=1,3",
			strings.Join([]string{
				printRequest(t, &control.PoolQueryTargetReq{
					ID:      "mypool",
					Rank:    1,
					Targets: []uint32{1, 3},
				}),
			}, " "),
			nil,
		},
		{
			"Query pool targets all indices; pool query fails",
			"pool query-targets mypool --rank=1",
			strings.Join([]string{
				printRequest(t, &control.PoolQueryReq{
					ID:        "mypool",
					QueryMask: daos.DefaultPoolQueryMask,
				}),
			}, " "),
			errors.New("pool query"),
		},
		{
			"Query pool with UUID",
			"pool query 12345678-1234-1234-1234-1234567890ab",
			strings.Join([]string{
				printRequest(t, &control.PoolQueryReq{
					ID:        "12345678-1234-1234-1234-1234567890ab",
					QueryMask: daos.DefaultPoolQueryMask,
				}),
			}, " "),
			nil,
		},
		{
			"Query pool with UUID and enabled ranks",
			"pool query --show-enabled 12345678-1234-1234-1234-1234567890ab",
			strings.Join([]string{
				printRequest(t, &control.PoolQueryReq{
					ID:        "12345678-1234-1234-1234-1234567890ab",
					QueryMask: setQueryMask(func(qm *daos.PoolQueryMask) { qm.SetOptions(daos.PoolQueryOptionEnabledEngines) }),
				}),
			}, " "),
			nil,
		},
		{
			"Query pool with UUID and enabled ranks",
			"pool query -e 12345678-1234-1234-1234-1234567890ab",
			strings.Join([]string{
				printRequest(t, &control.PoolQueryReq{
					ID:        "12345678-1234-1234-1234-1234567890ab",
					QueryMask: setQueryMask(func(qm *daos.PoolQueryMask) { qm.SetOptions(daos.PoolQueryOptionEnabledEngines) }),
				}),
			}, " "),
			nil,
		},
		{
			"Query pool for health only",
			"pool query --health-only 12345678-1234-1234-1234-1234567890ab",
			strings.Join([]string{
				printRequest(t, &control.PoolQueryReq{
					ID:        "12345678-1234-1234-1234-1234567890ab",
					QueryMask: setQueryMask(func(qm *daos.PoolQueryMask) { *qm = daos.HealthOnlyPoolQueryMask }),
				}),
			}, " "),
			nil,
		},
		{
			"Query pool with Label",
			"pool query test_label",
			strings.Join([]string{
				printRequest(t, &control.PoolQueryReq{
					ID:        "test_label",
					QueryMask: daos.DefaultPoolQueryMask,
				}),
			}, " "),
			nil,
		},
		{
			"Query pool with empty ID",
			"pool query \"\"",
			"",
			fmt.Errorf("invalid label"),
		},
		{
			"Upgrade pool with pool ID",
			"pool upgrade 031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
			strings.Join([]string{
				printRequest(t, &control.PoolUpgradeReq{
					ID: "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
				}),
			}, " "),
			nil,
		},
		{
			"Nonexistent subcommand",
			"pool quack",
			"",
			fmt.Errorf("Unknown command"),
		},
	})
}

func TestPoolGetACLToFile_Success(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	tmpDir, tmpCleanup := test.CreateTestDir(t)
	defer tmpCleanup()

	aclFile := filepath.Join(tmpDir, "out.txt")

	err := runCmd(t,
		fmt.Sprintf("pool get-acl 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --outfile %s", aclFile),
		log, control.DefaultMockInvoker(log),
	)

	if err != nil {
		t.Fatalf("Expected no error, got: %+v", err)
	}

	expResult := []string{
		"# Entries:",
		"#   None",
	}

	// Verify the contents of the file
	f, err := os.Open(aclFile)
	if err != nil {
		t.Fatalf("File '%s' not written", aclFile)
	}
	defer f.Close()

	result := make([]string, 0)
	scanner := bufio.NewScanner(f)
	for scanner.Scan() {
		if err := scanner.Err(); err != nil {
			t.Fatalf("Error reading from file: %s", err)
		}
		result = append(result, scanner.Text())
	}

	if diff := cmp.Diff(expResult, result); diff != "" {
		t.Fatalf("Unexpected response (-want, +got):\n%s\n", diff)
	}
}

func TestDmg_PoolListCmd_Errors(t *testing.T) {
	for name, tc := range map[string]struct {
		ctlCfg    *control.Config
		listResp  *mgmtpb.ListPoolsResp
		queryResp *mgmtpb.PoolQueryResp
		msErr     error
		expErr    error
	}{
		"list pools no config": {
			listResp: &mgmtpb.ListPoolsResp{},
			expErr:   errors.New("list pools failed: no configuration loaded"),
		},
		"list pools no queries": {
			ctlCfg:   &control.Config{},
			listResp: &mgmtpb.ListPoolsResp{},
		},
		"list pools ms failures": {
			ctlCfg:   &control.Config{},
			listResp: &mgmtpb.ListPoolsResp{},
			msErr:    errors.New("remote failed"),
			expErr:   errors.New("remote failed"),
		},
		"list pools query success": {
			ctlCfg: &control.Config{},
			listResp: &mgmtpb.ListPoolsResp{
				Pools: []*mgmtpb.ListPoolsResp_Pool{
					{
						Uuid:    test.MockUUID(1),
						SvcReps: []uint32{1, 3, 5, 8},
					},
				},
			},
			queryResp: &mgmtpb.PoolQueryResp{
				Uuid:      test.MockUUID(1),
				TierStats: []*mgmtpb.StorageUsageStats{{}},
			},
		},
		"list pools query failure": {
			ctlCfg: &control.Config{},
			listResp: &mgmtpb.ListPoolsResp{
				Pools: []*mgmtpb.ListPoolsResp_Pool{
					{
						Uuid:    test.MockUUID(1),
						SvcReps: []uint32{1, 3, 5, 8},
						State:   daos.PoolServiceStateReady.String(),
					},
				},
			},
			queryResp: &mgmtpb.PoolQueryResp{
				Status: int32(daos.NotInit),
			},
			expErr: errors.New("Query on pool \"00000001\" unsuccessful, status: \"DER_UNINIT"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			responses := []*control.UnaryResponse{
				control.MockMSResponse("10.0.0.1:10001", tc.msErr, tc.listResp),
			}
			if tc.queryResp != nil {
				responses = append(responses,
					control.MockMSResponse("10.0.0.1:10001", tc.msErr, tc.queryResp))
			}

			mi := control.NewMockInvoker(log, &control.MockInvokerConfig{
				UnaryResponseSet: responses,
			})

			cmd := new(poolListCmd)
			cmd.setInvoker(mi)
			cmd.SetLog(log)
			cmd.setConfig(tc.ctlCfg)

			gotErr := cmd.Execute(nil)
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}
