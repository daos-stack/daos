//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package security

import (
	"fmt"
	"os"
	"strings"
	"testing"
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
	badKeyPermError := &badPermsError{badKeyPerm, MaxCertPerm, MaxKeyPerm}
	malformedError := fmt.Sprintf("%s does not contain PEM data", malformed)
	toomanyError := "Only one key allowed per file"
	notkeyError := "PEM Block is not a Private Key"

	// Setup permissions for tests below.
	if err := os.Chmod(goodKeyPath, MaxKeyPerm); err != nil {
		t.Fatal(err)
	}
	// Intentionallly safe cert perm as it should be incorrect
	if err := os.Chmod(badKeyPerm, MaxCertPerm); err != nil {
		t.Fatal(err)
	}
	if err := os.Chmod(malformed, MaxKeyPerm); err != nil {
		t.Fatal(err)
	}
	if err := os.Chmod(toomany, MaxKeyPerm); err != nil {
		t.Fatal(err)
	}
	if err := os.Chmod(notkey, MaxKeyPerm); err != nil {
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
	badError := &badPermsError{badPerm, MaxKeyPerm, MaxCertPerm}
	malformedError := fmt.Sprintf("%s does not contain PEM data", malformed)
	toomanyError := "Only one cert allowed per file"

	// Setup permissions for tests below.
	if err := os.Chmod(goodPath, MaxCertPerm); err != nil {
		t.Fatal(err)
	}
	// Intentionally safe cert perm as it should be incorrect
	if err := os.Chmod(badPerm, MaxKeyPerm); err != nil {
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
func TestValidateCertDirectory(t *testing.T) {
	goodDirPath := "testdata/certs/goodperms"
	badDirPerm := "testdata/certs/badperms"
	notDir := "testdata/certs/daosCA.crt"
	badDirPermError := &badPermsError{badDirPerm, 0777, MaxDirPerm}
	notDirError := fmt.Sprintf("Certificate directory path (%s) is not a directory", notDir)
	testCases := []struct {
		pathname string
		testname string
		expected string
	}{
		{goodDirPath, "GoodPermissions", ""},
		{badDirPerm, "ImproperPermissions", badDirPermError.Error()},
		{notDir, "NotADirectory", notDirError},
	}

	if err := os.Chmod(goodDirPath, MaxDirPerm); err != nil {
		t.Fatal(err)
	}
	// Intentionall safe cert perm as to be incorrect
	if err := os.Chmod(badDirPerm, 0777); err != nil {
		t.Fatal(err)
	}

	for _, tc := range testCases {
		t.Run(tc.testname, func(t *testing.T) {
			result := ""
			err := ValidateCertDirectory(tc.pathname)
			if err != nil {
				result = err.Error()
			}
			if strings.Compare(result, tc.expected) != 0 {
				t.Errorf("result %s; expected %s", result, tc.expected)
			}
		})
	}
}
