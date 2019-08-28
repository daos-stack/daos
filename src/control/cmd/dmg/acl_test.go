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
	"strings"
	"testing"
)

// badFileOpener is a mock used to represent failure to open a file
type badFileOpener struct {
	errorMsg string
}

func (b *badFileOpener) OpenFile(filename string) (readableFile, error) {
	return nil, errors.New(b.errorMsg)
}

// mockReadableFile is a mock used to represent a file to be read
type mockReadableFile struct {
	fileText string
	read     int
}

func (f *mockReadableFile) Close() error {
	return nil
}

func (f *mockReadableFile) Read(p []byte) (n int, err error) {
	if f.read >= len(f.fileText) {
		return 0, io.EOF
	}

	copy(p, f.fileText[f.read:])

	remaining := len(f.fileText) - f.read
	var readLen int
	if remaining > len(p) {
		readLen = len(p)
	} else {
		readLen = remaining
	}
	f.read = f.read + readLen
	return readLen, nil
}

type mockErrorFile struct {
	errorMsg string
}

func (f *mockErrorFile) Close() error {
	return nil
}

func (f *mockErrorFile) Read(p []byte) (n int, err error) {
	return 1, errors.New(f.errorMsg)
}

// mockFileOpener is a mock that returns a readableFile
type mockFileOpener struct {
	textFile readableFile
}

func (m *mockFileOpener) OpenFile(filename string) (readableFile, error) {
	return m.textFile, nil
}

func TestReadACLFile_FileOpenFailed(t *testing.T) {
	expectedError := "badFileOpener error"
	badOpener := &badFileOpener{
		errorMsg: expectedError,
	}

	result, err := readACLFile(badOpener, "badfile.txt")

	if result != nil {
		t.Error("Expected no result, got something back")
	}

	if err == nil {
		t.Fatal("Expected an error, got nil")
	}

	if !strings.Contains(err.Error(), expectedError) {
		t.Errorf("Wrong error message '%s' (expected to contain '%s')",
			err.Error(), expectedError)
	}
}

func TestReadACLFile_EmptyFile(t *testing.T) {
	mockFile := &mockReadableFile{}
	mockOpener := &mockFileOpener{
		textFile: mockFile,
	}

	result, err := readACLFile(mockOpener, "file.txt")

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
	mockFile := &mockReadableFile{
		fileText: expectedACE + "\n",
	}
	mockOpener := &mockFileOpener{
		textFile: mockFile,
	}

	result, err := readACLFile(mockOpener, "file.txt")

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
	mockFile := &mockReadableFile{
		fileText: expectedACE + " \n\n",
	}
	mockOpener := &mockFileOpener{
		textFile: mockFile,
	}

	result, err := readACLFile(mockOpener, "file.txt")

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

	mockFile := &mockReadableFile{
		fileText: strings.Join(expectedACEs, "\n"),
	}
	mockOpener := &mockFileOpener{
		textFile: mockFile,
	}

	result, err := readACLFile(mockOpener, "file.txt")

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
	expectedError := "mockErrorFile error"
	mockFile := &mockErrorFile{
		errorMsg: expectedError,
	}
	mockOpener := &mockFileOpener{
		textFile: mockFile,
	}

	result, err := readACLFile(mockOpener, "file.txt")

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
