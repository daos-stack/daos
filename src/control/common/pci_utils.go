//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package common

import (
	"fmt"
	"sort"
	"strconv"
	"strings"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/pkg/errors"
)

const (
	// bdevPciAddrSep defines the separator used between PCI addresses in string lists.
	bdevPciAddrSep = " "
	// vmdDomainLen defines the expected length of a VMD backing devices address domain.
	vmdDomainLen = 6
)

// parsePCIAddress returns separated components of BDF format PCI address.
func parsePCIAddress(addr string) (dom, bus, dev, fun uint64, err error) {
	parts := strings.Split(addr, ":")
	devFunc := strings.Split(parts[len(parts)-1], ".")
	if len(parts) != 3 || len(devFunc) != 2 {
		err = errors.Errorf("unexpected pci address bdf format: %q", addr)
		return
	}

	if dom, err = strconv.ParseUint(parts[0], 16, 64); err != nil {
		return
	}
	if bus, err = strconv.ParseUint(parts[1], 16, 32); err != nil {
		return
	}
	if dev, err = strconv.ParseUint(devFunc[0], 16, 32); err != nil {
		return
	}
	if fun, err = strconv.ParseUint(devFunc[1], 16, 32); err != nil {
		return
	}

	return
}

// PCIAddress represents a standard PCI address with domain and BDF.
type PCIAddress struct {
	Domain   string
	Bus      string
	Device   string
	Function string
}

func (pa *PCIAddress) String() string {
	if pa == nil {
		return ""
	}

	return fmt.Sprintf("%s:%s:%s.%s", pa.Domain, pa.Bus, pa.Device, pa.Function)
}

// Equals compares string representation of address.
func (pa *PCIAddress) Equals(o *PCIAddress) bool {
	if pa == nil || o == nil {
		return false
	}

	return o.String() == pa.String()
}

// IsVMDBackingAddress indicates whether PCI address is a VMD backing device.
func (pa *PCIAddress) IsVMDBackingAddress() bool {
	if pa == nil {
		return false
	}

	return pa.Domain != "0000"
}

// BackingToVMDAddress returns the VMD PCI address associated with a VMD backing devices address.
func (pa *PCIAddress) BackingToVMDAddress() (*PCIAddress, error) {
	if pa == nil {
		return nil, errors.New("PCIAddress is nil")
	}
	if !pa.IsVMDBackingAddress() {
		return nil, errors.New("not a vmd backing device address")

	}

	// assume non-zero pci address domain field indicates a vmd backing device wisth
	// fixed length field
	if len(pa.Domain) != vmdDomainLen {
		return nil, errors.New("unexpected length of vmd domain")
	}

	return NewPCIAddress(fmt.Sprintf("0000:%s:%s.%s", pa.Domain[:2], pa.Domain[2:4],
		pa.Domain[5:]))
}

// LessThan evaluate whether "this" address is less than "other" by comparing
// domain/bus/device/function in order.
func (pa *PCIAddress) LessThan(other *PCIAddress) bool {
	if pa == nil || other == nil {
		return false
	}

	if pa.Domain != other.Domain {
		return hexStr2Int(pa.Domain) < hexStr2Int(other.Domain)
	}

	if pa.Bus != other.Bus {
		return hexStr2Int(pa.Bus) < hexStr2Int(other.Bus)
	}

	if pa.Device != other.Device {
		return hexStr2Int(pa.Device) < hexStr2Int(other.Device)
	}

	return hexStr2Int(pa.Function) < hexStr2Int(other.Function)
}

// NewPCIAddress creates a PCIAddress struct from input string.
func NewPCIAddress(addr string) (*PCIAddress, error) {
	dom, bus, dev, fun, err := parsePCIAddress(addr)
	if err != nil {
		return nil, err
	}

	pa := &PCIAddress{
		Bus:      fmt.Sprintf("%02x", bus),
		Device:   fmt.Sprintf("%02x", dev),
		Function: fmt.Sprintf("%01x", fun),
	}

	if dom == 0 {
		pa.Domain = "0000"
	} else {
		pa.Domain = fmt.Sprintf("%x", dom)
	}

	return pa, nil
}

// PCIAddressSet represents a unique list of PCI addresses.
type PCIAddressSet struct {
	addrMap map[string]*PCIAddress
}

// Contains returns true if provided address is already in list.
func (pal *PCIAddressSet) Contains(a *PCIAddress) bool {
	if pal == nil {
		return false
	}

	_, found := pal.addrMap[a.String()]
	return found
}

func (pal *PCIAddressSet) add(a *PCIAddress) {
	if pal.addrMap == nil {
		pal.addrMap = make(map[string]*PCIAddress)
	}

	pal.addrMap[a.String()] = a
}

// Add adds PCI addresses to slice. Ignores duplicate addresses.
func (pal *PCIAddressSet) Add(addrs ...*PCIAddress) error {
	if pal == nil {
		return errors.New("PCIAddressSet is nil")
	}

	for _, addr := range addrs {
		pal.add(addr)
	}

	return nil
}

// AddStrings adds PCI addresses to slice from supplied strings. If any input string is not a valid PCI
// address then return error and don't add any elements to slice. Ignores duplicateaddresses.
func (pal *PCIAddressSet) AddStrings(addrs ...string) error {
	if pal == nil {
		return errors.New("PCIAddressSet is nil")
	}

	for _, addr := range addrs {
		if addr == "" {
			continue
		}

		a, err := NewPCIAddress(addr)
		if err != nil {
			return err
		}

		pal.add(a)
	}

	return nil
}

// Addresses returns sorted slice of PCI address type object references.
func (pal *PCIAddressSet) Addresses() []*PCIAddress {
	if pal == nil {
		return nil
	}

	addrs := make([]*PCIAddress, 0, len(pal.addrMap))
	for _, addr := range pal.addrMap {
		addrs = append(addrs, addr)
	}
	sort.Slice(addrs, func(i, j int) bool { return addrs[i].LessThan(addrs[j]) })

	return addrs
}

// Strings returns PCI addresses as slice of strings.
func (pal *PCIAddressSet) Strings() []string {
	if pal == nil {
		return nil
	}

	addrs := make([]string, len(pal.addrMap))
	for i, addr := range pal.Addresses() {
		addrs[i] = addr.String()
	}

	return addrs
}

// Strings returns PCI addresses as string of joined space separated strings.
func (pal *PCIAddressSet) String() string {
	return strings.Join(pal.Strings(), bdevPciAddrSep)
}

// Len returns length of slice. Required by sort.Interface.
func (pal *PCIAddressSet) Len() int {
	if pal == nil {
		return 0
	}

	return len(pal.addrMap)
}

// IsEmpty returns true if address set is empty.
func (pal *PCIAddressSet) IsEmpty() bool {
	return pal.Len() == 0
}

func hexStr2Int(s string) int64 {
	i, err := strconv.ParseInt(s, 16, 64)
	if err != nil {
		panic(err)
	}
	return i
}

// Intersect returns elements in 'this' AND input address lists.
func (pal *PCIAddressSet) Intersect(in *PCIAddressSet) *PCIAddressSet {
	intersection := &PCIAddressSet{}

	// loop over the smaller set
	if pal.Len() < in.Len() {
		for _, a := range pal.Addresses() {
			if in.Contains(a) {
				intersection.Add(a)
			}
		}

		return intersection
	}

	for _, a := range in.Addresses() {
		if pal.Contains(a) {
			intersection.Add(a)
		}
	}

	return intersection
}

// Difference returns elements in 'this' list but NOT IN input address list.
func (pal *PCIAddressSet) Difference(in *PCIAddressSet) *PCIAddressSet {
	difference := &PCIAddressSet{}

	for _, a := range pal.Addresses() {
		if !in.Contains(a) {
			difference.Add(a)
		}
	}

	return difference
}

// BackingToVMDAddresses converts all VMD backing device PCI addresses (with the VMD address
// encoded in the domain component of the PCI address) in set back to the PCI address of the VMD
// e.g. [5d0505:01:00.0, 5d0505:03:00.0] -> [0000:5d:05.5].
//
// Many assumptions are made as to the input and output PCI address structure in the conversion.
func (pal *PCIAddressSet) BackingToVMDAddresses(log logging.Logger) (*PCIAddressSet, error) {
	if pal == nil {
		return nil, errors.New("PCIAddressSet is nil")
	}

	outAddrs := PCIAddressSet{}

	for _, inAddr := range pal.Addresses() {
		if !inAddr.IsVMDBackingAddress() {
			if err := outAddrs.Add(inAddr); err != nil {
				return nil, err
			}
			continue
		}

		vmdAddr, err := inAddr.BackingToVMDAddress()
		if err != nil {
			return nil, err
		}

		log.Debugf("replacing backing device %s with vmd %s", inAddr, vmdAddr)
		if err := outAddrs.Add(vmdAddr); err != nil {
			return nil, err
		}
	}

	return &outAddrs, nil
}

// NewPCIAddressSet takes a variable number of strings and attempts to create an address set.
func NewPCIAddressSet(addrs ...string) (*PCIAddressSet, error) {
	al := &PCIAddressSet{}
	if err := al.AddStrings(addrs...); err != nil {
		return nil, err
	}

	return al, nil
}

// NewPCIAddressSetFromString takes a space-separated string and attempts to create an address set.
func NewPCIAddressSetFromString(addrs string) (*PCIAddressSet, error) {
	return NewPCIAddressSet(strings.Split(addrs, bdevPciAddrSep)...)
}
