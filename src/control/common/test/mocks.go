//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package test

import (
	"fmt"
	"math"
	"net"
	"os"
	"strings"
	"testing"
)

// SkipWithoutMockLibrary skips the test if the mock library is not available.
func SkipWithoutMockLibrary(t *testing.T, library string) {
	if !strings.Contains(os.Getenv("LD_PRELOAD"), library) {
		t.Skipf("skipping test because LD_PRELOAD does not contain %s", library)
	}
}

var hostAddrs = make(map[int32]*net.TCPAddr)

// MockListPoolsResult mocks list pool results.
type MockListPoolsResult struct {
	Status int32
	Err    error
}

// GetIndex return suitable index value for auto generating mocks.
func GetIndex(varIdx ...int32) int32 {
	if len(varIdx) == 0 {
		varIdx = append(varIdx, 1)
	}

	return varIdx[0]
}

// MockUUID returns mock UUID values for use in tests.
func MockUUID(varIdx ...int32) string {
	idx := GetIndex(varIdx...)

	return fmt.Sprintf("%08d-%04d-%04d-%04d-%012d", idx, idx, idx, idx, idx)
}

// MockHostAddr returns mock tcp addresses for use in tests.
func MockHostAddr(varIdx ...int32) *net.TCPAddr {
	idx := GetIndex(varIdx...)

	if _, exists := hostAddrs[idx]; !exists {
		addr, err := net.ResolveTCPAddr("tcp", fmt.Sprintf("10.0.0.%d:10001", idx))
		if err != nil {
			panic(err)
		}
		hostAddrs[idx] = addr
	}

	return hostAddrs[idx]
}

// MockPCIAddr returns mock PCIAddr values for use in tests.
func MockPCIAddr(varIdx ...int32) string {
	idx := GetIndex(varIdx...)

	return fmt.Sprintf("0000:%02d:00.0", idx)
}

// MockPCIAddrs returns slice of mock PCIAddr values for use in tests.
func MockPCIAddrs(idxs ...int) (addrs []string) {
	for _, i := range idxs {
		addrs = append(addrs, MockPCIAddr(int32(i)))
	}

	return
}

// MockVMDPCIAddr returns mock PCIAddr values for use in tests.
// VMD PCI address domains start at 0x10000 to avoid overlapping with standard ACPI addresses.
func MockVMDPCIAddr(dom int32, varIdx ...int32) string {
	idx := GetIndex(varIdx...)

	return fmt.Sprintf("%06x:%02x:00.0", (math.MaxUint16+1)*dom, idx)
}

// MockVMDPCIAddrs returns slice of mock VMD PCIAddr values for use in tests.
func MockVMDPCIAddrs(dom int, idxs ...int) (addrs []string) {
	for _, i := range idxs {
		addrs = append(addrs, MockVMDPCIAddr(int32(dom), int32(i)))
	}

	return
}

// MockWriter is a mock io.Writer that can be used to inject errors and check
// values written.
type MockWriter struct {
	builder  strings.Builder
	WriteErr error
}

func (w *MockWriter) Write(p []byte) (int, error) {
	if w.WriteErr != nil {
		return 0, w.WriteErr
	}
	return w.builder.Write(p)
}

// GetWritten gets the string value written using Write.
func (w *MockWriter) GetWritten() string {
	return w.builder.String()
}
