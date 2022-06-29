//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"bufio"
	"context"
	"fmt"
	"os"
	"os/user"
	"path/filepath"
	"reflect"
	"strings"
	"testing"

	"github.com/dustin/go-humanize"
	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/common/test"
	. "github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
)

var (
	defaultPoolUUID = MockUUID()
)

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

	tmpDir, tmpCleanup := CreateTestDir(t)
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

	propWithVal := func(key, val string) *control.PoolProperty {
		hdlr := control.PoolProperties()[key]
		prop := hdlr.GetProperty(key)
		if val != "" {
			if err := prop.SetValue(val); err != nil {
				panic(err)
			}
		}
		return prop
	}

	runCmdTests(t, []cmdTest{
		{
			"Pool create with extra argument",
			fmt.Sprintf("pool create --size %s foo bar", testSizeStr),
			"",
			errors.New("unexpected"),
		},
		{
			"Create pool with label prop and flag",
			fmt.Sprintf("pool create --label foo --size %s --properties label:foo", testSizeStr),
			"",
			errors.New("can't set label property"),
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
					Ranks:      []system.Rank{},
					Properties: []*control.PoolProperty{
						propWithVal("label", "foo"),
					},
				}),
			}, " "),
			nil,
		},
		{
			"Create pool with label flag",
			fmt.Sprintf("pool create --size %s --label foo", testSizeStr),
			strings.Join([]string{
				printRequest(t, &control.PoolCreateReq{
					TotalBytes: uint64(testSize),
					TierRatio:  []float64{0.06, 0.94},
					User:       eUsr.Username + "@",
					UserGroup:  eGrp.Name + "@",
					Ranks:      []system.Rank{},
					Properties: []*control.PoolProperty{
						propWithVal("label", "foo"),
					},
				}),
			}, " "),
			nil,
		},
		{
			"Create pool with label property",
			fmt.Sprintf("pool create --size %s --properties label:foo", testSizeStr),
			strings.Join([]string{
				printRequest(t, &control.PoolCreateReq{
					TotalBytes: uint64(testSize),
					TierRatio:  []float64{0.06, 0.94},
					User:       eUsr.Username + "@",
					UserGroup:  eGrp.Name + "@",
					Ranks:      []system.Rank{},
					Properties: []*control.PoolProperty{
						propWithVal("label", "foo"),
					},
				}),
			}, " "),
			nil,
		},
		{
			"Create pool with missing arguments",
			"pool create",
			"",
			errors.New("must be supplied"),
		},
		{
			"Create pool with incompatible arguments (auto nvme-size)",
			fmt.Sprintf("pool create --size %s --nvme-size %s", testSizeStr, testSizeStr),
			"",
			errors.New("may not be mixed"),
		},
		{
			"Create pool with incompatible arguments (auto scm-size)",
			fmt.Sprintf("pool create --size %s --scm-size %s", testSizeStr, testSizeStr),
			"",
			errors.New("may not be mixed"),
		},
		{
			"Create pool with incompatible arguments (size nranks)",
			"pool create --size 100% --nranks 16",
			"",
			errors.New("--size may not be mixed with --num-ranks"),
		},
		{
			"Create pool with incompatible arguments (size ranks)",
			"pool create --size 100% --ranks 1,2,3",
			"",
			errors.New("--size may not be mixed with --ranks"),
		},
		{
			"Create pool with invalid arguments (too small ratio)",
			"pool create --size=0%",
			"",
			errors.New("Creating DAOS pool with invalid full size ratio"),
		},
		{
			"Create pool with invalid arguments (too big ratio)",
			"pool create --size=101%",
			"",
			errors.New("Creating DAOS pool with invalid full size ratio"),
		},
		{
			"Create pool with incompatible rank arguments (auto)",
			fmt.Sprintf("pool create --size %s --nranks 16 --ranks 1,2,3", testSizeStr),
			"",
			errors.New("may not be mixed"),
		},
		{
			"Create pool with invalid tier-ratio (auto)",
			fmt.Sprintf("pool create --size %s --tier-ratio 200", testSizeStr),
			"",
			errors.New("0-100"),
		},
		{
			"Create pool with single tier-ratio (auto)",
			fmt.Sprintf("pool create --size %s --tier-ratio 10", testSizeStr),
			strings.Join([]string{
				printRequest(t, &control.PoolCreateReq{
					TotalBytes: uint64(testSize),
					TierRatio:  []float64{0.1, 0.9},
					User:       eUsr.Username + "@",
					UserGroup:  eGrp.Name + "@",
					Ranks:      []system.Rank{},
				}),
			}, " "),
			nil,
		},
		{
			"Create pool with incompatible arguments (manual)",
			fmt.Sprintf("pool create --scm-size %s --nranks 42", testSizeStr),
			"",
			errors.New("may not be mixed"),
		},
		{
			"Create pool with minimal arguments",
			fmt.Sprintf("pool create --scm-size %s --nsvc 3", testSizeStr),
			strings.Join([]string{
				printRequest(t, &control.PoolCreateReq{
					NumSvcReps: 3,
					User:       eUsr.Username + "@",
					UserGroup:  eGrp.Name + "@",
					Ranks:      []system.Rank{},
					TierBytes:  []uint64{uint64(testSize), 0},
				}),
			}, " "),
			nil,
		},
		{
			"Create pool with auto storage parameters",
			fmt.Sprintf("pool create --size %s --tier-ratio 2,98 --nranks 8", testSizeStr),
			strings.Join([]string{
				printRequest(t, &control.PoolCreateReq{
					TotalBytes: uint64(testSize),
					TierRatio:  []float64{0.02, 0.98},
					NumRanks:   8,
					User:       eUsr.Username + "@",
					UserGroup:  eGrp.Name + "@",
					Ranks:      []system.Rank{},
				}),
			}, " "),
			nil,
		},
		{
			"Create pool with user and group domains",
			fmt.Sprintf("pool create --scm-size %s --nsvc 3 --user foo@home --group bar@home", testSizeStr),
			strings.Join([]string{
				printRequest(t, &control.PoolCreateReq{
					NumSvcReps: 3,
					User:       "foo@home",
					UserGroup:  "bar@home",
					Ranks:      []system.Rank{},
					TierBytes:  []uint64{uint64(testSize), 0},
				}),
			}, " "),
			nil,
		},
		{
			"Create pool with user but no group",
			fmt.Sprintf("pool create --scm-size %s --nsvc 3 --user foo", testSizeStr),
			strings.Join([]string{
				printRequest(t, &control.PoolCreateReq{
					NumSvcReps: 3,
					User:       "foo@",
					UserGroup:  eGrp.Name + "@",
					Ranks:      []system.Rank{},
					TierBytes:  []uint64{uint64(testSize), 0},
				}),
			}, " "),
			nil,
		},
		{
			"Create pool with group but no user",
			fmt.Sprintf("pool create --scm-size %s --nsvc 3 --group foo", testSizeStr),
			strings.Join([]string{
				printRequest(t, &control.PoolCreateReq{
					NumSvcReps: 3,
					User:       eUsr.Username + "@",
					UserGroup:  "foo@",
					Ranks:      []system.Rank{},
					TierBytes:  []uint64{uint64(testSize), 0},
				}),
			}, " "),
			nil,
		},
		{
			"Create pool with invalid ACL file",
			fmt.Sprintf("pool create --scm-size %s --acl-file /not/a/real/file", testSizeStr),
			"",
			dmgTestErr("opening ACL file: open /not/a/real/file: no such file or directory"),
		},
		{
			"Create pool with empty ACL file",
			fmt.Sprintf("pool create --scm-size %s --acl-file %s", testSizeStr, testEmptyFile),
			"",
			dmgTestErr(fmt.Sprintf("ACL file '%s' contains no entries", testEmptyFile)),
		},
		{
			"Exclude a target with single target idx",
			"pool exclude 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --rank 0 --target-idx 1",
			strings.Join([]string{
				printRequest(t, &control.PoolExcludeReq{
					ID:        "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
					Rank:      0,
					Targetidx: []uint32{1},
				}),
			}, " "),
			nil,
		},
		{
			"Exclude a target with multiple idx",
			"pool exclude 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --rank 0 --target-idx 1,2,3",
			strings.Join([]string{
				printRequest(t, &control.PoolExcludeReq{
					ID:        "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
					Rank:      0,
					Targetidx: []uint32{1, 2, 3},
				}),
			}, " "),
			nil,
		},
		{
			"Exclude a target with no idx given",
			"pool exclude 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --rank 0",
			strings.Join([]string{
				printRequest(t, &control.PoolExcludeReq{
					ID:        "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
					Rank:      0,
					Targetidx: []uint32{},
				}),
			}, " "),
			nil,
		},
		{
			"Drain a target with single target idx",
			"pool drain 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --rank 0 --target-idx 1",
			strings.Join([]string{
				printRequest(t, &control.PoolDrainReq{
					ID:        "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
					Rank:      0,
					Targetidx: []uint32{1},
				}),
			}, " "),
			nil,
		},
		{
			"Drain a target with multiple idx",
			"pool drain 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --rank 0 --target-idx 1,2,3",
			strings.Join([]string{
				printRequest(t, &control.PoolDrainReq{
					ID:        "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
					Rank:      0,
					Targetidx: []uint32{1, 2, 3},
				}),
			}, " "),
			nil,
		},
		{
			"Drain a target with no idx given",
			"pool drain 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --rank 0",
			strings.Join([]string{
				printRequest(t, &control.PoolDrainReq{
					ID:        "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
					Rank:      0,
					Targetidx: []uint32{},
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
					Ranks: []system.Rank{1},
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
					Ranks: []system.Rank{1, 2, 3},
				}),
			}, " "),
			nil,
		},
		{
			"Reintegrate a target with single target idx",
			"pool reintegrate 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --rank 0 --target-idx 1",
			strings.Join([]string{
				printRequest(t, &control.PoolReintegrateReq{
					ID:        "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
					Rank:      0,
					Targetidx: []uint32{1},
				}),
			}, " "),
			nil,
		},
		{
			"Reintegrate a target with multiple idx",
			"pool reintegrate 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --rank 0 --target-idx 1,2,3",
			strings.Join([]string{
				printRequest(t, &control.PoolReintegrateReq{
					ID:        "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
					Rank:      0,
					Targetidx: []uint32{1, 2, 3},
				}),
			}, " "),
			nil,
		},
		{
			"Reintegrate a target with no idx given",
			"pool reintegrate 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --rank 0",
			strings.Join([]string{
				printRequest(t, &control.PoolReintegrateReq{
					ID:        "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
					Rank:      0,
					Targetidx: []uint32{},
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
					Properties: []*control.PoolProperty{
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
					Properties: []*control.PoolProperty{
						propWithVal("label", "foo"),
						propWithVal("space_rb", "42"),
					},
				}),
			}, " "),
			nil,
		},
		{
			"Set pool property with pool flag and deprecated flags",
			"pool set-prop 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --name label --value foo",
			strings.Join([]string{
				printRequest(t, &control.PoolSetPropReq{
					ID:         "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
					Properties: []*control.PoolProperty{propWithVal("label", "foo")},
				}),
			}, " "),
			nil,
		},
		{
			"Set pool property mixed flags/positional",
			"pool set-prop 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --name label --value foo label:foo",
			"",
			errors.New("cannot mix"),
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
			errors.New("must not be empty"),
		},
		{
			"Set pool property bad value",
			"pool set-prop 031bcaf8-f0f5-42ef-b3c5-ee048676dceb reclaim:all",
			"",
			errors.New("invalid value"),
		},
		{
			"Set pool rf property is not allowed",
			"pool set-prop 031bcaf8-f0f5-42ef-b3c5-ee048676dceb rf:1",
			"",
			errors.New("can't set redundancy factor on existing pool."),
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
					Properties: []*control.PoolProperty{
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
			"Query pool with UUID",
			"pool query 12345678-1234-1234-1234-1234567890ab",
			strings.Join([]string{
				printRequest(t, &control.PoolQueryReq{
					ID: "12345678-1234-1234-1234-1234567890ab",
				}),
			}, " "),
			nil,
		},
		{
			"Query pool with UUID and enabled ranks",
			"pool query --show-enabled 12345678-1234-1234-1234-1234567890ab",
			strings.Join([]string{
				printRequest(t, &control.PoolQueryReq{
					ID:                  "12345678-1234-1234-1234-1234567890ab",
					IncludeEnabledRanks: true,
				}),
			}, " "),
			nil,
		},
		{
			"Query pool with UUID and enabled ranks",
			"pool query -e 12345678-1234-1234-1234-1234567890ab",
			strings.Join([]string{
				printRequest(t, &control.PoolQueryReq{
					ID:                  "12345678-1234-1234-1234-1234567890ab",
					IncludeEnabledRanks: true,
				}),
			}, " "),
			nil,
		},
		{
			"Query pool with UUID and disabled ranks",
			"pool query --show-disabled 12345678-1234-1234-1234-1234567890ab",
			strings.Join([]string{
				printRequest(t, &control.PoolQueryReq{
					ID:                   "12345678-1234-1234-1234-1234567890ab",
					IncludeDisabledRanks: true,
				}),
			}, " "),
			nil,
		},
		{
			"Query pool with UUID and disabled ranks",
			"pool query -b 12345678-1234-1234-1234-1234567890ab",
			strings.Join([]string{
				printRequest(t, &control.PoolQueryReq{
					ID:                   "12345678-1234-1234-1234-1234567890ab",
					IncludeDisabledRanks: true,
				}),
			}, " "),
			nil,
		},
		{
			"Query pool with Label",
			"pool query test_label",
			strings.Join([]string{
				printRequest(t, &control.PoolQueryReq{
					ID: "test_label",
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
		{
			"Query pool with incompatible arguments",
			"pool query --show-disabled --show-enabled 12345678-1234-1234-1234-1234567890ab",
			"",
			errors.New("may not be mixed"),
		},
	})
}

func TestPoolGetACLToFile_Success(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer ShowBufferOnFailure(t, buf)

	tmpDir, tmpCleanup := CreateTestDir(t)
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
						State:   system.PoolServiceStateReady.String(),
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

			PoolListCmd := new(PoolListCmd)
			PoolListCmd.setInvoker(mi)
			PoolListCmd.SetLog(log)
			PoolListCmd.setConfig(tc.ctlCfg)

			gotErr := PoolListCmd.Execute(nil)
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}

type MockRequestsRecorderInvoker struct {
	control.MockInvoker
	Requests []control.UnaryRequest
}

func (invoker *MockRequestsRecorderInvoker) InvokeUnaryRPC(context context.Context, request control.UnaryRequest) (*control.UnaryResponse, error) {
	invoker.Requests = append(invoker.Requests, request)
	return invoker.MockInvoker.InvokeUnaryRPC(context, request)
}

func TestDmg_PoolCreateAllCmd(t *testing.T) {
	type ExpectedOutput struct {
		PoolConfig control.MockPoolRespConfig
		WarningMsg string
		Error      error
	}

	for name, tc := range map[string]struct {
		StorageRatio     string
		HostsConfigArray []control.MockHostStorageConfig
		ExpectedOutput   ExpectedOutput
	}{
		"single server": {
			StorageRatio: "100%",
			HostsConfigArray: []control.MockHostStorageConfig{
				{
					HostName: "foo",
					ScmConfig: []control.MockScmConfig{
						{
							MockStorageConfig: control.MockStorageConfig{
								TotalBytes: uint64(100) * uint64(humanize.GByte),
								AvailBytes: uint64(100) * uint64(humanize.GByte),
							},
						},
					},
					NvmeConfig: []control.MockNvmeConfig{
						{
							MockStorageConfig: control.MockStorageConfig{
								TotalBytes: uint64(1) * uint64(humanize.TByte),
								AvailBytes: uint64(1) * uint64(humanize.TByte),
							},
							Rank: 0,
						},
					},
				},
			},
			ExpectedOutput: ExpectedOutput{
				PoolConfig: control.MockPoolRespConfig{
					HostName:  "foo",
					Ranks:     "0",
					ScmBytes:  uint64(100) * uint64(humanize.GByte),
					NvmeBytes: uint64(1) * uint64(humanize.TByte),
				},
			},
		},
		"single server 30%": {
			StorageRatio: "30%",
			HostsConfigArray: []control.MockHostStorageConfig{
				{
					HostName: "foo",
					ScmConfig: []control.MockScmConfig{
						{
							MockStorageConfig: control.MockStorageConfig{
								TotalBytes: uint64(100) * uint64(humanize.GByte),
								AvailBytes: uint64(100) * uint64(humanize.GByte),
							},
						},
					},
					NvmeConfig: []control.MockNvmeConfig{
						{
							MockStorageConfig: control.MockStorageConfig{
								TotalBytes: uint64(1) * uint64(humanize.TByte),
								AvailBytes: uint64(1) * uint64(humanize.TByte),
							},
							Rank: 0,
						},
					},
				},
			},
			ExpectedOutput: ExpectedOutput{
				PoolConfig: control.MockPoolRespConfig{
					HostName:  "foo",
					Ranks:     "0",
					ScmBytes:  uint64(30) * uint64(humanize.GByte),
					NvmeBytes: uint64(300) * uint64(humanize.GByte),
				},
			},
		},
		"double server": {
			StorageRatio: "100%",
			HostsConfigArray: []control.MockHostStorageConfig{
				{
					HostName: "foo",
					ScmConfig: []control.MockScmConfig{
						{
							MockStorageConfig: control.MockStorageConfig{
								TotalBytes: uint64(100) * uint64(humanize.GByte),
								AvailBytes: uint64(100) * uint64(humanize.GByte),
							},
						},
					},
					NvmeConfig: []control.MockNvmeConfig{
						{
							MockStorageConfig: control.MockStorageConfig{
								TotalBytes: uint64(1) * uint64(humanize.TByte),
								AvailBytes: uint64(1) * uint64(humanize.TByte),
							},
							Rank: 0,
						},
					},
				},
				{
					HostName: "bar",
					ScmConfig: []control.MockScmConfig{
						{
							MockStorageConfig: control.MockStorageConfig{
								TotalBytes: uint64(1) * uint64(humanize.TByte),
								AvailBytes: uint64(1) * uint64(humanize.TByte),
							},
						},
						{ // Check if not mounted SCM is well managed
							MockStorageConfig: control.MockStorageConfig{
								TotalBytes: uint64(0),
								AvailBytes: uint64(0),
							},
						},
						{
							MockStorageConfig: control.MockStorageConfig{
								TotalBytes: uint64(100) * uint64(humanize.GByte),
								AvailBytes: uint64(100) * uint64(humanize.GByte),
							},
						},
						{
							MockStorageConfig: control.MockStorageConfig{
								TotalBytes: uint64(1) * uint64(humanize.TByte),
								AvailBytes: uint64(50) * uint64(humanize.GByte),
							},
						},
					},
					NvmeConfig: []control.MockNvmeConfig{
						{
							MockStorageConfig: control.MockStorageConfig{
								TotalBytes: uint64(1) * uint64(humanize.TByte),
								AvailBytes: uint64(1) * uint64(humanize.TByte),
							},
							Rank: 1,
						},
						{
							MockStorageConfig: control.MockStorageConfig{
								TotalBytes: uint64(1) * uint64(humanize.TByte),
								AvailBytes: uint64(400) * uint64(humanize.GByte),
							},
							Rank: 2,
						},
						{
							MockStorageConfig: control.MockStorageConfig{
								TotalBytes: uint64(1) * uint64(humanize.TByte),
								AvailBytes: uint64(300) * uint64(humanize.GByte),
							},
							Rank: 2,
						},
						{
							MockStorageConfig: control.MockStorageConfig{
								TotalBytes: uint64(3) * uint64(humanize.TByte),
								AvailBytes: uint64(2) * uint64(humanize.TByte),
							},
							Rank: 3,
						},
					},
				},
			},
			ExpectedOutput: ExpectedOutput{
				PoolConfig: control.MockPoolRespConfig{
					HostName:  "foo",
					Ranks:     "0,1,2,3",
					ScmBytes:  uint64(50) * uint64(humanize.GByte),
					NvmeBytes: uint64(700) * uint64(humanize.GByte),
				},
			},
		},
		"No NVME": {
			StorageRatio: "100%",
			HostsConfigArray: []control.MockHostStorageConfig{
				{
					HostName: "foo",
					ScmConfig: []control.MockScmConfig{
						{
							MockStorageConfig: control.MockStorageConfig{
								TotalBytes: uint64(100) * uint64(humanize.GByte),
								AvailBytes: uint64(100) * uint64(humanize.GByte),
							},
						},
					},
					NvmeConfig: []control.MockNvmeConfig{},
				},
			},
			ExpectedOutput: ExpectedOutput{
				PoolConfig: control.MockPoolRespConfig{
					HostName:  "foo",
					Ranks:     "0",
					ScmBytes:  uint64(100) * uint64(humanize.GByte),
					NvmeBytes: uint64(0),
				},
				WarningMsg: "Creating DAOS pool without NVME storage",
			},
		},
		"SCM:NVME ratio": {
			StorageRatio: "  100   %  ",
			HostsConfigArray: []control.MockHostStorageConfig{
				{
					HostName: "foo",
					ScmConfig: []control.MockScmConfig{
						{
							MockStorageConfig: control.MockStorageConfig{
								TotalBytes: uint64(100) * uint64(humanize.GByte),
								AvailBytes: uint64(100) * uint64(humanize.GByte),
							},
						},
					},
					NvmeConfig: []control.MockNvmeConfig{
						{
							MockStorageConfig: control.MockStorageConfig{
								TotalBytes: uint64(100) * uint64(humanize.TByte),
								AvailBytes: uint64(100) * uint64(humanize.TByte),
							},
							Rank: 0,
						},
					},
				},
			},
			ExpectedOutput: ExpectedOutput{
				PoolConfig: control.MockPoolRespConfig{
					HostName:  "foo",
					Ranks:     "0",
					ScmBytes:  uint64(100) * uint64(humanize.GByte),
					NvmeBytes: uint64(100) * uint64(humanize.TByte),
				},
				WarningMsg: "SCM:NVMe ratio is less than",
			},
		},
		"single server error 1%": {
			StorageRatio: "1%",
			HostsConfigArray: []control.MockHostStorageConfig{
				{
					HostName: "foo",
					ScmConfig: []control.MockScmConfig{
						{
							MockStorageConfig: control.MockStorageConfig{
								TotalBytes: uint64(100) * uint64(humanize.GByte),
								AvailBytes: uint64(1),
							},
						},
					},
					NvmeConfig: []control.MockNvmeConfig{
						{
							MockStorageConfig: control.MockStorageConfig{
								TotalBytes: uint64(1) * uint64(humanize.TByte),
								AvailBytes: uint64(1) * uint64(humanize.TByte),
							},
							Rank: 0,
						},
					},
				},
			},
			ExpectedOutput: ExpectedOutput{
				Error: errors.New("Not enough SCM storage available with ratio 1%"),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mockInvokerConfig := new(control.MockInvokerConfig)

			unaryResponse := new(control.UnaryResponse)
			for _, hostStorageConfig := range tc.HostsConfigArray {
				storageScanResp := control.MockStorageScanResp(t,
					hostStorageConfig.ScmConfig,
					hostStorageConfig.NvmeConfig)
				hostResponse := &control.HostResponse{
					Addr:    hostStorageConfig.HostName,
					Message: storageScanResp,
				}
				unaryResponse.Responses = append(unaryResponse.Responses, hostResponse)
			}
			mockInvokerConfig.UnaryResponseSet = append(mockInvokerConfig.UnaryResponseSet, unaryResponse)

			if tc.ExpectedOutput.PoolConfig.Ranks != "" {
				poolCreateResp := control.MockPoolCreateResp(t, &tc.ExpectedOutput.PoolConfig)
				hostResponse := &control.HostResponse{
					Addr:    tc.ExpectedOutput.PoolConfig.HostName,
					Message: poolCreateResp,
				}
				unaryResponse = new(control.UnaryResponse)
				unaryResponse.Responses = append(unaryResponse.Responses, hostResponse)
				mockInvokerConfig.UnaryResponseSet = append(mockInvokerConfig.UnaryResponseSet, unaryResponse)
			}

			mockInvoker := &MockRequestsRecorderInvoker{
				MockInvoker: *control.NewMockInvoker(log, mockInvokerConfig),
				Requests:    []control.UnaryRequest{},
			}

			poolCreateCmd := new(PoolCreateCmd)
			poolCreateCmd.setInvoker(mockInvoker)
			poolCreateCmd.SetLog(log)
			poolCreateCmd.Size = tc.StorageRatio

			err := poolCreateCmd.Execute(nil)

			if tc.ExpectedOutput.Error != nil {
				test.AssertTrue(t, err != nil, "Expected an error")
				testExpectedError(t, tc.ExpectedOutput.Error, err)
			} else {
				test.AssertTrue(t, err == nil, "Expected no error")
				test.AssertEqual(t, len(mockInvoker.Requests), 2, "Invalid number of request sent")
				test.AssertTrue(t,
					reflect.TypeOf(mockInvoker.Requests[0]) == reflect.TypeOf(&control.StorageScanReq{}),
					"Invalid request type: wanted="+reflect.TypeOf(&control.StorageScanReq{}).String()+
						" got="+reflect.TypeOf(mockInvoker.Requests[0]).String())
				test.AssertTrue(t,
					reflect.TypeOf(mockInvoker.Requests[1]) == reflect.TypeOf(&control.PoolCreateReq{}),
					"Invalid request type: wanted="+reflect.TypeOf(&control.PoolCreateReq{}).String()+
						" got="+reflect.TypeOf(mockInvoker.Requests[1]).String())
				poolCreateRequest := mockInvoker.Requests[1].(*control.PoolCreateReq)
				test.AssertEqual(t,
					poolCreateRequest.TierBytes[0],
					tc.ExpectedOutput.PoolConfig.ScmBytes,
					"Invalid size of allocated SCM")
				test.AssertEqual(t,
					poolCreateRequest.TierBytes[1],
					tc.ExpectedOutput.PoolConfig.NvmeBytes,
					"Invalid size of allocated NVME")
				test.AssertEqual(t,
					poolCreateRequest.TotalBytes,
					uint64(0),
					"Invalid size of TotalBytes attribute: disabled with manual allocation")
				test.AssertTrue(t,
					poolCreateRequest.TierRatio == nil,
					"Invalid size of TierRatio attribute: disabled with manual allocation")
				msg := fmt.Sprintf("Creating DAOS pool with %s of all storage",
					strings.ReplaceAll(tc.StorageRatio, " ", ""))
				test.AssertTrue(t,
					strings.Contains(buf.String(), msg),
					"missing success message: Creating DAOS pool with with <ratio>% of all storage")
				if tc.ExpectedOutput.WarningMsg != "" {
					test.AssertTrue(t,
						strings.Contains(buf.String(), tc.ExpectedOutput.WarningMsg),
						"missing warning message: "+tc.ExpectedOutput.WarningMsg)
				}
			}
		})
	}
}
