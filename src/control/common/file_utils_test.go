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
	"os/user"
	"path"
	"path/filepath"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"
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

func TestUtils_GetFileOwner(t *testing.T) {
	curUsr, _ := user.Current()

	for name, tc := range map[string]struct {
		createFile     bool
		createDir      bool
		expUsername    string
		expErr         error
		expErrNotExist bool
	}{
		"path doesnt exist": {
			expErr:         errors.New("stat"),
			expErrNotExist: true,
		},
		"path is a directory": {
			createDir:      true,
			expErr:         os.ErrNotExist,
			expErrNotExist: true,
		},
		"success": {
			createFile:  true,
			expUsername: curUsr.Username,
		},
	} {
		t.Run(name, func(t *testing.T) {
			tmpDir, cleanup := CreateTestDir(t)
			defer cleanup()

			name := "foo"
			path := filepath.Join(tmpDir, name)

			switch {
			case tc.createFile:
				f, err := os.Create(path)
				if err != nil {
					t.Fatal(err)
				}
				if err := f.Close(); err != nil {
					t.Fatal(err)
				}
				t.Logf("file created %q", path)
			case tc.createDir:
				pathDir, err := ioutil.TempDir(tmpDir, name)
				if err != nil {
					t.Fatal(err)
				}
				t.Logf("directory created %q", pathDir)
				path = pathDir
			}

			username, err := GetFileOwner(path)
			CmpErr(t, tc.expErr, err)
			AssertEqual(t, tc.expErrNotExist, os.IsNotExist(errors.Cause(err)),
				"unexpected not exist error state")
			AssertEqual(t, tc.expUsername, username, "unexpected owner username")
		})
	}
}
