//
// (C) Copyright 2019-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package security

import (
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/pkg/errors"
)

func TestLoadPEMData(t *testing.T) {

	goodCertPath := "testdata/certs/daosCA.crt"
	betterCertPermPath := "testdata/certs/server.crt"
	badCertPerm := "testdata/certs/badperms.crt"
	badCertPermError := &badPermsError{badCertPerm, 0666, MaxCertPerm}

	testCases := []struct {
		filename string
		perms    os.FileMode
		testname string
		expected string
	}{
		{goodCertPath, MaxCertPerm, "GoodCert", ""},
		{betterCertPermPath, MaxCertPerm, "BetterCertPerms", ""},
		{badCertPerm, MaxCertPerm, "ImproperPermissions", badCertPermError.Error()},
	}

	// Setup permissions for tests below.
	if err := os.Chmod(goodCertPath, MaxCertPerm); err != nil {
		t.Fatal(err)
	}
	if err := os.Chmod(betterCertPermPath, 0444); err != nil {
		t.Fatal(err)
	}
	if err := os.Chmod(badCertPerm, 0666); err != nil {
		t.Fatal(err)
	}

	for _, tc := range testCases {
		t.Run(tc.testname, func(t *testing.T) {
			result := ""
			_, err := LoadPEMData(tc.filename, tc.perms)
			if err != nil {
				result = err.Error()
			}
			if strings.Compare(result, tc.expected) != 0 {
				t.Errorf("result %s; expected %s", result, tc.expected)
			}
		})
	}
}

func TestLoadPrivateKey(t *testing.T) {
	goodKeyPath := "testdata/certs/daosCA.key"
	badKeyPerm := "testdata/certs/badperms.key"
	malformed := "testdata/certs/bad.key"
	toomany := "testdata/certs/toomanypem.key"
	notkey := "testdata/certs/notkey.crt"
	badKeyPermError := &badPermsError{badKeyPerm, MaxCertPerm, MaxUserOnlyKeyPerm}
	malformedError := fmt.Sprintf("%s does not contain PEM data", malformed)
	toomanyError := "Only one key allowed per file"
	notkeyError := "PEM Block is not a Private Key"

	// Setup permissions for tests below.
	if err := os.Chmod(goodKeyPath, MaxUserOnlyKeyPerm); err != nil {
		t.Fatal(err)
	}
	// Intentionallly safe cert perm as it should be incorrect
	if err := os.Chmod(badKeyPerm, MaxCertPerm); err != nil {
		t.Fatal(err)
	}
	if err := os.Chmod(malformed, MaxUserOnlyKeyPerm); err != nil {
		t.Fatal(err)
	}
	if err := os.Chmod(toomany, MaxUserOnlyKeyPerm); err != nil {
		t.Fatal(err)
	}
	if err := os.Chmod(notkey, MaxUserOnlyKeyPerm); err != nil {
		t.Fatal(err)
	}

	testCases := []struct {
		filename string
		testname string
		expected string
	}{
		{goodKeyPath, "GoodKey", ""},
		{badKeyPerm, "ImproperPermissions", badKeyPermError.Error()},
		{malformed, "Malformed Key", malformedError},
		{toomany, "InvalidPEMCount", toomanyError},
		{notkey, "NotAKey", notkeyError},
	}

	for _, tc := range testCases {
		t.Run(tc.testname, func(t *testing.T) {
			result := ""
			_, err := LoadPrivateKey(tc.filename)
			if err != nil {
				result = err.Error()
			}
			if strings.Compare(result, tc.expected) != 0 {
				t.Errorf("result %s; expected %s", result, tc.expected)
			}
		})
	}
}
func TestLoadCertificate(t *testing.T) {
	goodPath := "testdata/certs/daosCA.crt"
	badPerm := "testdata/certs/badperms.crt"
	malformed := "testdata/certs/bad.crt"
	toomany := "testdata/certs/toomanypem.crt"
	badError := &badPermsError{badPerm, 0700, MaxCertPerm}
	malformedError := fmt.Sprintf("%s does not contain PEM data", malformed)
	toomanyError := "Only one cert allowed per file"

	// Setup permissions for tests below.
	if err := os.Chmod(goodPath, MaxCertPerm); err != nil {
		t.Fatal(err)
	}
	// Intentionally safe cert perm as it should be incorrect
	if err := os.Chmod(badPerm, 0700); err != nil {
		t.Fatal(err)
	}
	if err := os.Chmod(malformed, MaxCertPerm); err != nil {
		t.Fatal(err)
	}
	if err := os.Chmod(toomany, MaxCertPerm); err != nil {
		t.Fatal(err)
	}

	testCases := []struct {
		filename string
		testname string
		expected string
	}{
		{goodPath, "GoodKey", ""},
		{badPerm, "ImproperPermissions", badError.Error()},
		{malformed, "Malformed Key", malformedError},
		{toomany, "InvalidPEMCount", toomanyError},
	}

	for _, tc := range testCases {
		t.Run(tc.testname, func(t *testing.T) {
			result := ""
			_, err := LoadCertificate(tc.filename)
			if err != nil {
				result = err.Error()
			}
			if strings.Compare(result, tc.expected) != 0 {
				t.Errorf("result %s; expected %s", result, tc.expected)
			}
		})
	}
}
func TestSecurity_Pem_ValidateCertDirectory(t *testing.T) {
	for name, tc := range map[string]struct {
		perms  fs.FileMode
		notDir bool
		expErr error
	}{
		"max acceptable perms": {
			perms: MaxDirPerm,
		},
		"perms too open": {
			perms:  0777,
			expErr: errors.New("insecure permissions"),
		},
		"not a directory": {
			perms:  MaxDirPerm,
			notDir: true,
			expErr: errors.New("not a directory"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			dir, cleanupDir := test.CreateTestDir(t)
			defer cleanupDir()

			testDir := filepath.Join(dir, "test_dir")
			if err := os.Mkdir(testDir, tc.perms); err != nil {
				t.Fatal(err)
			}
			// Use chmod to set the permissions regardless of umask.
			if err := os.Chmod(testDir, tc.perms); err != nil {
				t.Fatal(err)
			}
			testFile := test.CreateTestFile(t, dir, "some content")

			path := testDir
			if tc.notDir {
				path = testFile
			}

			err := ValidateCertDirectory(path)

			test.CmpErr(t, tc.expErr, err)
		})
	}
}
