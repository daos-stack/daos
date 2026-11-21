//
// (C) Copyright 2019-2022 Intel Corporation.
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
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
	"github.com/pkg/errors"

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

func TestUtils_MkdirForcePerm(t *testing.T) {
	expPerm := 0777

	testDir, clean := CreateTestDir(t)
	defer clean()

	testPath := path.Join(testDir, "foo")
	err := MkdirForcePerm(testPath, os.FileMode(expPerm))
	if err != nil {
		t.Fatalf("Unexpected error: %q", err)
	}

	info, err := os.Stat(testPath)
	if err != nil {
		t.Fatalf("Unexpected error: %q", err)
	}

	perm := int(info.Mode().Perm())
	if perm != expPerm {
		t.Fatalf("Created directory has unexpected permissions: %04o instead of %04o", perm, expPerm)
	}
}

func TestUtils_SyncDir(t *testing.T) {
	for name, tc := range map[string]struct {
		path   func(t *testing.T) string
		expErr error
	}{
		"existing directory": {
			path: func(t *testing.T) string {
				testDir, cleanup := CreateTestDir(t)
				t.Cleanup(cleanup)
				return testDir
			},
		},
		"nonexistent path": {
			path: func(t *testing.T) string {
				return "/nonexistent/path/that/should/not/exist"
			},
			expErr: errors.New("no such file or directory"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotErr := SyncDir(tc.path(t))
			CmpErr(t, tc.expErr, gotErr)
		})
	}
}

func TestUtils_WriteFileAtomic(t *testing.T) {
	for name, tc := range map[string]struct {
		existing     []byte
		staleStaging bool
		perm         os.FileMode
		expErr       error
	}{
		"new file": {
			perm: 0644,
		},
		"replace read-only file": {
			existing: []byte("old"),
			perm:     0400,
		},
		"stale read-only staging file": {
			staleStaging: true,
			perm:         0400,
		},
		"missing directory": {
			perm:   0644,
			expErr: errors.New("no such file or directory"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			testDir, cleanup := CreateTestDir(t)
			defer cleanup()

			path := filepath.Join(testDir, "file")
			if tc.expErr != nil {
				path = filepath.Join(testDir, "missing", "file")
			}
			if tc.existing != nil {
				if err := os.WriteFile(path, tc.existing, tc.perm); err != nil {
					t.Fatal(err)
				}
			}
			if tc.staleStaging {
				if err := os.WriteFile(path+".staging", []byte("stale"), 0400); err != nil {
					t.Fatal(err)
				}
			}

			gotErr := WriteFileAtomic(path, []byte("new"), tc.perm)
			CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			got, err := os.ReadFile(path)
			if err != nil {
				t.Fatal(err)
			}
			AssertEqual(t, "new", string(got), "content")
			fi, err := os.Stat(path)
			if err != nil {
				t.Fatal(err)
			}
			AssertEqual(t, tc.perm, fi.Mode().Perm(), "mode")
			if _, err := os.Stat(path + ".staging"); !os.IsNotExist(err) {
				t.Fatalf("staging file left behind: %v", err)
			}
		})
	}
}
