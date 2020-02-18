//
// (C) Copyright 2019-2020 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package main

import (
	"bufio"
	"fmt"
	"os"
	"os/user"
	"path/filepath"
	"strconv"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/client"
	. "github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
)

func createACLFile(t *testing.T, path string, acl *client.AccessControlList) {
	t.Helper()

	file, err := os.Create(path)
	if err != nil {
		t.Fatalf("Couldn't create ACL file: %v", err)
	}
	defer file.Close()

	_, err = file.WriteString(formatACLDefault(acl))
	if err != nil {
		t.Fatalf("Couldn't write to file: %v", err)
	}
}

func TestPoolCommands(t *testing.T) {
	testScmSizeStr := "512GiB"
	testScmSize := 549755813888
	testNvmeSizeStr := "512GB"
	testNvmeSize := 512000000000
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
	testACLFile := filepath.Join(tmpDir, "test_acl.txt")
	testACL := &client.AccessControlList{
		Entries: []string{"A::OWNER@:rw", "A:G:GROUP@:rw"},
	}
	createACLFile(t, testACLFile, testACL)

	// An existing file with contents for tests that need to verify overwrite
	testExistingFile := filepath.Join(tmpDir, "existing.txt")
	createACLFile(t, testExistingFile, testACL)

	// An existing file with write-only perms
	testWriteOnlyFile := filepath.Join(tmpDir, "write.txt")
	createACLFile(t, testWriteOnlyFile, testACL)
	err = os.Chmod(testWriteOnlyFile, 0222)
	if err != nil {
		t.Fatalf("Couldn't set file writable only")
	}

	testEmptyFile := filepath.Join(tmpDir, "empty.txt")
	empty, err := os.Create(testEmptyFile)
	if err != nil {
		t.Fatalf("Failed to create empty file: %s", err)
	}
	empty.Close()

	// Subdirectory with no write perms
	testNoPermDir := filepath.Join(tmpDir, "badpermsdir")
	os.Mkdir(testNoPermDir, 0444)

	runCmdTests(t, []cmdTest{
		{
			"Create pool with missing arguments",
			"pool create",
			"",
			errMissingFlag,
		},
		{
			"Create pool with minimal arguments",
			fmt.Sprintf("pool create --scm-size %s --nsvc 3", testScmSizeStr),
			strings.Join([]string{
				"ConnectClients",
				fmt.Sprintf("PoolCreate-%+v", &client.PoolCreateReq{
					ScmBytes:   uint64(testScmSize),
					NumSvcReps: 3,
					Sys:        "daos_server", // FIXME: This should be a constant
					Usr:        eUsr.Username + "@",
					Grp:        eGrp.Name + "@",
				}),
			}, " "),
			nil,
		},
		{
			"Create pool with all arguments",
			fmt.Sprintf("pool create --scm-size %s --nsvc 3 --user foo --group bar --nvme-size %s --sys fnord --acl-file %s",
				testScmSizeStr, testNvmeSizeStr, testACLFile),
			strings.Join([]string{
				"ConnectClients",
				fmt.Sprintf("PoolCreate-%+v", &client.PoolCreateReq{
					ScmBytes:   uint64(testScmSize),
					NvmeBytes:  uint64(testNvmeSize),
					NumSvcReps: 3,
					Sys:        "fnord",
					Usr:        "foo@",
					Grp:        "bar@",
					ACL:        testACL,
				}),
			}, " "),
			nil,
		},
		{
			"Create pool with raw byte count size args",
			fmt.Sprintf("pool create --scm-size %s --nsvc 3 --user foo --group bar --nvme-size %s --sys fnord --acl-file %s",
				strconv.Itoa(testScmSize), strconv.Itoa(testNvmeSize), testACLFile),
			strings.Join([]string{
				"ConnectClients",
				fmt.Sprintf("PoolCreate-%+v", &client.PoolCreateReq{
					ScmBytes:   uint64(testScmSize),
					NvmeBytes:  uint64(testNvmeSize),
					NumSvcReps: 3,
					Sys:        "fnord",
					Usr:        "foo@",
					Grp:        "bar@",
					ACL:        testACL,
				}),
			}, " "),
			nil,
		},
		{
			"Create pool with user and group domains",
			fmt.Sprintf("pool create --scm-size %s --nsvc 3 --user foo@home --group bar@home", testScmSizeStr),
			strings.Join([]string{
				"ConnectClients",
				fmt.Sprintf("PoolCreate-%+v", &client.PoolCreateReq{
					ScmBytes:   uint64(testScmSize),
					NumSvcReps: 3,
					Sys:        "daos_server",
					Usr:        "foo@home",
					Grp:        "bar@home",
				}),
			}, " "),
			nil,
		},
		{
			"Create pool with user but no group",
			fmt.Sprintf("pool create --scm-size %s --nsvc 3 --user foo", testScmSizeStr),
			strings.Join([]string{
				"ConnectClients",
				fmt.Sprintf("PoolCreate-%+v", &client.PoolCreateReq{
					ScmBytes:   uint64(testScmSize),
					NumSvcReps: 3,
					Sys:        "daos_server",
					Usr:        "foo@",
				}),
			}, " "),
			nil,
		},
		{
			"Create pool with group but no user",
			fmt.Sprintf("pool create --scm-size %s --nsvc 3 --group foo", testScmSizeStr),
			strings.Join([]string{
				"ConnectClients",
				fmt.Sprintf("PoolCreate-%+v", &client.PoolCreateReq{
					ScmBytes:   uint64(testScmSize),
					NumSvcReps: 3,
					Sys:        "daos_server",
					Grp:        "foo@",
				}),
			}, " "),
			nil,
		},
		{
			"Create pool with invalid ACL file",
			fmt.Sprintf("pool create --scm-size %s --acl-file /not/a/real/file", testScmSizeStr),
			"ConnectClients",
			dmgTestErr("opening ACL file: open /not/a/real/file: no such file or directory"),
		},
		{
			"Create pool with empty ACL file",
			fmt.Sprintf("pool create --scm-size %s --acl-file %s", testScmSizeStr, testEmptyFile),
			"ConnectClients",
			dmgTestErr(fmt.Sprintf("ACL file '%s' contains no entries", testEmptyFile)),
		},
		{
			"Destroy pool with force",
			"pool destroy --pool 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --force",
			strings.Join([]string{
				"ConnectClients",
				fmt.Sprintf("PoolDestroy-%+v", &client.PoolDestroyReq{
					UUID:  "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
					Force: true,
				}),
			}, " "),
			nil,
		},
		{
			"Set string pool property",
			"pool set-prop --pool 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --name reclaim --value lazy",
			strings.Join([]string{
				"ConnectClients",
				fmt.Sprintf("PoolSetProp-%+v", client.PoolSetPropReq{
					UUID:     "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
					Property: "reclaim",
					Value:    "lazy",
				}),
			}, " "),
			nil,
		},
		{
			"Set numeric pool property",
			"pool set-prop --pool 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --name answer --value 42",
			strings.Join([]string{
				"ConnectClients",
				fmt.Sprintf("PoolSetProp-%+v", client.PoolSetPropReq{
					UUID:     "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
					Property: "answer",
					Value:    42,
				}),
			}, " "),
			nil,
		},
		{
			"Set pool property missing value",
			"pool set-prop --pool 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --name whoops",
			"",
			errors.New("required flag"),
		},
		{
			"Get pool ACL",
			"pool get-acl --pool 031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
			strings.Join([]string{
				"ConnectClients",
				fmt.Sprintf("PoolGetACL-%+v", client.PoolGetACLReq{
					UUID: "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
				}),
			}, " "),
			nil,
		},
		{
			"Get pool ACL with verbose flag",
			"pool get-acl --pool 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --verbose",
			strings.Join([]string{
				"ConnectClients",
				fmt.Sprintf("PoolGetACL-%+v", client.PoolGetACLReq{
					UUID: "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
				}),
			}, " "),
			nil,
		},
		{
			"Get pool ACL with output to bad file",
			"pool get-acl --pool 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --outfile /foo/bar/acl.txt",
			strings.Join([]string{
				"ConnectClients",
				fmt.Sprintf("PoolGetACL-%+v", client.PoolGetACLReq{
					UUID: "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
				}),
			}, " "),
			errors.New("open /foo/bar/acl.txt: no such file or directory"),
		},
		{
			"Get pool ACL with output to existing file",
			fmt.Sprintf("pool get-acl --pool 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --outfile %s", testExistingFile),
			strings.Join([]string{
				"ConnectClients",
				fmt.Sprintf("PoolGetACL-%+v", client.PoolGetACLReq{
					UUID: "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
				}),
			}, " "),
			errors.New(fmt.Sprintf("file already exists: %s", testExistingFile)),
		},
		{
			"Get pool ACL with output to existing file with write-only perms",
			fmt.Sprintf("pool get-acl --pool 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --outfile %s", testWriteOnlyFile),
			strings.Join([]string{
				"ConnectClients",
				fmt.Sprintf("PoolGetACL-%+v", client.PoolGetACLReq{
					UUID: "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
				}),
			}, " "),
			errors.New(fmt.Sprintf("file already exists: %s", testWriteOnlyFile)),
		},
		{
			"Get pool ACL with output to existing file with force",
			fmt.Sprintf("pool get-acl --pool 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --outfile %s --force", testExistingFile),
			strings.Join([]string{
				"ConnectClients",
				fmt.Sprintf("PoolGetACL-%+v", client.PoolGetACLReq{
					UUID: "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
				}),
			}, " "),
			nil,
		},
		{
			"Get pool ACL with output to directory with no write perms",
			fmt.Sprintf("pool get-acl --pool 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --outfile %s", filepath.Join(testNoPermDir, "out.txt")),
			strings.Join([]string{
				"ConnectClients",
				fmt.Sprintf("PoolGetACL-%+v", client.PoolGetACLReq{
					UUID: "031bcaf8-f0f5-42ef-b3c5-ee048676dceb",
				}),
			}, " "),
			errors.New(fmt.Sprintf("open %s: permission denied", filepath.Join(testNoPermDir, "out.txt"))),
		},
		{
			"Overwrite pool ACL with invalid ACL file",
			"pool overwrite-acl --pool 12345678-1234-1234-1234-1234567890ab --acl-file /not/a/real/file",
			"ConnectClients",
			dmgTestErr("opening ACL file: open /not/a/real/file: no such file or directory"),
		},
		{
			"Overwrite pool ACL with empty ACL file",
			fmt.Sprintf("pool overwrite-acl --pool 12345678-1234-1234-1234-1234567890ab --acl-file %s", testEmptyFile),
			"ConnectClients",
			dmgTestErr(fmt.Sprintf("ACL file '%s' contains no entries", testEmptyFile)),
		},
		{
			"Overwrite pool ACL",
			fmt.Sprintf("pool overwrite-acl --pool 12345678-1234-1234-1234-1234567890ab --acl-file %s", testACLFile),
			strings.Join([]string{
				"ConnectClients",
				fmt.Sprintf("PoolOverwriteACL-%+v", client.PoolOverwriteACLReq{
					UUID: "12345678-1234-1234-1234-1234567890ab",
					ACL:  testACL,
				}),
			}, " "),
			nil,
		},
		{
			"Update pool ACL with invalid ACL file",
			"pool update-acl --pool 12345678-1234-1234-1234-1234567890ab --acl-file /not/a/real/file",
			"ConnectClients",
			dmgTestErr("opening ACL file: open /not/a/real/file: no such file or directory"),
		},
		{
			"Update pool ACL with empty ACL file",
			fmt.Sprintf("pool update-acl --pool 12345678-1234-1234-1234-1234567890ab --acl-file %s", testEmptyFile),
			"ConnectClients",
			dmgTestErr(fmt.Sprintf("ACL file '%s' contains no entries", testEmptyFile)),
		},
		{
			"Update pool ACL without file or entry",
			"pool update-acl --pool 12345678-1234-1234-1234-1234567890ab",
			"ConnectClients",
			dmgTestErr("either ACL file or entry parameter is required"),
		},
		{
			"Update pool ACL with both file and entry",
			fmt.Sprintf("pool update-acl --pool 12345678-1234-1234-1234-1234567890ab --acl-file %s --entry A::user@:rw", testACLFile),
			"ConnectClients",
			dmgTestErr("either ACL file or entry parameter is required"),
		},
		{
			"Update pool ACL with ACL file",
			fmt.Sprintf("pool update-acl --pool 12345678-1234-1234-1234-1234567890ab --acl-file %s", testACLFile),
			strings.Join([]string{
				"ConnectClients",
				fmt.Sprintf("PoolUpdateACL-%+v", client.PoolUpdateACLReq{
					UUID: "12345678-1234-1234-1234-1234567890ab",
					ACL:  testACL,
				}),
			}, " "),
			nil,
		},
		{
			"Update pool ACL with entry",
			"pool update-acl --pool 12345678-1234-1234-1234-1234567890ab --entry A::user@:rw",
			strings.Join([]string{
				"ConnectClients",
				fmt.Sprintf("PoolUpdateACL-%+v", client.PoolUpdateACLReq{
					UUID: "12345678-1234-1234-1234-1234567890ab",
					ACL:  &client.AccessControlList{Entries: []string{"A::user@:rw"}},
				}),
			}, " "),
			nil,
		},
		{
			"Delete pool ACL without principal flag",
			"pool delete-acl --pool 12345678-1234-1234-1234-1234567890ab",
			"ConnectClients",
			dmgTestErr("the required flag `-p, --principal' was not specified"),
		},
		{
			"Delete pool ACL",
			"pool delete-acl --pool 12345678-1234-1234-1234-1234567890ab --principal OWNER@",
			strings.Join([]string{
				"ConnectClients",
				fmt.Sprintf("PoolDeleteACL-%+v", client.PoolDeleteACLReq{
					UUID:      "12345678-1234-1234-1234-1234567890ab",
					Principal: "OWNER@",
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
	defer ShowBufferOnFailure(t, buf)

	tmpDir, tmpCleanup := CreateTestDir(t)
	defer tmpCleanup()

	aclFile := filepath.Join(tmpDir, "out.txt")

	conn := newTestConn(t)
	err := runCmd(t, fmt.Sprintf("pool get-acl --pool 12345678-1234-1234-123456789abc --outfile %s", aclFile), log, conn)

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
