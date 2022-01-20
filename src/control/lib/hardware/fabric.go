//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package hardware

import (
	"context"
	"fmt"
	"math"
	"sort"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
)

// FabricInterface represents basic information about a fabric interface.
type FabricInterface struct {
	// Name is the fabric device name.
	Name string `json:"name"`
	// OSDevice is the corresponding OS-level network interface device.
	OSDevice string `json:"os_device"`
	// Providers is the set of supported providers.
	Providers common.StringSet `json:"providers"`
	// DeviceClass is the type of the network interface.
	DeviceClass NetDevClass `json:"device_class"`
	// NUMANode is the NUMA affinity of the network interface.
	NUMANode uint `json:"numa_node"`
}

func (fi *FabricInterface) String() string {
	if fi == nil {
		return "<nil>"
	}

	name := "<no name>"
	if fi.Name != "" {
		name = fi.Name
	}

	var osName string
	if fi.OSDevice != "" {
		osName = fmt.Sprintf(" (interface: %s)", fi.OSDevice)
	}

	providers := "none"
	if len(fi.Providers) > 0 {
		providers = fi.Providers.String()
	}

	return fmt.Sprintf("%s%s (providers: %s)", name, osName, providers)
}

type fabricInterfaceMap map[string]*FabricInterface

func (s fabricInterfaceMap) keys() []string {
	keys := make([]string, 0, len(s))
	for str := range s {
		keys = append(keys, str)
	}
	sort.Strings(keys)
	return keys
}

func (s fabricInterfaceMap) update(name string, fi *FabricInterface) {
	if fi == nil || name == "" {
		return
	}

	if cur, found := s[name]; found {
		// once these values are set to something nonzero, they are immutable
		if cur.OSDevice == "" {
			cur.OSDevice = fi.OSDevice
		}
		if cur.NUMANode == 0 {
			cur.NUMANode = fi.NUMANode
		}
		if cur.DeviceClass == NetDevClass(0) {
			cur.DeviceClass = fi.DeviceClass
		}

		// always possible to add to providers
		if fi.Providers != nil {
			if cur.Providers == nil {
				cur.Providers = common.NewStringSet()
			}
			cur.Providers.Add(fi.Providers.ToSlice()...)
		}
		return
	}

	s[name] = fi
}

type osDevMap map[string]fabricInterfaceMap

func (m osDevMap) keys() []string {
	keys := make([]string, 0, len(m))
	for str := range m {
		keys = append(keys, str)
	}
	sort.Strings(keys)
	return keys
}

func (m osDevMap) update(name string, fi *FabricInterface) {
	if fi == nil || name == "" {
		return
	}

	if _, exists := m[name]; !exists {
		m[name] = make(fabricInterfaceMap)
	}
	m[name].update(fi.Name, fi)
}

func (m osDevMap) remove(fi *FabricInterface) {
	if fi == nil || fi.OSDevice == "" {
		return
	}

	if devices, exists := m[fi.OSDevice]; exists {
		delete(devices, fi.Name)
		if len(devices) == 0 {
			delete(m, fi.OSDevice)
		}
	}
}

// NewFabricInterfaceSet creates a new fabric interface set and initializes it with the passed-in
// FabricInterfaces if provided.
func NewFabricInterfaceSet(fis ...*FabricInterface) *FabricInterfaceSet {
	result := &FabricInterfaceSet{
		byName:  make(fabricInterfaceMap),
		byOSDev: make(osDevMap),
	}

	for _, fi := range fis {
		result.Update(fi)
	}

	return result
}

// FabricInterfaceSet is a set of fabric interfaces.
type FabricInterfaceSet struct {
	byName  fabricInterfaceMap
	byOSDev osDevMap
}

func (s *FabricInterfaceSet) String() string {
	var b strings.Builder
	for _, name := range s.Names() {
		fi, err := s.GetInterface(name)
		if err != nil {
			continue
		}
		b.WriteString(fi.String())
		b.WriteRune('\n')
	}
	return b.String()
}

// NumFabricInterfaces is the total number of FabricInterfaces in the set.
func (s *FabricInterfaceSet) NumFabricInterfaces() int {
	if s == nil {
		return 0
	}
	return len(s.byName)
}

// Names provides a sorted list of the fabric interface names.
func (s *FabricInterfaceSet) Names() []string {
	if s == nil {
		return []string{}
	}
	return s.byName.keys()
}

// NumOSDevices is the number of unique OS-level network devices in the set.
func (s *FabricInterfaceSet) NumOSDevices() int {
	if s == nil {
		return 0
	}
	return len(s.byOSDev)
}

// OSDevices provides a sorted list of the OS-level network devices.
func (s *FabricInterfaceSet) OSDevices() []string {
	if s == nil {
		return []string{}
	}
	return s.byOSDev.keys()
}

// Update updates the details of the fabric interface if it's already in the set, or adds it to the
// set otherwise.
func (s *FabricInterfaceSet) Update(fi *FabricInterface) {
	if s == nil || fi == nil {
		return
	}

	name := fi.Name
	s.byName.update(name, fi)

	osDev := fi.OSDevice
	if osDev == "" {
		return
	}

	s.byOSDev.update(osDev, fi)
}

// Remove deletes a FabricInterface from the set.
func (s *FabricInterfaceSet) Remove(fiName string) {
	fi, err := s.GetInterface(fiName)
	if err != nil {
		// Not in the set
		return
	}

	s.byOSDev.remove(fi)
	delete(s.byName, fiName)
}

// GetInterface fetches a fabric interface by its fabric device name.
func (s *FabricInterfaceSet) GetInterface(name string) (*FabricInterface, error) {
	if s == nil {
		return nil, errors.New("nil FabricInterfaceSet")
	}

	if name == "" {
		return nil, errors.New("interface name is required")
	}

	fi, exists := s.byName[name]
	if !exists {
		return nil, errors.Errorf("interface %q not found", name)
	}

	return fi, nil
}

// GetInterfaceOnOSDevice searches for an interface with a given OS-level device name that
// supports a given provider.
func (s *FabricInterfaceSet) GetInterfaceOnOSDevice(osDev string, provider string) (*FabricInterface, error) {
	if s == nil {
		return nil, errors.New("nil FabricInterfaceSet")
	}

	if osDev == "" {
		return nil, errors.New("OS device name is required")
	}

	if provider == "" {
		return nil, errors.New("fabric provider is required")
	}

	fis, exists := s.byOSDev[osDev]
	if !exists {
		return nil, errors.Errorf("OS device %q not found", osDev)
	}

	for _, fi := range fis {
		if fi.Providers.Has(provider) {
			return fi, nil
		}
	}

	return nil, errors.Errorf("fabric provider %q not supported on OS device %q", provider, osDev)
}

// NetDevClass represents the class of network device.
// ARP protocol hardware identifiers: https://elixir.free-electrons.com/linux/v4.0/source/include/uapi/linux/if_arp.h#L29
type NetDevClass uint32

const (
	Netrom     NetDevClass = 0
	Ether      NetDevClass = 1
	Eether     NetDevClass = 2
	Ax25       NetDevClass = 3
	Pronet     NetDevClass = 4
	Chaos      NetDevClass = 5
	IEEE802    NetDevClass = 6
	Arcnet     NetDevClass = 7
	Appletlk   NetDevClass = 8
	Dlci       NetDevClass = 15
	Atm        NetDevClass = 19
	Metricom   NetDevClass = 23
	IEEE1394   NetDevClass = 24
	Eui64      NetDevClass = 27
	Infiniband NetDevClass = 32
	Loopback   NetDevClass = 772

	// NetDevAny matches any network device class
	NetDevAny NetDevClass = math.MaxUint32
)

func (c NetDevClass) String() string {
	switch c {
	case Netrom:
		return "NETROM"
	case Ether:
		return "ETHER"
	case Eether:
		return "EETHER"
	case Ax25:
		return "AX25"
	case Pronet:
		return "PRONET"
	case Chaos:
		return "CHAOS"
	case IEEE802:
		return "IEEE802"
	case Arcnet:
		return "ARCNET"
	case Appletlk:
		return "APPLETLK"
	case Dlci:
		return "DLCI"
	case Atm:
		return "ATM"
	case Metricom:
		return "METRICOM"
	case IEEE1394:
		return "IEEE1394"
	case Eui64:
		return "EUI64"
	case Infiniband:
		return "INFINIBAND"
	case Loopback:
		return "LOOPBACK"
	case NetDevAny:
		return "ANY"
	default:
		return fmt.Sprintf("UNKNOWN (0x%x)", uint32(c))
	}
}

// FabricInterfaceProvider is an interface that returns a new set of fabric interfaces.
type FabricInterfaceProvider interface {
	GetFabricInterfaces(ctx context.Context) (*FabricInterfaceSet, error)
}

// NetDevClassProvider is an interface that returns the NetDevClass associated with a device.
type NetDevClassProvider interface {
	GetNetDevClass(string) (NetDevClass, error)
}

// FabricInterfaceSetBuilder is an interface used by builders that construct a set of fabric
// interfaces.
type FabricInterfaceSetBuilder interface {
	BuildPart(context.Context, *FabricInterfaceSet) error
}

// FabricInterfaceBuilder is a builder that adds new FabricInterface objects to the FabricInterfaceSet.
type FabricInterfaceBuilder struct {
	log         logging.Logger
	fiProviders []FabricInterfaceProvider
}

// BuildPart collects FabricInterfaces from its providers and adds them to the FabricInterfaceSet.
func (f *FabricInterfaceBuilder) BuildPart(ctx context.Context, fis *FabricInterfaceSet) error {
	if f == nil {
		return errors.New("FabricInterfaceBuilder is nil")
	}

	if fis == nil {
		return errors.New("FabricInterfaceSet is nil")
	}

	if len(f.fiProviders) == 0 {
		return errors.New("FabricInterfaceBuilder is uninitialized")
	}

	fiSets := make([]*FabricInterfaceSet, 0)
	for _, fiProv := range f.fiProviders {
		set, err := fiProv.GetFabricInterfaces(ctx)
		if err != nil {
			return err
		}

		fiSets = append(fiSets, set)
	}

	for _, set := range fiSets {
		for _, name := range set.Names() {
			fi, err := set.GetInterface(name)
			if err != nil {
				f.log.Errorf("can't update interface %s: %s", name, err.Error())
				continue
			}
			fis.Update(fi)
		}
	}

	return nil
}

func newFabricInterfaceBuilder(log logging.Logger, fiProviders ...FabricInterfaceProvider) *FabricInterfaceBuilder {
	return &FabricInterfaceBuilder{
		log:         log,
		fiProviders: fiProviders,
	}
}

// OSDeviceBuilder is a builder that updates FabricInterfaces with an OSDevice.
type OSDeviceBuilder struct {
	log  logging.Logger
	topo *Topology
}

// BuildPart updates existing FabricInterface structures in the FabricInterfaceSet to include an
// OS-level device name, if available.
func (o *OSDeviceBuilder) BuildPart(ctx context.Context, fis *FabricInterfaceSet) error {
	if o == nil {
		return errors.New("OSDeviceBuilder is nil")
	}

	if fis == nil {
		return errors.New("FabricInterfaceSet is nil")
	}

	if o.topo == nil {
		return errors.New("OSDeviceBuilder is uninitialized")
	}

	devsByName := o.topo.AllDevices()

	for _, name := range fis.Names() {
		fi, err := fis.GetInterface(name)
		if err != nil {
			o.log.Errorf("can't update interface %s: %s", name, err.Error())
			continue
		}

		if fi.DeviceClass == Loopback || fi.Name == "lo" {
			// Loopback is not a hardware device
			fi.OSDevice = name
			fis.Update(fi)
			continue
		}

		dev, exists := devsByName[name]
		if !exists {
			o.log.Debugf("ignoring fabric interface %q not found in topology", name)
			fis.Remove(name)
			continue
		}

		if dev.Type == DeviceTypeNetInterface {
			fi.OSDevice = name
			fis.Update(fi)
			continue
		}

		siblings := o.topo.NUMANodes[dev.NUMANode.ID].PCIDevices[dev.PCIAddr]
		for _, sib := range siblings {
			if sib.Type == DeviceTypeNetInterface {
				fi.OSDevice = sib.Name
				fis.Update(fi)
				break
			}
		}

		if fi.OSDevice == "" {
			o.log.Errorf("no OS device sibling found for fabric %q", name)
		}
	}

	return nil
}

func newOSDeviceBuilder(log logging.Logger, topo *Topology) *OSDeviceBuilder {
	return &OSDeviceBuilder{
		log:  log,
		topo: topo,
	}
}

// NUMAAffinityBuilder is a builder that updates FabricInterfaces with a NUMA node affinity.
type NUMAAffinityBuilder struct {
	log  logging.Logger
	topo *Topology
}

// BuildPart updates existing FabricInterface structures in the setto include a
// NUMA node affinity, if available.
func (n *NUMAAffinityBuilder) BuildPart(ctx context.Context, fis *FabricInterfaceSet) error {
	if n == nil {
		return errors.New("NUMAAffinityBuilder is nil")
	}

	if fis == nil {
		return errors.New("FabricInterfaceSet is nil")
	}

	if n.topo == nil {
		return errors.New("NUMAAffinityBuilder is uninitialized")
	}

	devsByName := n.topo.AllDevices()

	for _, name := range fis.Names() {
		fi, err := fis.GetInterface(name)
		if err != nil {
			n.log.Errorf("can't update interface %s: %s", name, err.Error())
			continue
		}

		if fi.DeviceClass == Loopback {
			// Loopback is not a physical device
			continue
		}

		dev, exists := devsByName[name]
		if !exists {
			n.log.Debugf("fabric interface %q not found in topology", name)
			continue
		}

		fi.NUMANode = dev.NUMANode.ID
	}
	return nil
}

func newNUMAAffinityBuilder(log logging.Logger, topo *Topology) *NUMAAffinityBuilder {
	return &NUMAAffinityBuilder{
		log:  log,
		topo: topo,
	}
}

// NetDevClassBuilder is a builder updates FabricInterfaces with a NetDevClass.
type NetDevClassBuilder struct {
	log      logging.Logger
	provider NetDevClassProvider
}

// BuildPart updates existing FabricInterface structures in the FabricInterfaceSet to include a
// net device class (i.e. device type), if available.
func (n *NetDevClassBuilder) BuildPart(ctx context.Context, fis *FabricInterfaceSet) error {
	if n == nil {
		return errors.New("NetDevClassBuilder is nil")
	}

	if fis == nil {
		return errors.New("FabricInterfaceSet is nil")
	}

	if n.provider == nil {
		return errors.New("NetDevClassBuilder is uninitialized")
	}

	for _, name := range fis.Names() {
		fi, err := fis.GetInterface(name)
		if err != nil {
			return err
		}

		if fi.OSDevice == "" {
			n.log.Debugf("fabric interface %q has no corresponding OS-level device", name)
			continue
		}

		ndc, err := n.provider.GetNetDevClass(fi.OSDevice)
		if err != nil {
			n.log.Debugf("failed to get device class for %q: %s", name, err.Error())
		}

		fi.DeviceClass = ndc
	}
	return nil
}

func newNetDevClassBuilder(log logging.Logger, provider NetDevClassProvider) *NetDevClassBuilder {
	return &NetDevClassBuilder{
		log:      log,
		provider: provider,
	}
}

// FabricInterfaceSetBuilderConfig contains the configuration used by FabricInterfaceSetBuilders.
type FabricInterfaceSetBuilderConfig struct {
	Topology                 *Topology
	FabricInterfaceProviders []FabricInterfaceProvider
	NetDevClassProvider      NetDevClassProvider
}

func defaultFabricInterfaceSetBuilders(log logging.Logger, config *FabricInterfaceSetBuilderConfig) []FabricInterfaceSetBuilder {
	return []FabricInterfaceSetBuilder{
		newFabricInterfaceBuilder(log, config.FabricInterfaceProviders...),
		newOSDeviceBuilder(log, config.Topology),
		newNetDevClassBuilder(log, config.NetDevClassProvider),
		newNUMAAffinityBuilder(log, config.Topology),
	}
}

// FabricScannerConfig contains the parameters required to set up a FabricScanner.
type FabricScannerConfig struct {
	TopologyProvider         TopologyProvider
	FabricInterfaceProviders []FabricInterfaceProvider
	NetDevClassProvider      NetDevClassProvider
}

// Validate checks if the FabricScannerConfig is valid.
func (c *FabricScannerConfig) Validate() error {
	if c == nil {
		return errors.New("FabricScannerConfig is nil")
	}

	if c.TopologyProvider == nil {
		return errors.New("TopologyProvider is required")
	}

	if len(c.FabricInterfaceProviders) == 0 {
		return errors.New("at least one FabricInterfaceProvider is required")
	}

	if c.NetDevClassProvider == nil {
		return errors.New("NetDevClassProvider is required")
	}

	return nil
}

// FabricScanner is a type that scans the system for fabric interfaces.
type FabricScanner struct {
	log      logging.Logger
	config   *FabricScannerConfig
	builders []FabricInterfaceSetBuilder
}

// NewFabricScanner creates a new FabricScanner with the given configuration.
func NewFabricScanner(log logging.Logger, config *FabricScannerConfig) (*FabricScanner, error) {
	if config == nil {
		return nil, errors.New("FabricScannerConfig is nil")
	}

	return &FabricScanner{
		log:    log,
		config: config,
	}, nil
}

func (s *FabricScanner) init(ctx context.Context) error {
	if err := s.config.Validate(); err != nil {
		return errors.Wrap(err, "invalid FabricScannerConfig")
	}

	topo, err := s.config.TopologyProvider.GetTopology(ctx)
	if err != nil {
		return err
	}

	s.builders = defaultFabricInterfaceSetBuilders(s.log,
		&FabricInterfaceSetBuilderConfig{
			Topology:                 topo,
			FabricInterfaceProviders: s.config.FabricInterfaceProviders,
			NetDevClassProvider:      s.config.NetDevClassProvider,
		})

	s.log.Debugf("initialized FabricScanner")
	return nil
}

// Scan discovers fabric interfaces in the system.
func (s *FabricScanner) Scan(ctx context.Context) (*FabricInterfaceSet, error) {
	if s == nil {
		return nil, errors.New("FabricScanner is nil")
	}

	if !s.isInitialized() {
		if err := s.init(ctx); err != nil {
			return nil, err
		}
	}

	result := NewFabricInterfaceSet()
	for _, builder := range s.builders {
		if err := builder.BuildPart(ctx, result); err != nil {
			return nil, err
		}
	}

	s.log.Debugf("discovered %d fabric interfaces:\n%s",
		result.NumFabricInterfaces(), result.String())

	return result, nil
}

func (s *FabricScanner) isInitialized() bool {
	return len(s.builders) > 0
}
