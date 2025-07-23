//
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package build

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/pkg/errors"
)

func TestBuild_IsDefaultConfigNotFound(t *testing.T) {
	for name, tc := range map[string]struct {
		err       error
		expResult bool
	}{
		"ErrDefaultConfigNotFound": {
			err:       &ErrDefaultConfigNotFound{},
			expResult: true,
		},
		"some other error": {
			err: errors.New("something happened"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			test.AssertEqual(t, tc.expResult, IsDefaultConfigNotFound(tc.err), "")
		})
	}
}

func TestBuild_ConfigDirs(t *testing.T) {
	for name, tc := range map[string]struct {
		configDir string
		expDirs   []string
	}{
		"default": {
			configDir: DefaultConfigDir,
			expDirs:   []string{DefaultConfigDir},
		},
		"custom": {
			configDir: "/home/cooldev/daos/etc",
			expDirs:   []string{"/home/cooldev/daos/etc", DefaultConfigDir},
		},
	} {
		t.Run(name, func(t *testing.T) {
			oldDir := ConfigDir
			defer func() {
				ConfigDir = oldDir
			}()

			ConfigDir = tc.configDir

			dirs := ConfigDirs()

			test.CmpAny(t, "config dir list", tc.expDirs, dirs)
		})
	}
}

func TestBuild_FindConfigFilePath(t *testing.T) {
	for name, tc := range map[string]struct {
		configDirNotExist  bool
		fileInConfigDir    bool
		defaultDirNotExist bool
		fileInDefaultDir   bool
		filename           string
		expPathConfigDir   bool
		expPathDefaultDir  bool
		expErr             error
	}{
		"absolute path": {
			filename: "/etc/daos/test.txt",
			expErr:   errors.New("already specifies a path"),
		},
		"local path": {
			filename: "./install/etc/test.txt",
			expErr:   errors.New("already specifies a path"),
		},
		"local path without current dir": {
			filename: "install/etc/test.txt",
			expErr:   errors.New("already specifies a path"),
		},
		"neither dir exists": {
			configDirNotExist:  true,
			defaultDirNotExist: true,
			filename:           "test.txt",
			expErr:             errors.New("not found"),
		},
		"both exist but no file": {
			filename: "test.txt",
			expErr:   errors.New("not found"),
		},
		"no default dir": {
			defaultDirNotExist: true,
			fileInConfigDir:    true,
			filename:           "test.txt",
			expPathConfigDir:   true,
		},
		"no config dir": {
			configDirNotExist: true,
			fileInDefaultDir:  true,
			filename:          "test.txt",
			expPathDefaultDir: true,
		},
		"config path has file": {
			fileInConfigDir:  true,
			filename:         "test.txt",
			expPathConfigDir: true,
		},
		"default path has file": {
			fileInDefaultDir:  true,
			filename:          "test.txt",
			expPathDefaultDir: true,
		},
		"both directories have file": {
			fileInConfigDir:  true,
			fileInDefaultDir: true,
			filename:         "test.txt",
			expPathConfigDir: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			basePath, cleanup := test.CreateTestDir(t)
			defer cleanup()

			oldCfgDir := ConfigDir
			oldDefaultDir := DefaultConfigDir
			defer func() {
				ConfigDir = oldCfgDir
				DefaultConfigDir = oldDefaultDir
			}()
			ConfigDir = filepath.Join(basePath, "config")
			DefaultConfigDir = filepath.Join(basePath, "default")

			if !tc.configDirNotExist {
				if err := os.Mkdir(ConfigDir, 0755); err != nil {
					t.Fatal(err)
				}
			}

			if !tc.defaultDirNotExist {
				if err := os.Mkdir(DefaultConfigDir, 0755); err != nil {
					t.Fatal(err)
				}
			}

			filePath := func(dir string) string {
				return filepath.Join(dir, tc.filename)
			}

			createFile := func(t *testing.T, dir string) {
				f, err := os.Create(filePath(dir))
				if err != nil {
					t.Fatal(err)
				}
				if _, err := f.WriteString("The rain in Spain stays mainly in the plain."); err != nil {
					t.Fatal(err)
				}

				if err := f.Close(); err != nil {
					t.Fatal(err)
				}
			}
			if tc.fileInConfigDir {
				createFile(t, ConfigDir)
			}
			if tc.fileInDefaultDir {
				createFile(t, DefaultConfigDir)
			}

			path, err := FindConfigFilePath(tc.filename)

			test.CmpErr(t, tc.expErr, err)

			// Tune the expected path to the test-created directories
			expPath := ""
			if tc.expPathConfigDir {
				expPath = filePath(ConfigDir)
			} else if tc.expPathDefaultDir {
				expPath = filePath(DefaultConfigDir)
			}
			test.AssertEqual(t, expPath, path, "")
		})
	}
}
