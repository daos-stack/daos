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

package main

import (
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/pkg/errors"
)

func createTestDir(t *testing.T) (string, func()) {
	t.Helper()
	testDir, err := ioutil.TempDir("", strings.Replace(t.Name(), "/", "-", -1))
	if err != nil {
		t.Fatal(err)
	}
	return testDir, func() {
		os.RemoveAll(testDir)
	}
}

func createTestFile(t *testing.T, filePath string, content string) {
	file, err := os.Create(filePath)
	if err != nil {
		t.Fatal(err)
	}
	_, err = file.WriteString(content)
	if err != nil {
		t.Fatal(err)
	}
	file.Close()
}

func TestReadACLFile_FileOpenFailed(t *testing.T) {
	result, err := readACLFile("badfile.txt")

	if result != nil {
		t.Error("Expected no result, got something back")
	}

	if err == nil {
		t.Fatal("Expected an error, got nil")
	}

	// NB: Not really convinced that testing for a specific error
	// is that useful, but this is fine.
	if !os.IsNotExist(errors.Cause(err)) {
		t.Errorf("Wrong error '%s' (expected to be os.IsNotExist)", err)
	}
}

func TestReadACLFile_EmptyFile(t *testing.T) {
	testDir, cleanup := createTestDir(t)
	defer cleanup()

	testPath := filepath.Join(testDir, "empty-file.txt")
	createTestFile(t, testPath, "")

	result, err := readACLFile(testPath)

	if err != nil {
		t.Errorf("Expected no error, got '%s'", err.Error())
	}

	if result == nil {
		t.Error("Expected result, got nil")
	}

	if len(result) != 0 {
		t.Errorf("Expected empty result, got %d items", len(result))
	}
}

func TestReadACLFile_OneValidACE(t *testing.T) {
	expectedACE := "A::OWNER@:rw"
	testDir, cleanup := createTestDir(t)
	defer cleanup()

	testPath := filepath.Join(testDir, "file.txt")
	createTestFile(t, testPath, expectedACE+"\n")

	result, err := readACLFile(testPath)

	if err != nil {
		t.Errorf("Expected no error, got '%s'", err.Error())
	}

	if result == nil {
		t.Error("Expected result, got nil")
	}

	if len(result) != 1 {
		t.Fatalf("Expected 1 result, got %d items", len(result))
	}

	if result[0] != expectedACE {
		t.Errorf("Expected ACE '%s', got '%s'", expectedACE, result[0])
	}
}

func TestReadACLFile_WhitespaceExcluded(t *testing.T) {
	expectedACE := "A::OWNER@:rw"

	testDir, cleanup := createTestDir(t)
	defer cleanup()

	testPath := filepath.Join(testDir, "file.txt")
	createTestFile(t, testPath, expectedACE+" \n\n")

	result, err := readACLFile(testPath)

	if err != nil {
		t.Errorf("Expected no error, got '%s'", err.Error())
	}

	if result == nil {
		t.Error("Expected result, got nil")
	}

	if len(result) != 1 {
		t.Fatalf("Expected 1 result, got %d items", len(result))
	}

	if result[0] != expectedACE {
		t.Errorf("Expected ACE '%s', got '%s'", expectedACE, result[0])
	}
}

func TestReadACLFile_MultiValidACE(t *testing.T) {
	expectedACEs := []string{
		"A:g:GROUP@:r",
		"A::OWNER@:rw",
		"A:g:readers@:r",
		"L:f:baduser@:rw",
		"U:f:EVERYONE@:rw",
	}

	testDir, cleanup := createTestDir(t)
	defer cleanup()

	testPath := filepath.Join(testDir, "file.txt")
	createTestFile(t, testPath, strings.Join(expectedACEs, "\n"))

	result, err := readACLFile(testPath)

	if err != nil {
		t.Errorf("Expected no error, got '%s'", err.Error())
	}

	if result == nil {
		t.Error("Expected result, got nil")
	}

	if len(result) != len(expectedACEs) {
		t.Fatalf("Expected %d results, got %d items",
			len(expectedACEs), len(result))
	}

	for i, resultACE := range result {
		if resultACE != expectedACEs[i] {
			t.Errorf("Expected ACE string '%s' at index %d, got '%s'",
				expectedACEs[i], i, resultACE)
		}
	}
}

func TestReadACLFile_ErrorReadingFile(t *testing.T) {
	testDir, cleanup := createTestDir(t)
	defer cleanup()

	testPath := filepath.Join(testDir, "file.txt")
	createTestFile(t, testPath, "")
	if err := os.Chmod(testPath, 0000); err != nil {
		t.Fatal(err)
	}

	result, err := readACLFile(testPath)

	if result != nil {
		t.Error("Expected nil result, but got a result")
	}

	if err == nil {
		t.Fatal("Expected an error, got nil")
	}
}
