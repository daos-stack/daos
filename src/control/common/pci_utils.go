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

	"github.com/pkg/errors"
)

// bdevPciAddrSep defines the separator used between PCI addresses in string lists.
const bdevPciAddrSep = " "

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

// NewPCIAddress creates a PCIAddress stuct from input string.
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

// PCIAddressList represents a list of PCI addresses.
type PCIAddressList []*PCIAddress

// Contains returns true if provided address is already in list.
func (pal *PCIAddressList) Contains(a *PCIAddress) bool {
	if pal == nil {
		return false
	}

	for _, addr := range *pal {
		if addr.Equals(a) {
			return true
		}
	}

	return false
}

// Add adds PCI addresses to slice. Ignores duplicate addresses.
func (pal *PCIAddressList) Add(addrs ...PCIAddress) {
	if pal == nil {
		return
	}

	for _, a := range addrs {
		if !pal.Contains(&a) {
			*pal = append(*pal, &a)
		}
	}

	sort.Sort(pal)
}

// AddStrings adds PCI addresses to slice from supplied strings. If any input string is not a valid PCI
// address then return error and don't add any elements to slice. Ignores duplicateaddresses.
func (pal *PCIAddressList) AddStrings(addrs ...string) error {
	if pal == nil {
		return errors.New("method call on nil address list")
	}

	var al []*PCIAddress

	addrs = DedupeStringSlice(addrs)

	for _, addr := range addrs {
		if addr == "" {
			continue
		}

		a, err := NewPCIAddress(addr)
		if err != nil {
			return err
		}

		if !pal.Contains(a) {
			al = append(al, a)
		}
	}
	*pal = append(*pal, al...)

	sort.Sort(pal)

	return nil
}

// Strings returns PCI addresses as slice of strings.
func (pal *PCIAddressList) Strings() []string {
	if pal == nil {
		return nil
	}

	var addrs []string

	for _, addr := range *pal {
		addrs = append(addrs, addr.String())
	}

	return sort.StringSlice(addrs)
}

// Strings returns PCI addresses as string of joined space separated strings.
func (pal *PCIAddressList) String() string {
	if pal == nil {
		return ""
	}

	return strings.Join(pal.Strings(), bdevPciAddrSep)
}

// Copy returns a copy of the PCIAddressList.
func (pal *PCIAddressList) Copy() PCIAddressList {
	if pal == nil {
		return PCIAddressList{}
	}

	cpy := make(PCIAddressList, 0, pal.Len())

	return append(cpy, *pal...)
}

// Len returns length of slice. Required by sort.Interface.
func (pal *PCIAddressList) Len() int {
	if pal == nil {
		return 0
	}

	return len(*pal)
}

func hexStr2Int(s string) int64 {
	i, err := strconv.ParseInt(s, 16, 64)
	if err != nil {
		panic(err)
	}
	return i
}

// Less returns true if elem i is "less" than elem j. Required by sort.Interface.
func (pal *PCIAddressList) Less(i, j int) bool {
	if pal == nil {
		return false
	}

	al := *pal

	fmv := hexStr2Int(al[i].Domain)
	smv := hexStr2Int(al[j].Domain)

	if fmv != smv {
		return fmv < smv
	}

	fbv := hexStr2Int(al[i].Bus)
	sbv := hexStr2Int(al[j].Bus)

	if fbv != sbv {
		return fbv < sbv
	}

	fdv := hexStr2Int(al[i].Device)
	sdv := hexStr2Int(al[j].Device)

	if fdv != sdv {
		return fdv < sdv
	}

	ffv := hexStr2Int(al[i].Function)
	sfv := hexStr2Int(al[j].Function)

	return ffv < sfv
}

// Swap exchanges elements in underlying slice. Required by sort.Interface.
func (pal *PCIAddressList) Swap(i, j int) {
	if pal == nil {
		return
	}

	al := *pal

	al[i], al[j] = al[j], al[i]
}

// IsEmpty returns true if length of slice is zero.
func (pal *PCIAddressList) IsEmpty() bool {
	return pal == nil || pal.Len() == 0
}

// Intersect returns elements in 'this' AND input address lists.
func (pal *PCIAddressList) Intersect(inList *PCIAddressList) *PCIAddressList {
	if pal == nil || inList == nil {
		return nil
	}

	intersection := PCIAddressList{}

	// loop over the smaller set
	if pal.Len() < inList.Len() {
		for _, a := range *pal {
			if inList.Contains(a) {
				intersection.Add(*a)
			}
		}

		return &intersection
	}

	for _, a := range *inList {
		if pal.Contains(a) {
			intersection.Add(*a)
		}
	}

	return &intersection
}

// Difference returns elements in 'this' list but NOT IN input address list.
func (pal *PCIAddressList) Difference(inList *PCIAddressList) *PCIAddressList {
	if pal == nil {
		return nil
	}

	difference := PCIAddressList{}

	for _, a := range *pal {
		if !inList.Contains(a) {
			difference.Add(*a)
		}
	}

	return &difference
}

// NewPCIAddressList takes a variable number of strings and attempts to create an address list.
func NewPCIAddressList(addrs ...string) (*PCIAddressList, error) {
	al := &PCIAddressList{}
	if err := al.AddStrings(addrs...); err != nil {
		return nil, err
	}

	sort.Sort(al)

	return al, nil
}

// NewPCIAddressListFromString takes a space-separated string and attempts to create an address list.
func NewPCIAddressListFromString(addrs string) (*PCIAddressList, error) {
	return NewPCIAddressList(strings.Split(addrs, bdevPciAddrSep)...)
}
