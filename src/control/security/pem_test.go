//
// (C) Copyright 2019 Intel Corporation.
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

package security

import (
	"fmt"
	"os"
	"strings"
	"testing"
)

func TestLoadPEMData(t *testing.T) {

	goodCertPath := "testdata/certs/daosCA.crt"
	badCertPerm := "testdata/certs/badperms.crt"
	badCertPermError := &insecureError{badCertPerm, SafeCertPerm}

	testCases := []struct {
		filename string
		perms    os.FileMode
		testname string
		expected string
	}{
		{goodCertPath, SafeCertPerm, "GoodCert", ""},
		{badCertPerm, SafeCertPerm, "ImproperPermissions", badCertPermError.Error()},
	}

	// Setup permissions for tests below.
	if err := os.Chmod(goodCertPath, SafeCertPerm); err != nil {
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
	badKeyPermError := &insecureError{badKeyPerm, SafeKeyPerm}
	malformedError := fmt.Sprintf("%s does not contain PEM data", malformed)
	toomanyError := "Only one key allowed per file"
	notkeyError := "PEM Block is not a Private Key"

	// Setup permissions for tests below.
	if err := os.Chmod(goodKeyPath, SafeKeyPerm); err != nil {
		t.Fatal(err)
	}
	// Intentionallly safe cert perm as it should be incorrect
	if err := os.Chmod(badKeyPerm, SafeCertPerm); err != nil {
		t.Fatal(err)
	}
	if err := os.Chmod(malformed, SafeKeyPerm); err != nil {
		t.Fatal(err)
	}
	if err := os.Chmod(toomany, SafeKeyPerm); err != nil {
		t.Fatal(err)
	}
	if err := os.Chmod(notkey, SafeKeyPerm); err != nil {
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
func TestValidateCertDirectory(t *testing.T) {
	goodDirPath := "testdata/certs/goodperms"
	badDirPerm := "testdata/certs/badperms"
	notDir := "testdata/certs/daosCA.crt"
	badDirPermError := &insecureError{badDirPerm, SafeDirPerm}
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

	if err := os.Chmod(goodDirPath, SafeDirPerm); err != nil {
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
