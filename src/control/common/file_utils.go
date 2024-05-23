//
// (C) Copyright 2019-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package common

import (
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"os/exec"
	"path"
	"path/filepath"
	"strings"

	"github.com/pkg/errors"
	yaml "gopkg.in/yaml.v2"
)

// UtilLogDepth signifies stack depth, set calldepth on calls to logger so
// log message context refers to caller not callee.
const UtilLogDepth = 4

// GetFilePaths return full file paths in given directory with
// matching file extensions
func GetFilePaths(dir string, ext string) ([]string, error) {
	files, err := ioutil.ReadDir(dir)
	if err != nil {
		return nil, err
	}
	extension := ext
	// if extension has been provided without '.' prefix, add one
	if filepath.Ext(ext) == "" {
		extension = fmt.Sprintf(".%s", ext)
	}
	var matchingFiles []string
	for _, file := range files {
		if filepath.Ext(file.Name()) == extension {
			matchingFiles = append(
				matchingFiles,
				fmt.Sprintf("%s/%s", dir, file.Name()))
		}
	}
	return matchingFiles, nil
}

// TruncFile overrides existing or creates new file with default options
func TruncFile(path string) (*os.File, error) {
	return os.OpenFile(path, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, 0664)
}

// AppendFile appends to existing or creates new file with default options
func AppendFile(path string) (*os.File, error) {
	return os.OpenFile(path, os.O_WRONLY|os.O_CREATE|os.O_APPEND, 0664)
}

// WriteSlice writes string slice to specified file, overwriting and creating
// if non-existent.
func WriteSlice(path string, slice []string) (err error) {
	file, err := TruncFile(path)
	if err != nil {
		return
	}
	defer file.Close()

	sep := "\n"
	for _, line := range slice {
		if _, err = file.WriteString(line + sep); err != nil {
			return
		}
	}

	return file.Sync()
}

// WriteString writes string to specified file, wrapper around WriteSlice.
func WriteString(path string, s string) error {
	return WriteSlice(path, []string{s})
}

// StructsToString returns yaml representation (as a list of strings) of any
// interface but avoids fields/lines prefixed with xxx_ such as added by
// protobuf boilerplate.
func StructsToString(i interface{}) (lines string, err error) {
	s, err := yaml.Marshal(i)
	if err != nil {
		return
	}
	for _, l := range strings.Split(string(s), "\n") {
		if !strings.Contains(l, "xxx_") {
			lines = lines + l + "\n"
		}
	}
	return
}

// PrintStructs dumps friendly YAML representation of structs to stdout
// proceeded with "name" identifier.
func PrintStructs(name string, i interface{}) {
	fmt.Println(name + ":")
	s, err := StructsToString(i)
	if err != nil {
		fmt.Println("Unable to YAML encode response: ", err)
		return
	}
	fmt.Println(s)
}

// WriteFileAtomic mimics ioutil.WriteFile, but it makes sure the file is
// either successfully written persistently or untouched.
func WriteFileAtomic(path string, data []byte, perm os.FileMode) error {
	// Write a staging file.
	staging := path + ".staging"
	if err := writeFile(staging, data, perm); err != nil {
		return errors.WithStack(err)
	}

	// Rename the staging file to the destination.
	if err := os.Rename(staging, path); err != nil {
		os.Remove(staging)
		return errors.WithStack(err)
	}

	// Sync the rename.
	return SyncDir(filepath.Dir(path))
}

// writeFile mimics ioutil.WriteFile, but syncs the file before returning. The
// error is one from the standard library.
func writeFile(path string, data []byte, perm os.FileMode) (err error) {
	f, err := os.OpenFile(path, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, perm)
	if err != nil {
		return
	}
	defer func() {
		if tmperr := f.Close(); tmperr != nil && err == nil {
			err = tmperr
		}
		if err != nil {
			os.Remove(path)
		}
	}()

	n, err := f.Write(data)
	if err != nil {
		return
	} else if n < len(data) {
		return fmt.Errorf("write %s: only wrote %d/%d", path, n, len(data))
	}

	return f.Sync()
}

// SyncDir flushes all prior modifications to a directory. This is required if
// one modifies a directory (e.g., by creating a new file in it) and needs to
// wait for this modification to become persistent.
func SyncDir(path string) (err error) {
	defer func() { err = errors.WithStack(err) }()

	// Since a directory can't be opened for writing, os.Open suffices.
	d, err := os.Open(path)
	if err != nil {
		return
	}
	defer func() {
		if tmperr := d.Close(); tmperr != nil && err == nil {
			err = tmperr
		}
	}()

	return d.Sync()
}

// Run executes command in os and builds useful error message.
func Run(cmd string) error {
	// executing as subshell enables pipes in cmd string
	out, err := exec.Command("sh", "-c", cmd).CombinedOutput()
	if err != nil {
		err = errors.Wrapf(
			err, "Error running %s: %s", cmd, out)
	}

	return err
}

// GetWorkingPath retrieves path relative to the current working directory when
// invoking the current process.
func GetWorkingPath(inPath string) (string, error) {
	if path.IsAbs(inPath) {
		return "", errors.New("unexpected absolute path, want relative")
	}

	workingDir, err := os.Getwd()
	if err != nil {
		return "", errors.Wrap(err, "unable to determine working directory")
	}

	return path.Join(workingDir, inPath), nil
}

// GetAdjacentPath retrieves path relative to the binary used to launch the
// currently running process.
func GetAdjacentPath(inPath string) (string, error) {
	if path.IsAbs(inPath) {
		return "", errors.New("unexpected absolute path, want relative")
	}

	selfPath, err := os.Readlink("/proc/self/exe")
	if err != nil {
		return "", errors.Wrap(err, "unable to determine path to self")
	}

	return path.Join(path.Dir(selfPath), inPath), nil
}

// ResolvePath simply returns an absolute path, appends input path to current
// working directory if input path not empty otherwise appends default path to
// location of running binary (adjacent). Use case is specific to config files.
func ResolvePath(inPath string, defaultPath string) (outPath string, err error) {
	switch {
	case inPath == "":
		// no custom path specified, look up adjacent
		outPath, err = GetAdjacentPath(defaultPath)
	case filepath.IsAbs(inPath):
		outPath = inPath
	default:
		// custom path specified, look up relative to cwd
		outPath, err = GetWorkingPath(inPath)
	}

	if err != nil {
		return "", err
	}

	return outPath, nil
}

// FindBinary attempts to locate the named binary by checking $PATH first.
// If the binary is not found in $PATH, look in the directory containing the
// running processes binary as well as the working directory.
func FindBinary(binName string) (string, error) {
	binPath, err := exec.LookPath(binName)
	if err == nil {
		return binPath, nil
	}

	adjPath, err := GetAdjacentPath(binName)
	if err != nil {
		return "", err
	}

	if _, err = os.Stat(adjPath); err != nil {
		return "", err
	}

	return adjPath, nil
}

// CpFile copies a file from src to dst.
func CpFile(src, dst string) error {
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()

	out, err := os.Create(dst)
	if err != nil {
		return err
	}
	defer out.Close()

	_, err = io.Copy(out, in)
	if err != nil {
		return err
	}

	return nil
}

// Copy the Directory from source to destination.
func CpDir(source string, dest string) error {
	// get properties of source dir
	sourceinfo, err := os.Stat(source)
	if err != nil {
		return errors.Wrap(err, "unable to get FileInfo structure")
	}

	// create dest dir
	err = os.MkdirAll(dest, sourceinfo.Mode())
	if err != nil {
		return errors.Wrap(err, "unable to create destination Folder")
	}

	directory, _ := os.Open(source)
	objects, err := directory.Readdir(-1)

	for _, obj := range objects {
		sourceFile := source + "/" + obj.Name()
		destinationFile := dest + "/" + obj.Name()

		if obj.IsDir() {
			// create sub-directories - recursively
			err = CpDir(sourceFile, destinationFile)
			if err != nil {
				return errors.Wrap(err, "unable to Copy Dir")
			}
		} else {
			// perform the file copy
			err = CpFile(sourceFile, destinationFile)
			if err != nil {
				return errors.Wrap(err, "unable to Copy File")
			}
		}

	}
	return nil
}

// Check if file or directory that starts with . which is hidden
func IsHidden(filename string) bool {
	if filename != "" && filename[0:1] == "." {
		return true
	}

	return false
}

// Normalize the input path with removing redundant separators, up-level reference, changing relative
// path to absolute one, etc.
func NormalizePath(p string) (np string, err error) {
	np, err = filepath.EvalSymlinks(p)
	if err != nil {
		return
	}

	if !filepath.IsAbs(np) {
		np, err = filepath.Abs(np)
		if err != nil {
			return
		}
	}

	return
}

// HasPrefixPath reports whether sub parameter path is a prefix of the base parameter one.
func HasPrefixPath(base, sub string) (bool, error) {
	var err error

	base, err = NormalizePath(base)
	if err != nil {
		return false, err
	}
	baseList := strings.Split(base, string(os.PathSeparator))[1:]

	sub, err = NormalizePath(sub)
	if err != nil {
		return false, err
	}
	subList := strings.Split(sub, string(os.PathSeparator))[1:]

	if len(baseList) > len(subList) {
		return false, nil
	}

	for i, file := range baseList {
		if file != subList[i] {
			return false, nil
		}
	}

	return true, nil
}
