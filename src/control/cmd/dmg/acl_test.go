//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"errors"
	"fmt"
	"io"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/test"
)

// mockReader is a mock used to represent a successful read of some text
type mockReader struct {
	text string
	read int
}

func (r *mockReader) Read(p []byte) (n int, err error) {
	if r.read >= len(r.text) {
		return 0, io.EOF
	}

	copy(p, r.text[r.read:])

	remaining := len(r.text) - r.read
	var readLen int
	if remaining > len(p) {
		readLen = len(p)
	} else {
		readLen = remaining
	}
	r.read = r.read + readLen
	return readLen, nil
}

// mockErrorReader is a mock used to represent an error reading
type mockErrorReader struct {
	errorMsg string
}

func (r *mockErrorReader) Read(p []byte) (n int, err error) {
	return 1, errors.New(r.errorMsg)
}

func TestReadACLFile_FileOpenFailed(t *testing.T) {
	result, err := readACLFile("/some/fake/path/badfile.txt")

	if result != nil {
		t.Error("Expected no result, got something back")
	}

	if err == nil {
		t.Fatal("Expected an error, got nil")
	}
}

func TestReadACLFile_Success(t *testing.T) {
	dir, cleanup := test.CreateTestDir(t)
	defer cleanup()
	path := test.CreateTestFile(t, dir, "A::OWNER@:rw\nA::user1@:rw\nA:g:group1@:r\n")

	expectedNumACEs := 3

	result, err := readACLFile(path)

	if err != nil {
		t.Errorf("Expected no error, got '%s'", err.Error())
	}

	if result == nil {
		t.Fatal("Expected result, got nil")
	}

	// Just sanity check - parsing is already tested more in-depth below
	if len(result.Entries) != expectedNumACEs {
		t.Errorf("Expected %d items, got %d", expectedNumACEs, len(result.Entries))
	}
}

func TestReadACLFile_Empty(t *testing.T) {
	dir, cleanup := test.CreateTestDir(t)
	defer cleanup()
	path := test.CreateTestFile(t, dir, "")

	result, err := readACLFile(path)

	if result != nil {
		t.Errorf("expected no result, got: %+v", result)
	}

	test.ExpectError(t, err, fmt.Sprintf("ACL file '%s' contains no entries", path), "unexpected error")
}

func TestParseACL_EmptyFile(t *testing.T) {
	mockFile := &mockReader{}

	result, err := parseACL(mockFile)

	if err != nil {
		t.Errorf("Expected no error, got '%s'", err.Error())
	}

	if result == nil {
		t.Error("Expected result, got nil")
		return
	}

	if len(result.Entries) != 0 {
		t.Errorf("Expected empty result, got %d items", len(result.Entries))
	}
}

func TestParseACL_OneValidACE(t *testing.T) {
	expectedACE := "A::OWNER@:rw"
	expectedACL := &common.AccessControlList{Entries: []string{expectedACE}}
	mockFile := &mockReader{
		text: expectedACE + "\n",
	}

	result, err := parseACL(mockFile)

	if err != nil {
		t.Errorf("Expected no error, got '%s'", err.Error())
	}

	if result == nil {
		t.Error("Expected result, got nil")
	}

	if diff := cmp.Diff(result, expectedACL); diff != "" {
		t.Errorf("Unexpected ACL: %v\n", diff)
	}
}

func TestParseACL_WhitespaceExcluded(t *testing.T) {
	expectedACE := "A::OWNER@:rw"
	expectedACL := &common.AccessControlList{Entries: []string{expectedACE}}
	mockFile := &mockReader{
		text: expectedACE + " \n\n",
	}

	result, err := parseACL(mockFile)

	if err != nil {
		t.Errorf("Expected no error, got '%s'", err.Error())
	}

	if result == nil {
		t.Error("Expected result, got nil")
	}

	if diff := cmp.Diff(result, expectedACL); diff != "" {
		t.Errorf("Unexpected ACL: %v\n", diff)
	}
}

func TestParseACL_MultiValidACE(t *testing.T) {
	expectedACEs := []string{
		"A:g:GROUP@:r",
		"A::OWNER@:rw",
		"A:g:readers@:r",
		"L:f:baduser@:rw",
		"U:f:EVERYONE@:rw",
	}
	expectedACL := &common.AccessControlList{
		Entries: expectedACEs,
	}

	mockFile := &mockReader{
		text: strings.Join(expectedACEs, "\n"),
	}

	result, err := parseACL(mockFile)

	if err != nil {
		t.Errorf("Expected no error, got '%s'", err.Error())
	}

	if result == nil {
		t.Error("Expected result, got nil")
	}

	if diff := cmp.Diff(result, expectedACL); diff != "" {
		t.Errorf("Unexpected ACL: %v\n", diff)
	}
}

func TestParseACL_ErrorReadingFile(t *testing.T) {
	expectedError := "mockErrorReader error"
	mockFile := &mockErrorReader{
		errorMsg: expectedError,
	}

	result, err := parseACL(mockFile)

	if result != nil {
		t.Error("Expected nil result, but got a result")
	}

	if err == nil {
		t.Fatal("Expected an error, got nil")
	}

	if !strings.Contains(err.Error(), expectedError) {
		t.Errorf("Wrong error message '%s' (expected to contain '%s')",
			err.Error(), expectedError)
	}
}

func TestParseACL_MultiValidACEWithComment(t *testing.T) {
	expectedACEs := []string{
		"A:g:readers@:r",
		"L:f:baduser@:rw",
		"U:f:EVERYONE@:rw",
	}
	expectedACL := &common.AccessControlList{
		Entries: expectedACEs,
	}

	input := []string{
		"# This is a comment",
	}
	input = append(input, expectedACEs...)
	input = append(input, " #another comment here")

	mockFile := &mockReader{
		text: strings.Join(input, "\n"),
	}

	result, err := parseACL(mockFile)

	if err != nil {
		t.Errorf("Expected no error, got '%s'", err.Error())
	}

	if result == nil {
		t.Error("Expected result, got nil")
	}

	if diff := cmp.Diff(result, expectedACL); diff != "" {
		t.Errorf("Unexpected ACE list: %v\n", diff)
	}
}

func TestFormatACL(t *testing.T) {
	for name, tc := range map[string]struct {
		acl     *common.AccessControlList
		verbose bool
		expStr  string
	}{
		"nil": {
			expStr: "# Entries:\n#   None\n",
		},
		"empty": {
			acl:    &common.AccessControlList{},
			expStr: "# Entries:\n#   None\n",
		},
		"empty verbose": {
			acl:     &common.AccessControlList{},
			expStr:  "# Entries:\n#   None\n",
			verbose: true,
		},
		"single": {
			acl: &common.AccessControlList{
				Entries: []string{
					"A::user@:rw",
				},
			},
			expStr: "# Entries:\nA::user@:rw\n",
		},
		"single verbose": {
			acl: &common.AccessControlList{
				Entries: []string{
					"A::user@:rw",
				},
			},
			expStr:  "# Entries:\n# Allow::user@:Read/Write\nA::user@:rw\n",
			verbose: true,
		},
		"multiple": {
			acl: &common.AccessControlList{
				Entries: []string{
					"A::OWNER@:rw",
					"A:G:GROUP@:rw",
					"A:G:readers@:r",
				},
			},
			expStr: "# Entries:\nA::OWNER@:rw\nA:G:GROUP@:rw\nA:G:readers@:r\n",
		},
		"multiple verbose": {
			acl: &common.AccessControlList{
				Entries: []string{
					"A::OWNER@:rw",
					"A:G:GROUP@:rw",
					"A:G:readers@:r",
				},
			},
			expStr: "# Entries:\n# Allow::Owner:Read/Write\nA::OWNER@:rw\n" +
				"# Allow:Group:Owner-Group:Read/Write\nA:G:GROUP@:rw\n" +
				"# Allow:Group:readers@:Read\nA:G:readers@:r\n",
			verbose: true,
		},
		"with owner user": {
			acl: &common.AccessControlList{
				Entries: []string{
					"A::OWNER@:rw",
				},
				Owner: "bob@",
			},
			expStr: "# Owner: bob@\n# Entries:\nA::OWNER@:rw\n",
		},
		"with owner group": {
			acl: &common.AccessControlList{
				Entries: []string{
					"A:G:GROUP@:rw",
				},
				OwnerGroup: "admins@",
			},
			expStr: "# Owner Group: admins@\n# Entries:\nA:G:GROUP@:rw\n",
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.AssertEqual(t, formatACL(tc.acl, tc.verbose), tc.expStr, "string output didn't match")
		})
	}
}

func TestFormatACLDefault(t *testing.T) {
	acl := &common.AccessControlList{
		Entries: []string{
			"A::OWNER@:rw",
			"A::someuser@:rw",
			"A:G:GROUP@:rw",
			"A:G:writers@:rw",
			"A::EVERYONE@:r",
		},
	}

	// Just need to make sure it doesn't use verbose mode
	expStr := formatACL(acl, false)

	test.AssertEqual(t, formatACLDefault(acl), expStr, "output didn't match non-verbose mode")
}

func TestGetVerboseACE(t *testing.T) {
	for name, tc := range map[string]struct {
		shortACE string
		expStr   string
	}{
		"empty": {
			expStr: "",
		},
		"owner": {
			shortACE: "A::OWNER@:r",
			expStr:   "Allow::Owner:Read",
		},
		"named user": {
			shortACE: "A::friendlyuser@:r",
			expStr:   "Allow::friendlyuser@:Read",
		},
		"owner group": {
			shortACE: "A:G:GROUP@:r",
			expStr:   "Allow:Group:Owner-Group:Read",
		},
		"named group": {
			shortACE: "A:G:mynicegroup@:r",
			expStr:   "Allow:Group:mynicegroup@:Read",
		},
		"everyone": {
			shortACE: "A::EVERYONE@:r",
			expStr:   "Allow::Everyone:Read",
		},
		"no identity": {
			shortACE: "A:::r",
			expStr:   "Allow::None:Read",
		},
		"write perms": {
			shortACE: "A::friendlyuser@:w",
			expStr:   "Allow::friendlyuser@:Write",
		},
		"read-write perms": {
			shortACE: "A::friendlyuser@:rw",
			expStr:   "Allow::friendlyuser@:Read/Write",
		},
		"same order as short perms": {
			shortACE: "A::friendlyuser@:wr",
			expStr:   "Allow::friendlyuser@:Write/Read",
		},
		"no perms": {
			shortACE: "A::friendlyuser@:",
			expStr:   "Allow::friendlyuser@:None",
		},
		"unrecognized perms": {
			shortACE: "A::friendlyuser@:rvwx",
			expStr:   "Allow::friendlyuser@:Read/Unknown/Write/Unknown",
		},
		"unrecognized flags": {
			shortACE: "A:CGI:someone@:rw",
			expStr:   "Allow:Unknown/Group/Unknown:someone@:Read/Write",
		},
		"unrecognized access type": {
			shortACE: "XA:G:someone@:rw",
			expStr:   "Unknown/Allow:Group:someone@:Read/Write",
		},
		"no access type": {
			shortACE: ":G:someone@:rw",
			expStr:   "None:Group:someone@:Read/Write",
		},
		"not an ACE": {
			shortACE: "the quick brown fox jumped over the lazy dog",
			expStr:   "invalid ACE",
		},
		"not all fields": {
			shortACE: "A::friendlyuser@",
			expStr:   "invalid ACE",
		},
		"too many fields": {
			shortACE: "A::friendlyuser@:rw:xyz",
			expStr:   "invalid ACE",
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.AssertEqual(t, getVerboseACE(tc.shortACE), tc.expStr, "incorrect output")
		})
	}
}
