package main

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/daos-stack/daos/src/control/common/test"
)

var (
	testPoolDirs = [...]string{"a", "ab", "aac", "aaad"}
	testVosFiles = [...]string{"vos-0", "vos-1", "vos-2", "vos-10", "vos-201", "vos-000", "vos-a", "rdb-pool", "rdb-666"}
)

func createFile(t *testing.T, filePath string) {
	t.Helper()

	fd, err := os.Create(filePath)
	if err != nil {
		t.Fatalf("Failed to create test vos file %s: %v", filePath, err)
	}
	fd.Close()
}

func createDirAll(t *testing.T, dirPath string) {
	t.Helper()

	if err := os.MkdirAll(dirPath, 0755); err != nil {
		t.Fatalf("Failed to create test pool directory %s: %v", dirPath, err)
	}
}

func testSetup(t *testing.T) (tmpDir string, teardown func()) {
	t.Helper()

	tmpDir, teardown = test.CreateTestDir(t)

	for _, dir := range testPoolDirs {
		createDirAll(t, filepath.Join(tmpDir, dir))
		for _, file := range testVosFiles {
			createFile(t, filepath.Join(tmpDir, dir, file))
		}
	}

	createDirAll(t, filepath.Join(tmpDir, "foo"))
	createFile(t, filepath.Join(tmpDir, "foo", "bar"))

	createDirAll(t, filepath.Join(tmpDir, "bar"))
	createDirAll(t, filepath.Join(tmpDir, "bar", "foo"))
	createDirAll(t, filepath.Join(tmpDir, "bar", "baz"))
	createFile(t, filepath.Join(tmpDir, "bar", "baz", "no_vos"))

	return
}

func TestListVosFiles(t *testing.T) {
	tmpDir, teardown := testSetup(t)
	t.Cleanup(teardown)

	for name, tc := range map[string]struct {
		args   string
		expRes []string
	}{
		"unaccessible": {
			args:   "/root/",
			expRes: []string{},
		},
		"No match": {
			args:   filepath.Join(tmpDir, "z"),
			expRes: []string{},
		},
		"void director prefix": {
			args: tmpDir + string(os.PathSeparator),
			expRes: []string{
				filepath.Join(tmpDir, "a") + string(os.PathSeparator),
				filepath.Join(tmpDir, "ab") + string(os.PathSeparator),
				filepath.Join(tmpDir, "aac") + string(os.PathSeparator),
				filepath.Join(tmpDir, "aaad") + string(os.PathSeparator),
				filepath.Join(tmpDir, "foo") + string(os.PathSeparator),
				filepath.Join(tmpDir, "bar") + string(os.PathSeparator),
			},
		},
		"a pool directory prefix": {
			args: filepath.Join(tmpDir, "a"),
			expRes: []string{
				filepath.Join(tmpDir, "a") + string(os.PathSeparator),
				filepath.Join(tmpDir, "ab") + string(os.PathSeparator),
				filepath.Join(tmpDir, "aac") + string(os.PathSeparator),
				filepath.Join(tmpDir, "aaad") + string(os.PathSeparator),
			},
		},
		"aa pool directory prefix": {
			args: filepath.Join(tmpDir, "aa"),
			expRes: []string{
				filepath.Join(tmpDir, "aac") + string(os.PathSeparator),
				filepath.Join(tmpDir, "aaad") + string(os.PathSeparator),
			},
		},
		"all vos files": {
			args: filepath.Join(tmpDir, "a") + string(os.PathSeparator),
			expRes: []string{
				filepath.Join(tmpDir, "a", "vos-0"),
				filepath.Join(tmpDir, "a", "vos-1"),
				filepath.Join(tmpDir, "a", "vos-2"),
				filepath.Join(tmpDir, "a", "vos-10"),
				filepath.Join(tmpDir, "a", "vos-201"),
				filepath.Join(tmpDir, "a", "rdb-pool"),
			},
		},
		"vos-1 prefix files": {
			args: filepath.Join(tmpDir, "a", "vos-1"),
			expRes: []string{
				filepath.Join(tmpDir, "a", "vos-1"),
				filepath.Join(tmpDir, "a", "vos-10"),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			results := listVosFiles(tc.args)
			test.AssertStringsEqual(t, tc.expRes, results, "listDirVos results do not match expected")
		})
	}
}

func TestFilterSuggestions(t *testing.T) {
	// The test cases are designed to cover various prefix scenarios.
	// It should notably cover the case where the prefix is a single character that matches the
	// second character of a suggestion, which is a special case in the appendSuggestion
	// function:  Workaround to properly handle invalid prefix management done by the grumble
	// completion engine.
	var (
		initialSuggestions    = []string{"-a", "--all", "-b", "--bar="}
		additionalSuggestions = []string{"foo", "a", "ab", "aac", "aaad"}
	)

	for name, tc := range map[string]struct {
		prefix string
		expRes []string
	}{
		"no prefix": {
			prefix: "",
			expRes: []string{"-a", "--all", "-b", "--bar=", "foo", "a", "ab", "aac", "aaad"},
		},
		"no match prefix": {
			prefix: "z",
			expRes: []string{},
		},
		"with '-' prefix": {
			prefix: "-",
			expRes: []string{"a", "--all", "b", "--bar="},
		},
		"with '--' prefix": {
			prefix: "--",
			expRes: []string{"--all", "--bar="},
		},
		"with 'a' prefix": {
			prefix: "a",
			expRes: []string{"", "b", "aac", "aaad"},
		},
		"with 'aa' prefix": {
			prefix: "aa",
			expRes: []string{"aac", "aaad"},
		},
		"with 'aaa' prefix": {
			prefix: "aaa",
			expRes: []string{"aaad"},
		},
	} {
		t.Run(name, func(t *testing.T) {
			results := filterSuggestions(tc.prefix, initialSuggestions, additionalSuggestions)
			test.AssertStringsEqual(t, tc.expRes, results, "filterSuggestions results do not match expected")
		})
	}
}
