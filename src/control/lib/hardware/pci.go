//
// (C) Copyright 2021-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package hardware

import (
	"encoding/json"
	"fmt"
	"math"
	"sort"
	"strconv"
	"strings"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"
)

const (
	// bdevPciAddrSep defines the separator used between PCI addresses in string sets.
	bdevPciAddrSep = " "
	// PCIAddrBusBitSize defines the number of bits used to represent bus in address.
	PCIAddrBusBitSize = 8
)

var ErrNotVMDBackingAddress = errors.New("not a vmd backing device address")

// parseVMDAddress returns the domain string interpreted as the VMD address.
func parseVMDAddress(addr string) (*PCIAddress, error) {
	// Left-pad domain string as necessary make it a valid PCI address.
	addr = fmt.Sprintf("%06s", addr)
	return NewPCIAddress(fmt.Sprintf("0000:%s:%s.%s", addr[:2], addr[2:4], addr[4:]))
}

// parsePCIAddress returns separated components of BDF format PCI address.
func parsePCIAddress(addr string) (dom uint16, bus, dev, fun uint8, vmdAddr *PCIAddress, err error) {
	parts := strings.Split(addr, ":")
	devFunc := strings.Split(parts[len(parts)-1], ".")
	if len(parts) != 3 || len(devFunc) != 2 {
		err = errors.Errorf("unexpected pci address bdf format: %q", addr)
		return
	}

	var val uint64
	if val, err = strconv.ParseUint(parts[0], 16, 64); err != nil {
		return
	}
	if val > math.MaxUint16 {
		vmdAddr, err = parseVMDAddress(parts[0])
		if err != nil {
			return
		}
		val = math.MaxUint16
	}
	dom = uint16(val)
	if val, err = strconv.ParseUint(parts[1], 16, 8); err != nil {
		return
	}
	bus = uint8(val)
	if val, err = strconv.ParseUint(devFunc[0], 16, 8); err != nil {
		return
	}
	dev = uint8(val)
	if val, err = strconv.ParseUint(devFunc[1], 16, 8); err != nil {
		return
	}
	fun = uint8(val)

	return
}

// PCIAddress represents the address of a standard PCI device
// or a VMD backing device.
type PCIAddress struct {
	VMDAddr  *PCIAddress `json:"vmd_address,omitempty"`
	Domain   uint16      `json:"domain"`
	Bus      uint8       `json:"bus"`
	Device   uint8       `json:"device"`
	Function uint8       `json:"function"`
}

func (pa *PCIAddress) FieldStrings() map[string]string {
	var domStr string
	if pa.VMDAddr != nil {
		domStr = fmt.Sprintf("%02x%02x%02x", pa.VMDAddr.Bus, pa.VMDAddr.Device, pa.VMDAddr.Function)
	} else {
		domStr = fmt.Sprintf("%04x", pa.Domain)
	}

	return map[string]string{
		"Domain":   domStr,
		"Bus":      fmt.Sprintf("%02x", pa.Bus),
		"Device":   fmt.Sprintf("%02x", pa.Device),
		"Function": fmt.Sprintf("%x", pa.Function),
	}
}

// IsZero returns true if address was uninitialized.
func (pa *PCIAddress) IsZero() bool {
	if pa == nil {
		return false
	}
	return pa.Equals(&PCIAddress{})
}

func (pa *PCIAddress) String() string {
	if pa == nil {
		return ""
	}

	fs := pa.FieldStrings()
	return fmt.Sprintf("%s:%s:%s.%s", fs["Domain"], fs["Bus"], fs["Device"], fs["Function"])
}

// Equals compares two PCIAddress structs for equality.
func (pa *PCIAddress) Equals(other *PCIAddress) bool {
	if pa == nil || other == nil {
		return false
	}

	if pa.VMDAddr != nil || other.VMDAddr != nil {
		if !pa.VMDAddr.Equals(other.VMDAddr) {
			return false
		}
	}

	return pa.Domain == other.Domain &&
		pa.Bus == other.Bus &&
		pa.Device == other.Device &&
		pa.Function == other.Function
}

// LessThan evaluate whether "this" address is less than "other" by comparing
// domain/bus/device/function in order.
func (pa *PCIAddress) LessThan(other *PCIAddress) bool {
	if pa == nil || other == nil {
		return false
	}

	// If VMD backing device address, return early on domain comparison if VMD domains are not
	// equal. If equal, proceed to sort on backing device address BDF.
	if pa.VMDAddr != nil && other.VMDAddr != nil {
		if !pa.VMDAddr.Equals(other.VMDAddr) {
			return pa.VMDAddr.LessThan(other.VMDAddr)
		}
	}

	return pa.Domain < other.Domain ||
		pa.Domain == other.Domain && pa.Bus < other.Bus ||
		pa.Domain == other.Domain && pa.Bus == other.Bus && pa.Device < other.Device ||
		pa.Domain == other.Domain && pa.Bus == other.Bus && pa.Device == other.Device &&
			pa.Function < other.Function
}

// IsVMDBackingAddress indicates whether PCI address is a VMD backing device.
func (pa *PCIAddress) IsVMDBackingAddress() bool {
	if pa == nil || pa.VMDAddr == nil {
		return false
	}

	return true
}

// BackingToVMDAddress returns the VMD PCI address associated with a VMD backing devices address.
func (pa *PCIAddress) BackingToVMDAddress() (*PCIAddress, error) {
	if pa == nil {
		return nil, errors.New("PCIAddress is nil")
	}
	if !pa.IsVMDBackingAddress() {
		return nil, ErrNotVMDBackingAddress
	}

	return pa.VMDAddr, nil
}

// NewPCIAddress creates a PCIAddress struct from input string.
func NewPCIAddress(addr string) (*PCIAddress, error) {
	dom, bus, dev, fun, vmd, err := parsePCIAddress(addr)
	if err != nil {
		return nil, errors.Wrapf(err, "unable to parse %q", addr)
	}

	return &PCIAddress{
		VMDAddr:  vmd,
		Domain:   dom,
		Bus:      bus,
		Device:   dev,
		Function: fun,
	}, nil
}

// MustNewPCIAddress creates a new PCIAddress from input string,
// which must be valid, otherwise a panic will occur.
func MustNewPCIAddress(addr string) *PCIAddress {
	pa, err := NewPCIAddress(addr)
	if err != nil {
		panic(err)
	}

	return pa
}

// PCIAddressSet represents a unique set of PCI addresses.
type PCIAddressSet struct {
	addrMap map[string]*PCIAddress
}

// Equals compares two PCIAddressSets for equality.
func (pas *PCIAddressSet) Equals(other *PCIAddressSet) bool {
	return pas.Difference(other).Len() == 0
}

// Contains returns true if provided address is already in set.
func (pas *PCIAddressSet) Contains(a *PCIAddress) bool {
	if pas == nil || a == nil {
		return false
	}

	_, found := pas.addrMap[a.String()]
	return found
}

func (pas *PCIAddressSet) add(a *PCIAddress) {
	if a == nil {
		return
	}

	if pas.addrMap == nil {
		pas.addrMap = make(map[string]*PCIAddress)
	}

	pas.addrMap[a.String()] = a
}

// Add adds PCI addresses to set. Ignores duplicate addresses.
func (pas *PCIAddressSet) Add(addrs ...*PCIAddress) error {
	if pas == nil {
		return errors.New("PCIAddressSet is nil")
	}

	for _, addr := range addrs {
		pas.add(addr)
	}

	return nil
}

// AddStrings adds PCI addresses to set from supplied strings. If any input string is not a valid PCI
// address then return error and don't add any elements to set.
func (pas *PCIAddressSet) AddStrings(addrs ...string) error {
	if pas == nil {
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

		pas.add(a)
	}

	return nil
}

// Addresses returns sorted slice of PCI address type object references.
func (pas *PCIAddressSet) Addresses() []*PCIAddress {
	if pas == nil {
		return nil
	}

	addrs := make([]*PCIAddress, 0, len(pas.addrMap))
	for _, addr := range pas.addrMap {
		addrs = append(addrs, addr)
	}
	sort.Slice(addrs, func(i, j int) bool { return addrs[i].LessThan(addrs[j]) })

	return addrs
}

// Strings returns PCI addresses as slice of strings.
func (pas *PCIAddressSet) Strings() []string {
	if pas == nil {
		return nil
	}

	addrs := make([]string, len(pas.addrMap))
	for i, addr := range pas.Addresses() {
		addrs[i] = addr.String()
	}

	return addrs
}

// Strings returns PCI addresses as string of joined space separated strings.
func (pas *PCIAddressSet) String() string {
	if pas == nil {
		return ""
	}

	return strings.Join(pas.Strings(), bdevPciAddrSep)
}

// Len returns length of set. Required by sort.Interface.
func (pas *PCIAddressSet) Len() int {
	if pas == nil {
		return 0
	}

	return len(pas.addrMap)
}

// IsEmpty returns true if address set is empty.
func (pas *PCIAddressSet) IsEmpty() bool {
	return pas.Len() == 0
}

// Intersect returns elements in 'this' AND input address sets.
func (pas *PCIAddressSet) Intersect(in *PCIAddressSet) *PCIAddressSet {
	intersection := &PCIAddressSet{}

	// loop over the smaller set
	if pas.Len() < in.Len() {
		for _, a := range pas.Addresses() {
			if in.Contains(a) {
				intersection.Add(a)
			}
		}

		return intersection
	}

	for _, a := range in.Addresses() {
		if pas.Contains(a) {
			intersection.Add(a)
		}
	}

	return intersection
}

// Difference returns elements in 'this' set but NOT IN input address set.
func (pas *PCIAddressSet) Difference(in *PCIAddressSet) *PCIAddressSet {
	difference := &PCIAddressSet{}

	for _, a := range pas.Addresses() {
		if !in.Contains(a) {
			difference.Add(a)
		}
	}

	return difference
}

// HasVMD returns true if any of the addresses in set is for a VMD backing device.
func (pas *PCIAddressSet) HasVMD() bool {
	if pas == nil {
		return false
	}

	for _, inAddr := range pas.Addresses() {
		if inAddr.IsVMDBackingAddress() {
			return true
		}
	}

	return false
}

// BackingToVMDAddresses converts all VMD backing device PCI addresses (with the VMD address
// encoded in the domain component of the PCI address) in set back to the PCI address of the VMD
// e.g. [5d0505:01:00.0, 5d0505:03:00.0] -> [0000:5d:05.5].
//
// Many assumptions are made as to the input and output PCI address structure in the conversion.
func (pas *PCIAddressSet) BackingToVMDAddresses() (*PCIAddressSet, error) {
	if pas == nil {
		return nil, errors.New("PCIAddressSet is nil")
	}

	outAddrs := PCIAddressSet{}

	for _, inAddr := range pas.Addresses() {
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

		if err := outAddrs.Add(vmdAddr); err != nil {
			return nil, err
		}
	}

	return &outAddrs, nil
}

// NewPCIAddressSet takes a variable number of strings and attempts to create an address set.
func NewPCIAddressSet(addrs ...string) (*PCIAddressSet, error) {
	as := &PCIAddressSet{}
	if err := as.AddStrings(addrs...); err != nil {
		return nil, err
	}

	return as, nil
}

// MustNewPCIAddressSet creates a new PCIAddressSet from input strings,
// which must be valid, otherwise a panic will occur.
func MustNewPCIAddressSet(addrs ...string) *PCIAddressSet {
	as, err := NewPCIAddressSet(addrs...)
	if err != nil {
		panic(err)
	}

	return as
}

// NewPCIAddressSetFromString takes a space-separated string and attempts to create an address set.
func NewPCIAddressSetFromString(addrs string) (*PCIAddressSet, error) {
	return NewPCIAddressSet(strings.Split(addrs, bdevPciAddrSep)...)
}

type (
	// PCIDevice represents an individual hardware device.
	PCIDevice struct {
		Name        string       `json:"name"`
		Type        DeviceType   `json:"type"`
		NUMANode    *NUMANode    `json:"-"`
		Bus         *PCIBus      `json:"-"`
		PCIAddr     PCIAddress   `json:"pci_address"`
		LinkSpeed   float64      `json:"link_speed,omitempty"`
		BlockDevice *BlockDevice `json:"-"`
	}

	// PCIBus represents the root of a PCI bus hierarchy.
	PCIBus struct {
		LowAddress  PCIAddress `json:"low_address"`
		HighAddress PCIAddress `json:"high_address"`
		NUMANode    *NUMANode  `json:"-"`
		PCIDevices  PCIDevices `json:"pci_devices"`
	}

	// PCIDevices groups hardware devices by PCI address.
	PCIDevices map[PCIAddress][]*PCIDevice
)

// NewPCIBus creates a new PCI bus.
func NewPCIBus(domain uint16, lo, hi uint8) *PCIBus {
	return &PCIBus{
		LowAddress:  PCIAddress{Domain: domain, Bus: lo},
		HighAddress: PCIAddress{Domain: domain, Bus: hi},
	}
}

// AddDevice adds a PCI device to the bus.
func (b *PCIBus) AddDevice(dev *PCIDevice) error {
	if b == nil || dev == nil {
		return errors.New("bus or device is nil")
	}
	if !b.Contains(&dev.PCIAddr) {
		return fmt.Errorf("device %s is not on bus %s", &dev.PCIAddr, b)
	}
	if b.PCIDevices == nil {
		b.PCIDevices = make(PCIDevices)
	}

	dev.Bus = b
	b.PCIDevices[dev.PCIAddr] = append(b.PCIDevices[dev.PCIAddr], dev)

	return nil
}

// Contains returns true if the given PCI address is contained within the bus.
func (b *PCIBus) Contains(addr *PCIAddress) bool {
	if b == nil || addr == nil {
		return false
	}

	return b.LowAddress.Domain == addr.Domain &&
		b.LowAddress.Bus <= addr.Bus &&
		addr.Bus <= b.HighAddress.Bus
}

func (b *PCIBus) String() string {
	laf := b.LowAddress.FieldStrings()
	if b.LowAddress.Bus == b.HighAddress.Bus {
		return fmt.Sprintf("%s:%s", laf["Domain"], laf["Bus"])
	}
	haf := b.HighAddress.FieldStrings()
	return fmt.Sprintf("%s:[%s-%s]", laf["Domain"], laf["Bus"], haf["Bus"])
}

// IsZero if PCI bus contains no valid addresses.
func (b *PCIBus) IsZero() bool {
	if b == nil {
		return true
	}

	return b.LowAddress.IsZero() && b.HighAddress.IsZero()
}

func (d *PCIDevice) String() string {
	var speedStr string
	if d.LinkSpeed > 0 {
		speedStr = fmt.Sprintf(" @ %.2f GB/s", d.LinkSpeed)
	}
	var sizeStr string
	if d.BlockDevice != nil {
		sizeStr = fmt.Sprintf("%s ", humanize.Bytes(d.BlockDevice.Size))
	}
	return fmt.Sprintf("%s %s (%s%s)%s", &d.PCIAddr, d.Name, sizeStr, d.Type, speedStr)
}

// DeviceName returns the system name of the PCI device.
func (d *PCIDevice) DeviceName() string {
	if d == nil {
		return ""
	}
	return d.Name
}

// DeviceType returns the type of PCI device.
func (d *PCIDevice) DeviceType() DeviceType {
	if d == nil {
		return DeviceTypeUnknown
	}
	return d.Type
}

// PCIDevice returns a pointer to itself.
func (d *PCIDevice) PCIDevice() *PCIDevice {
	return d
}

func (d PCIDevices) MarshalJSON() ([]byte, error) {
	strMap := make(map[string][]*PCIDevice)
	for k, v := range d {
		strMap[k.String()] = v
	}
	return json.Marshal(strMap)
}

// Add adds a device to the PCIDevices.
func (d PCIDevices) Add(dev *PCIDevice) error {
	if d == nil {
		return errors.New("nil PCIDevices map")
	}
	if dev == nil {
		return errors.New("nil PCIDevice")
	}
	addr := dev.PCIAddr
	d[addr] = append(d[addr], dev)
	return nil
}

// Keys fetches the sorted keys for the map.
func (d PCIDevices) Keys() []*PCIAddress {
	set := new(PCIAddressSet)
	for k := range d {
		ref := k
		if err := set.Add(&ref); err != nil {
			panic(err)
		}
	}
	return set.Addresses()
}

// Get returns the devices for the given PCI address.
func (d PCIDevices) Get(addr *PCIAddress) []*PCIDevice {
	if d == nil || addr == nil {
		return nil
	}

	if devs, found := d[*addr]; found {
		return devs
	}

	return nil
}
