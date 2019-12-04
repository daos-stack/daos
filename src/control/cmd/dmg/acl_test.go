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
	"errors"
	"io"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/client"
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

func createTestFile(t *testing.T, filePath string, content string) {
	file, err := os.Create(filePath)
	if err != nil {
		t.Fatal(err)
	}
	defer file.Close()

	_, err = file.WriteString(content)
	if err != nil {
		t.Fatal(err)
	}
}

func TestReadACLFile_Success(t *testing.T) {
	path := filepath.Join(os.TempDir(), "testACLFile.txt")
	createTestFile(t, path, "A::OWNER@:rw\nA::user1@:rw\nA:g:group1@:r\n")
	defer os.Remove(path)

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

func TestParseACL_EmptyFile(t *testing.T) {
	mockFile := &mockReader{}

	result, err := parseACL(mockFile)

	if err != nil {
		t.Errorf("Expected no error, got '%s'", err.Error())
	}

	if result == nil {
		t.Error("Expected result, got nil")
	}

	if len(result.Entries) != 0 {
		t.Errorf("Expected empty result, got %d items", len(result.Entries))
	}
}

func TestParseACL_OneValidACE(t *testing.T) {
	expectedACE := "A::OWNER@:rw"
	expectedACL := &client.AccessControlList{Entries: []string{expectedACE}}
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
	expectedACL := &client.AccessControlList{Entries: []string{expectedACE}}
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
	expectedACL := &client.AccessControlList{
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
	expectedACL := &client.AccessControlList{
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
