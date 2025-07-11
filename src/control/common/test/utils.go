//
// (C) Copyright 2018-2024 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package test

import (
	"bufio"
	"context"
	"fmt"
	"io"
	"io/fs"
	"net"
	"os"
	"path/filepath"
	"reflect"
	"sort"
	"strings"
	"sync"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"golang.org/x/sys/unix"
	"google.golang.org/protobuf/testing/protocmp"

	"github.com/daos-stack/daos/src/control/logging"
)

// AssertTrue asserts b is true
func AssertTrue(t *testing.T, b bool, message string) {
	t.Helper()

	if !b {
		t.Fatal(message)
	}
}

// AssertFalse asserts b is false
func AssertFalse(t *testing.T, b bool, message string) {
	t.Helper()

	if b {
		t.Fatal(message)
	}
}

// AssertEqual asserts b is equal to a
//
// Whilst suitable in most situations, reflect.DeepEqual() may not be
// suitable for nontrivial struct element comparisons, go-cmp should
// then be used.
func AssertEqual(
	t *testing.T, a interface{}, b interface{}, message string) {
	t.Helper()

	if reflect.DeepEqual(a, b) {
		return
	}
	if len(message) > 0 {
		message += ", "
	}

	t.Fatalf(message+"%#v != %#v", a, b)
}

// AssertNotEqual asserts b is not equal to a
//
// Whilst suitable in most situations, reflect.DeepEqual() may not be
// suitable for nontrivial struct element comparisons, go-cmp should
// then be used.
func AssertNotEqual(
	t *testing.T, a interface{}, b interface{}, message string) {
	t.Helper()

	if !reflect.DeepEqual(a, b) {
		return
	}
	if len(message) > 0 {
		message += ", "
	}

	t.Fatalf(message+"%#v == %#v", a, b)
}

// AssertStringsEqual sorts string slices before comparing.
func AssertStringsEqual(
	t *testing.T, a []string, b []string, message string) {
	t.Helper()

	sort.Strings(a)
	sort.Strings(b)

	AssertEqual(t, a, b, message)
}

// ExpectError asserts error contains expected message
func ExpectError(t *testing.T, actualErr error, expectedMessage string, desc interface{}) {
	t.Helper()

	if actualErr == nil {
		if expectedMessage != "" {
			t.Fatalf("expected a non-nil error: %v", desc)
		}
	} else if diff := cmp.Diff(expectedMessage, actualErr.Error()); diff != "" {
		t.Fatalf("unexpected error (-want, +got):\n%s\n", diff)
	}
}

// CmpErrBool compares two errors and returns a boolean value indicating equality
// or at least close similarity between their messages.
func CmpErrBool(want, got error) bool {
	if want == got {
		return true
	}

	if want == nil || got == nil {
		return false
	}
	if !strings.Contains(got.Error(), want.Error()) {
		return false
	}

	return true
}

// CmpErr compares two errors for equality or at least close similarity in their messages.
func CmpErr(t *testing.T, want, got error) {
	t.Helper()

	if want != nil && want.Error() == "" {
		t.Fatal("comparison with empty error will always return true, don't do it")
	}

	if !CmpErrBool(want, got) {
		t.Fatalf("unexpected error\n(wanted: %v, got: %v)", want, got)
	}
}

// CmpAny compares two values and fails the test if they are not equal.
func CmpAny(t *testing.T, desc string, want, got any, cmpOpts ...cmp.Option) {
	t.Helper()

	if diff := cmp.Diff(want, got, cmpOpts...); diff != "" {
		t.Fatalf("unexpected %s (-want, +got):\n%s\n", desc, diff)
	}
}

// SplitFile separates file content into contiguous sections separated by
// a blank line.
func SplitFile(path string) (sections [][]string, err error) {
	file, err := os.Open(path)
	if err != nil {
		return
	}
	defer file.Close()

	scanner := bufio.NewScanner(file)
	var lines []string
	for scanner.Scan() {
		if scanner.Text() == "" {
			sections = append(sections, lines)
			lines = make([]string, 0)
		} else {
			lines = append(lines, scanner.Text())
		}
	}
	if len(lines) > 0 {
		sections = append(sections, lines)
	}

	return
}

// LoadTestFiles reads inputs and outputs from file and do basic sanity checks.
// Both files contain entries of multiple lines separated by blank line.
// Return inputs and outputs, both of which are slices of string slices.
func LoadTestFiles(inFile string, outFile string) (
	inputs [][]string, outputs [][]string, err error) {

	inputs, err = SplitFile(inFile)
	if err != nil {
		return
	}
	outputs, err = SplitFile(outFile)
	if err != nil {
		return
	}

	if len(inputs) < 1 {
		err = fmt.Errorf("no inputs read from file")
	} else if len(inputs) != len(outputs) {
		err = fmt.Errorf("number of inputs and outputs not equal")
	}

	return
}

// ShowBufferOnFailure displays captured output on test failure. Should be run
// via defer in the test function.
func ShowBufferOnFailure(t *testing.T, buf fmt.Stringer) {
	t.Helper()

	if t.Failed() {
		fmt.Printf("captured log output:\n%s", buf.String())
	}
	if r, ok := buf.(interface{ Reset() }); ok {
		r.Reset()
	}
}

// DefaultCmpOpts gets default go-cmp comparison options for tests.
func DefaultCmpOpts() []cmp.Option {
	return []cmp.Option{
		cmpopts.IgnoreTypes(sync.Mutex{}, sync.RWMutex{}),
		protocmp.Transform(), // makes Protobuf structs comparable
	}
}

// CmpOptIgnoreFieldAnyType creates a cmp.Option that allows go-cmp comparisons to ignore all
// fields with a specific name in any type.
func CmpOptIgnoreFieldAnyType(field string) cmp.Option {
	return cmp.FilterPath(
		func(p cmp.Path) bool {
			return p.Last().String() == field || p.Last().String() == ("."+field)
		},
		cmp.Ignore())
}

// CmpOptEquateErrorMessages creates a cmp.Option that allows go-cmp to compare errors by message.
func CmpOptEquateErrorMessages() cmp.Option {
	areConcreteErrors := func(x, y interface{}) bool {
		_, ok1 := x.(error)
		_, ok2 := y.(error)
		return ok1 && ok2
	}
	return cmp.FilterValues(areConcreteErrors, cmp.Comparer(func(x, y interface{}) bool {
		xe := x.(error)
		ye := y.(error)
		return xe.Error() == ye.Error()
	}))
}

// CreateTestDir creates a temporary test directory.
// It returns the path to the directory and a cleanup function.
func CreateTestDir(t *testing.T) (string, func()) {
	t.Helper()

	name := strings.Replace(t.Name(), "/", "-", -1)
	tmpDir, err := os.MkdirTemp("", name)
	if err != nil {
		t.Fatalf("Couldn't create temporary directory: %v", err)
	}

	return tmpDir, func() {
		err := os.RemoveAll(tmpDir)
		if err != nil {
			t.Fatalf("Couldn't remove tmp dir: %v", err)
		}
	}
}

// RemoveContents removes contents of a directory.
func RemoveContents(t *testing.T, dir string) error {
	files, err := filepath.Glob(filepath.Join(dir, "*"))
	if err != nil {
		return err
	}
	for _, file := range files {
		err = os.RemoveAll(file)
		if err != nil {
			return err
		}
	}
	return nil
}

// CreateTestFile creates a file in the given directory with a random name, and
// writes the content string to the file. It returns the path to the file.
func CreateTestFile(t *testing.T, dir, content string) string {
	t.Helper()

	file, err := os.CreateTemp(dir, "")
	if err != nil {
		t.Fatal(err)
	}
	defer file.Close()

	_, err = file.WriteString(content)
	if err != nil {
		t.Fatal(err)
	}

	return file.Name()
}

// CreateTestSocket creates a Unix Domain Socket that can listen for connections
// on a given path. It returns the listener and a cleanup function.
func CreateTestSocket(t *testing.T, sockPath string) (*net.UnixListener, func()) {
	t.Helper()

	addr := &net.UnixAddr{Name: sockPath, Net: "unixpacket"}
	sock, err := net.ListenUnix("unixpacket", addr)
	if err != nil {
		t.Fatalf("Couldn't set up test socket: %v", err)
	}

	cleanup := func() {
		t.Helper()
		sock.Close()
		if err := unix.Unlink(sockPath); err != nil && !os.IsNotExist(err) {
			t.Fatalf("Unlink(%s): %s", sockPath, err)
		}
	}

	err = os.Chmod(sockPath, 0777)
	if err != nil {
		cleanup()
		t.Fatalf("Unable to set permissions on test socket: %v", err)
	}

	return sock, cleanup
}

// SetupTestListener sets up a Unix Domain Socket in a temp directory to listen
// and receive one connection.
// The server-side connection object is sent through the conn channel when a client
// connects.
// It returns the path to the socket, to allow the client to connect, and a
// cleanup function.
func SetupTestListener(t *testing.T, conn chan *net.UnixConn) (string, func()) {
	t.Helper()
	tmpDir, tmpCleanup := CreateTestDir(t)

	path := filepath.Join(tmpDir, "test.sock")
	sock, socketCleanup := CreateTestSocket(t, path)
	cleanup := func() {
		socketCleanup()
		tmpCleanup()
	}

	go func() {
		newConn, err := sock.AcceptUnix()
		if err != nil {
			t.Logf("Failed to accept connection: %v", err)
			return
		}
		conn <- newConn
	}()

	return path, cleanup
}

// CopyFile copies the src file to dst. Any existing file will be overwritten and
// will not copy file attributes.
func CopyFile(t *testing.T, src, dst string) {
	t.Helper()

	in, err := os.Open(src)
	if err != nil {
		t.Fatal(err)
	}
	defer in.Close()

	out, err := os.Create(dst)
	if err != nil {
		t.Fatal(err)
	}
	defer out.Close()

	_, err = io.Copy(out, in)
	if err != nil {
		t.Fatal(err)
	}
}

// CopyDir will recursively copy src to test. Fails if src does
// not exist or dst already exists.
func CopyDir(t *testing.T, src, dst string) {
	t.Helper()

	if _, err := os.Stat(dst); err == nil {
		t.Fatalf("dst already exists: %s", dst)
	}
	srcStat, err := os.Stat(src)
	if err != nil {
		t.Fatal(err)
	}
	if !srcStat.IsDir() {
		t.Fatalf("%s is not a directory", src)
	}

	if err := os.MkdirAll(dst, srcStat.Mode()); err != nil {
		t.Fatal(err)
	}

	if err := filepath.WalkDir(src, func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			return err
		}

		switch {
		case d.IsDir():
			if path == src || d.Name() == "." || d.Name() == ".." {
				return nil
			}
			CopyDir(t, path, filepath.Join(dst, d.Name()))
			return nil
		case d.Type() == os.ModeSymlink:
			link, err := os.Readlink(path)
			if err != nil {
				return err
			}
			return os.Symlink(link, filepath.Join(dst, d.Name()))
		case d.Type().IsRegular():
			info, err := d.Info()
			if err != nil {
				return err
			}
			destPath := filepath.Join(dst, d.Name())
			CopyFile(t, path, destPath)
			return os.Chmod(destPath, info.Mode())
		default:
			return fmt.Errorf("unsupported file type: %s", d.Type())
		}
	}); err != nil {
		t.Fatal(err)
	}
}

// Context returns a context that is canceled when the test is done.
func Context(t *testing.T) context.Context {
	t.Helper()
	ctx, cancel := context.WithCancel(context.Background())
	t.Cleanup(cancel)
	return ctx
}

// MustLogContext returns a context containing a buffer-backed logger.
// The contents of the buffer will be displayed on test failure. If the
// optional parent context is not supplied, a default context is created.
// Canceled when the test is done.
func MustLogContext(t *testing.T, parents ...context.Context) context.Context {
	t.Helper()

	var parent context.Context
	switch len(parents) {
	case 0:
		parent = Context(t)
	case 1:
		if parents[0] == nil {
			// Pass through a nil context for tests that expect it.
			return nil
		} else {
			var cancel context.CancelFunc
			parent, cancel = context.WithCancel(parents[0])
			t.Cleanup(cancel)
		}
	default:
		t.Fatal("too many parent contexts")
	}

	log, buf := logging.NewTestLogger(t.Name())
	t.Cleanup(func() {
		ShowBufferOnFailure(t, buf)
	})

	ctx, err := logging.ToContext(parent, log)
	if err != nil {
		t.Fatal(err)
	}
	return ctx
}

// JoinArgs creates a new string slice from a base string and optional
// additional string arguments. Does not modify the base string.
func JoinArgs(base []string, args ...string) []string {
	joined := make([]string, len(base))
	copy(joined, base)
	return append(joined, args...)
}
