//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package common

import (
	"fmt"
	"io/ioutil"
	"os"
	"path"
	"path/filepath"
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestUtils_ResolvePath(t *testing.T) {
	workingDir, err := os.Getwd()
	if err != nil {
		t.Fatal(err)
	}

	selfPath, err := os.Readlink("/proc/self/exe")
	if err != nil {
		t.Fatal(err)
	}
	adjacentDir := path.Dir(selfPath)

	for name, tc := range map[string]struct {
		inPath      string
		defaultPath string
		expected    string
		expErrMsg   string
	}{
		"absolute path": {"/foo/bar", "some/default", "/foo/bar", ""},
		"relative path": {"foo/bar", "some/default", path.Join(workingDir, "foo/bar"), ""},
		"empty path":    {"", "some/default", path.Join(adjacentDir, "some/default"), ""},
	} {
		t.Run(name, func(t *testing.T) {
			outPath, err := ResolvePath(tc.inPath, tc.defaultPath)
			ExpectError(t, err, tc.expErrMsg, name)

			if diff := cmp.Diff(tc.expected, outPath); diff != "" {
				t.Fatalf("unexpected resolved path (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestUtils_FindBinaryInPath(t *testing.T) {
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

func TestUtils_FindBinaryAdjacent(t *testing.T) {
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
