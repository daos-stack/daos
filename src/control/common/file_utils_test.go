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

package common

import (
	"fmt"
	"io/ioutil"
	"os"
	"path"
	"path/filepath"
	"testing"
)

func TestFindBinaryInPath(t *testing.T) {
	testDir, err := ioutil.TempDir("", t.Name())
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(testDir)

	testName := t.Name()
	testFile, err := os.OpenFile(path.Join(testDir, testName), os.O_RDWR|os.O_CREATE, 0755)
	if err != nil {
		t.Fatal(err)
	}
	testFile.Close()

	oldPathEnv := os.Getenv("PATH")
	defer os.Setenv("PATH", oldPathEnv)
	if err := os.Setenv("PATH", fmt.Sprintf("%s:%s", oldPathEnv, testDir)); err != nil {
		t.Fatal(err)
	}

	t.Run("expected success", func(t *testing.T) {
		binPath, err := FindBinary(testName)
		if err != nil {
			t.Fatal(err)
		}
		if binPath != testFile.Name() {
			t.Fatalf("expected %q; got %q", testFile.Name(), binPath)
		}
	})
	t.Run("expected failure", func(t *testing.T) {
		_, err := FindBinary("noWayThisExistsQuackMoo")
		if err == nil {
			t.Fatal("expected lookup to fail")
		}
	})
}

func TestFindBinaryAdjacent(t *testing.T) {
	testDir := filepath.Dir(os.Args[0])
	testFile, err := os.OpenFile(path.Join(testDir, t.Name()), os.O_RDWR|os.O_CREATE, 0755)
	if err != nil {
		t.Fatal(err)
	}
	testFile.Close()

	binPath, err := FindBinary(t.Name())
	if err != nil {
		t.Fatal(err)
	}
	if binPath != testFile.Name() {
		t.Fatalf("expected %q; got %q", testFile.Name(), binPath)
	}
}
