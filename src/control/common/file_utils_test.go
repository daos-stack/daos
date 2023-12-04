//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package common

import (
	"fmt"
	"os"
	"path"
	"path/filepath"
	"testing"

	"github.com/google/go-cmp/cmp"

	. "github.com/daos-stack/daos/src/control/common/test"
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
	testDir, clean := CreateTestDir(t)
	defer clean()

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

func TestUtils_NormizePath(t *testing.T) {
	testDir, clean := CreateTestDir(t)
	defer clean()

	wd, err := os.Getwd()
	if err != nil {
		t.Fatalf("Unexpected error: %q", err)
	}
	defer os.Chdir(wd)
	if err = os.Chdir("/tmp"); err != nil {
		t.Fatalf("Unexpected error: %q", err)
	}

	testPath := path.Join(testDir, "foo")
	if _, err = NormalizePath(testPath); err == nil {
		t.Fatalf("Expected error: no such file or directory")
	}

	var normPath string
	testPath = filepath.Base(testDir)
	if normPath, err = NormalizePath(testPath); err != nil {
		t.Fatalf("Unexpected error: %q", err)
	}
	if normPath != testDir {
		t.Fatalf("Expected %q; got %q", testDir, normPath)
	}

	testPath = path.Join(testDir, "foo")
	if err = os.Symlink(testDir, testPath); err != nil {
		t.Fatalf("Unexpected error: %q", err)
	}
	if normPath, err = NormalizePath(testPath); err != nil {
		t.Fatalf("Unexpected error: %q", err)
	}
	if normPath != testDir {
		t.Fatalf("Expected %q; got %q", testDir, normPath)
	}
}

func TestUtils_HasPrefixPath(t *testing.T) {
	testDir, clean := CreateTestDir(t)
	defer clean()

	testPath := path.Join(testDir, "foo")
	testFile, err := os.Create(testPath)
	if err != nil {
		t.Fatalf("Unexpected error: %q", err)
	}
	defer testFile.Close()

	var hp bool
	hp, err = HasPrefixPath(testDir, testPath)
	if err != nil {
		t.Fatalf("Unexpected error: %q", err)
	}
	if !hp {
		t.Fatalf("%q is a prefix of %q", testDir, testPath)
	}

	hp, err = HasPrefixPath(testPath, testPath)
	if err != nil {
		t.Fatalf("Unexpected error: %q", err)
	}
	if !hp {
		t.Fatalf("%q is a prefix of %q", testDir, testPath)
	}

	hp, err = HasPrefixPath("/opt", testPath)
	if err != nil {
		t.Fatalf("Unexpected error: %q", err)
	}
	if hp {
		t.Fatalf("%q is not a prefix of %q", testDir, testPath)
	}

}
