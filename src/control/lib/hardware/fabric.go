//
// (C) Copyright 2021-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package hardware

import (
	"context"
	"fmt"
	"sort"
	"strings"
	"sync"
	"time"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/dlopen"
	"github.com/daos-stack/daos/src/control/logging"
)

type (
	// FabricProvider includes information about a fabric provider.
	FabricProvider struct {
		Name     string
		Priority int
	}

	fabricProviderMap         map[string]*FabricProvider
	fabricProviderPriorityMap map[int]fabricProviderMap

	// FabricProviderSet is a non-duplicated set of FabricProviders.
	FabricProviderSet struct {
		byName     fabricProviderMap
		byPriority fabricProviderPriorityMap
	}
)

func (pm fabricProviderMap) add(p *FabricProvider) {
	if _, exists := pm[p.Name]; !exists {
		pm[p.Name] = p
	}
}

func (pm fabricProviderMap) del(p *FabricProvider) {
	delete(pm, p.Name)
}

func (pm fabricProviderMap) keys() []string {
	keys := []string{}
	for k := range pm {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	return keys
}

func (ppm fabricProviderPriorityMap) add(p *FabricProvider) {
	if _, exists := ppm[p.Priority]; !exists {
		ppm[p.Priority] = make(fabricProviderMap)
	}

	ppm[p.Priority].add(p)
}

func (ppm fabricProviderPriorityMap) del(p *FabricProvider) {
	if provs, exists := ppm[p.Priority]; exists {
		provs.del(p)
	}
}

func (ppm fabricProviderPriorityMap) keys() []int {
	keys := []int{}
	for i := range ppm {
		keys = append(keys, i)
	}
	sort.Ints(keys)
	return keys
}

func (p *FabricProvider) String() string {
	if p == nil {
		return "<nil>"
	}
	if p.Name == "" {
		return "<no name>"
	}
	return p.Name
}

func (ps *FabricProviderSet) String() string {
	if ps == nil {
		return "<nil>"
	}

	if ps.Len() == 0 {
		return "<empty>"
	}

	sorted := ps.ToSlice()
	strSlice := make([]string, 0, len(sorted))
	for _, p := range sorted {
		strSlice = append(strSlice, p.String())
	}
	return strings.Join(strSlice, ", ")
}

// Len returns the number of providers in the set.
func (ps *FabricProviderSet) Len() int {
	if ps == nil {
		return 0
	}
	return len(ps.byName)
}

// Has detects whether a provider is in the set.
func (ps *FabricProviderSet) Has(provider string) bool {
	if ps == nil {
		return false
	}

	_, exists := ps.byName[provider]
	return exists
}

// Add adds fabric providers to the set.
func (ps *FabricProviderSet) Add(providers ...*FabricProvider) {
	if ps == nil {
		return
	}

	if ps.byName == nil {
		ps.byName = make(fabricProviderMap)
	}

	if ps.byPriority == nil {
		ps.byPriority = make(fabricProviderPriorityMap)
	}

	for _, prov := range providers {
		if prov == nil || prov.Name == "" {
			continue
		}

		if old, exists := ps.byName[prov.Name]; exists {
			if prov.Priority > old.Priority {
				// lower value == higher priority
				// old has higher priority, leave it alone
				continue
			}

			ps.byPriority.del(old)
			ps.byName.del(old)
		}
		ps.byName.add(prov)
		ps.byPriority.add(prov)
	}
}

// ToSlice returns a list of FabricProviders sorted by priority.
func (ps *FabricProviderSet) ToSlice() []*FabricProvider {
	if ps == nil {
		return []*FabricProvider{}
	}

	slice := make([]*FabricProvider, 0, ps.Len())
	for _, priority := range ps.byPriority.keys() {
		provs := ps.byPriority[priority]
		for _, prov := range provs.keys() {
			slice = append(slice, provs[prov])
		}
	}
	return slice
}

// NewFabricProviderSet creates a new set of FabricProviders.
func NewFabricProviderSet(providers ...*FabricProvider) *FabricProviderSet {
	set := new(FabricProviderSet)
	for _, p := range providers {
		set.Add(p)
	}

	return set
}

// FabricInterface represents basic information about a fabric interface.
type FabricInterface struct {
	// Name is the fabric device name.
	Name string `json:"name"`
	// OSName is the device name as reported by the operating system.
	OSName string `json:"os_name"`
	// NetInterface is the set of corresponding OS-level network interface device.
	NetInterfaces common.StringSet `json:"net_interface"`
	// Providers is the set of supported providers.
	Providers *FabricProviderSet `json:"providers"`
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
	if fi.OSName != "" && fi.OSName != fi.Name {
		osName = fmt.Sprintf(" (OS name: %s)", fi.OSName)
	}

	var netIF string
	if len(fi.NetInterfaces) > 0 {
		netIF = fmt.Sprintf(" (interface: %s)", strings.Join(fi.NetInterfaces.ToSlice(), ", "))
	}

	providers := "none"
	if fi.Providers.Len() > 0 {
		providers = fi.Providers.String()
	}

	return fmt.Sprintf("%s%s%s (providers: %s)", name, osName, netIF, providers)
}

// SupportsProvider reports whether the FabricInterface supports the given provider string. If the
// string contains multiple comma-separated providers, it verifies that all are supported.
func (fi *FabricInterface) SupportsProvider(provider string) bool {
	if fi == nil {
		return false
	}

	// format: [lib+]prov[,prov2,...]
	var prefix string
	provPieces := strings.Split(provider, "+")
	providers := provPieces[0]
	if len(provPieces) > 1 {
		prefix = provPieces[0] + "+"
		providers = provPieces[1]
	}

	for _, prov := range strings.Split(providers, ",") {
		prov = prefix + prov
		if !fi.Providers.Has(prov) {
			return false
		}
	}

	return true
}

func (fi *FabricInterface) TopologyName() (string, error) {
	if fi == nil {
		return "", errors.New("FabricInterface is nil")
	}

	if fi.OSName != "" {
		return fi.OSName, nil
	}

	if fi.Name == "" {
		return "", errors.New("FabricInterface has no name")
	}

	return fi.Name, nil
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
		if cur.NUMANode == 0 {
			cur.NUMANode = fi.NUMANode
		}
		if cur.DeviceClass == NetDevClass(0) {
			cur.DeviceClass = fi.DeviceClass
		}

		// always possible to add to providers or net interfaces
		if fi.Providers != nil {
			if cur.Providers == nil {
				cur.Providers = NewFabricProviderSet()
			}
			cur.Providers.Add(fi.Providers.ToSlice()...)
		}
		if fi.NetInterfaces != nil {
			if cur.NetInterfaces == nil {
				cur.NetInterfaces = common.NewStringSet()
			}
			cur.NetInterfaces.Add(fi.NetInterfaces.ToSlice()...)
		}
		return
	}

	s[name] = fi
}

type netMap map[string]fabricInterfaceMap

func (m netMap) keys() []string {
	keys := make([]string, 0, len(m))
	for str := range m {
		keys = append(keys, str)
	}
	sort.Strings(keys)
	return keys
}

func (m netMap) update(name string, fi *FabricInterface) {
	if fi == nil || name == "" {
		return
	}

	if _, exists := m[name]; !exists {
		m[name] = make(fabricInterfaceMap)
	}
	m[name].update(fi.Name, fi)
}

func (m netMap) remove(fi *FabricInterface) {
	if fi == nil || len(fi.NetInterfaces) == 0 {
		return
	}

	for netIface := range fi.NetInterfaces {
		if devices, exists := m[netIface]; exists {
			delete(devices, fi.Name)
			if len(devices) == 0 {
				delete(m, netIface)
			}
		}
	}
}

// NewFabricInterfaceSet creates a new fabric interface set and initializes it with the passed-in
// FabricInterfaces if provided.
func NewFabricInterfaceSet(fis ...*FabricInterface) *FabricInterfaceSet {
	result := &FabricInterfaceSet{
		byName:   make(fabricInterfaceMap),
		byNetDev: make(netMap),
	}

	for _, fi := range fis {
		result.Update(fi)
	}

	return result
}

// FabricInterfaceSet is a set of fabric interfaces.
type FabricInterfaceSet struct {
	byName   fabricInterfaceMap
	byNetDev netMap
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

// NumNetDevices is the number of unique OS-level network devices in the set.
func (s *FabricInterfaceSet) NumNetDevices() int {
	if s == nil {
		return 0
	}
	return len(s.byNetDev)
}

// NetDevices provides a sorted list of the OS-level network devices.
func (s *FabricInterfaceSet) NetDevices() []string {
	if s == nil {
		return []string{}
	}
	return s.byNetDev.keys()
}

// Update updates the details of the fabric interface if it's already in the set, or adds it to the
// set otherwise.
func (s *FabricInterfaceSet) Update(fi *FabricInterface) {
	if s == nil || fi == nil {
		return
	}

	name := fi.Name
	s.byName.update(name, fi)

	for osDev := range fi.NetInterfaces {
		s.byNetDev.update(osDev, fi)
	}
}

// Remove deletes a FabricInterface from the set.
func (s *FabricInterfaceSet) Remove(fiName string) {
	fi, err := s.GetInterface(fiName)
	if err != nil {
		// Not in the set
		return
	}

	s.byNetDev.remove(fi)
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

// GetInterfaceOnNetDevice searches for an interface with a given OS network device name that
// supports a given provider.
func (s *FabricInterfaceSet) GetInterfaceOnNetDevice(netDev string, provider string) (*FabricInterface, error) {
	if s == nil {
		return nil, errors.New("nil FabricInterfaceSet")
	}

	if netDev == "" {
		return nil, errors.New("network device name is required")
	}

	if provider == "" {
		return nil, errors.New("fabric provider is required")
	}

	fis, exists := s.byNetDev[netDev]
	if !exists {
		return nil, errors.Errorf("network device %q not found", netDev)
	}

	for _, fi := range fis {
		if fi.SupportsProvider(provider) {
			return fi, nil
		}
	}

	return nil, ErrProviderNotOnDevice(provider, netDev)
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
	default:
		return fmt.Sprintf("UNKNOWN (0x%x)", uint32(c))
	}
}

// FabricInterfaceProvider is an interface that returns a new set of fabric interfaces.
type FabricInterfaceProvider interface {
	GetFabricInterfaces(ctx context.Context, provider string) (*FabricInterfaceSet, error)
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
	providers   []string
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

	// If no providers were requested, we should scan once for all providers. Empty string
	// indicates all providers.
	provs := f.providers
	if len(f.providers) == 0 {
		provs = []string{""}
	}

	fiSets := make([]*FabricInterfaceSet, 0)
	for _, fiProv := range f.fiProviders {
		for _, prov := range provs {
			set, err := fiProv.GetFabricInterfaces(ctx, prov)
			if errors.Is(errors.Cause(err), dlopen.ErrSoNotFound) || IsUnsupportedFabric(err) {
				// A runtime library wasn't installed. This is okay - we'll still detect
				// what we can.
				f.log.Debug(err.Error())
				continue
			}

			if err != nil {
				return err
			}

			fiSets = append(fiSets, set)
		}
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

func newFabricInterfaceBuilder(log logging.Logger, providers []string, fiProviders ...FabricInterfaceProvider) *FabricInterfaceBuilder {
	return &FabricInterfaceBuilder{
		log:         log,
		providers:   providers,
		fiProviders: fiProviders,
	}
}

// NetworkDeviceBuilder is a builder that updates FabricInterfaces with NetDevice.
type NetworkDeviceBuilder struct {
	log  logging.Logger
	topo *Topology
}

// BuildPart updates existing FabricInterface structures in the FabricInterfaceSet to include an
// OS-level network device name, if available.
func (o *NetworkDeviceBuilder) BuildPart(ctx context.Context, fis *FabricInterfaceSet) error {
	if o == nil {
		return errors.New("NetworkDeviceBuilder is nil")
	}

	if fis == nil {
		return errors.New("FabricInterfaceSet is nil")
	}

	if o.topo == nil {
		return errors.New("NetworkDeviceBuilder is uninitialized")
	}

	devsByName := o.topo.AllDevices()

	for _, name := range fis.Names() {
		fi, err := fis.GetInterface(name)
		if err != nil {
			o.log.Errorf("can't update interface %s: %s", name, err.Error())
			continue
		}

		topoName, err := fi.TopologyName()
		if err != nil {
			o.log.Errorf("can't get topology name for %q: %s", name, err.Error())
			continue
		}

		dev, exists := devsByName[topoName]
		if !exists {
			o.log.Tracef("ignoring fabric interface %q (%s) not found in topology", name, topoName)
			fis.Remove(name)
			continue
		}

		if fi.NetInterfaces == nil {
			fi.NetInterfaces = common.NewStringSet()
		}

		if dev.DeviceType() == DeviceTypeNetInterface {
			fi.NetInterfaces.Add(name)
			fis.Update(fi)
			continue
		}

		if pciDev := dev.PCIDevice(); pciDev != nil {
			netDevs := o.getNetDevsForPCIDevice(pciDev)
			fi.NetInterfaces.Add(netDevs.ToSlice()...)

			if len(fi.NetInterfaces) == 0 {
				o.log.Errorf("no network devices found for fabric %q", name)
			} else {
				fis.Update(fi)
			}
		}
	}

	return nil
}

func (o *NetworkDeviceBuilder) getNetDevsForPCIDevice(pciDev *PCIDevice) common.StringSet {
	devNames := common.NewStringSet()

	siblings := o.topo.NUMANodes[pciDev.NUMANode.ID].PCIDevices[pciDev.PCIAddr]
	for _, sib := range siblings {
		if sib.Type == DeviceTypeNetInterface {
			devNames.Add(sib.Name)
		}
	}

	// See if any virtual devices are attached to the hardware devices
	for _, virtDev := range o.topo.VirtualDevices {
		if virtDev.BackingDevice == nil {
			continue
		}

		if devNames.Has(virtDev.BackingDevice.Name) {
			devNames.Add(virtDev.Name)
		}
	}

	return devNames
}

func newNetworkDeviceBuilder(log logging.Logger, topo *Topology) *NetworkDeviceBuilder {
	return &NetworkDeviceBuilder{
		log:  log,
		topo: topo,
	}
}

// NUMAAffinityBuilder is a builder that updates FabricInterfaces with a NUMA node affinity.
type NUMAAffinityBuilder struct {
	log  logging.Logger
	topo *Topology
}

// BuildPart updates existing FabricInterface structures in the set to include a
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

		topoName, err := fi.TopologyName()
		if err != nil {
			n.log.Errorf("can't get topology name for %q: %s", name, err.Error())
			continue
		}

		dev, exists := devsByName[topoName]
		if !exists {
			n.log.Tracef("fabric interface %q (%s) not found in topology", name, topoName)
			continue
		}

		pciDev := dev.PCIDevice()
		if pciDev != nil {
			fi.NUMANode = pciDev.NUMANode.ID
		}
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

		if len(fi.NetInterfaces) == 0 {
			n.log.Tracef("fabric interface %q has no corresponding OS-level device", name)
			continue
		}

		ndc, err := n.provider.GetNetDevClass(fi.NetInterfaces.ToSlice()[0])
		if err != nil {
			n.log.Tracef("failed to get device class for %q: %s", name, err.Error())
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
	Providers                []string
	FabricInterfaceProviders []FabricInterfaceProvider
	NetDevClassProvider      NetDevClassProvider
}

func defaultFabricInterfaceSetBuilders(log logging.Logger, config *FabricInterfaceSetBuilderConfig) []FabricInterfaceSetBuilder {
	return []FabricInterfaceSetBuilder{
		newFabricInterfaceBuilder(log, config.Providers, config.FabricInterfaceProviders...),
		newNetworkDeviceBuilder(log, config.Topology),
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
	log       logging.Logger
	mutex     sync.Mutex
	config    *FabricScannerConfig
	providers common.StringSet
	builders  []FabricInterfaceSetBuilder
	topo      *Topology
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

func (s *FabricScanner) init(ctx context.Context, providers []string) error {
	if err := s.config.Validate(); err != nil {
		return errors.Wrap(err, "invalid FabricScannerConfig")
	}

	topo := s.topo
	if topo == nil {
		var err error
		topo, err = s.config.TopologyProvider.GetTopology(ctx)
		if err != nil {
			return err
		}
	}

	s.providers = common.NewStringSet(providers...)
	s.builders = defaultFabricInterfaceSetBuilders(s.log,
		&FabricInterfaceSetBuilderConfig{
			Topology:                 topo,
			Providers:                providers,
			FabricInterfaceProviders: s.config.FabricInterfaceProviders,
			NetDevClassProvider:      s.config.NetDevClassProvider,
		})
	return nil
}

// Scan discovers fabric interfaces in the system.
func (s *FabricScanner) Scan(ctx context.Context, providers ...string) (*FabricInterfaceSet, error) {
	if s == nil {
		return nil, errors.New("FabricScanner is nil")
	}

	s.mutex.Lock()
	defer s.mutex.Unlock()

	if !s.isInitialized(providers) {
		if err := s.init(ctx, providers); err != nil {
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

	if result.NumFabricInterfaces() == 0 {
		if len(providers) == 0 {
			return nil, errors.New("no fabric interfaces found")
		}
		return nil, fmt.Errorf("no fabric interfaces found with providers: %s", strings.Join(providers, ", "))
	}
	return result, nil
}

func (s *FabricScanner) isInitialized(providers []string) bool {
	if len(s.builders) == 0 {
		return false
	}

	if len(s.providers) != len(providers) {
		return false
	}

	for _, prov := range providers {
		if !s.providers.Has(prov) {
			return false
		}
	}

	return true
}

// CacheTopology caches a specific HW topology for use with the fabric scan.
func (s *FabricScanner) CacheTopology(t *Topology) error {
	if s == nil {
		return errors.New("FabricScanner is nil")
	}

	if t == nil {
		return errors.New("Topology is nil")
	}

	s.mutex.Lock()
	defer s.mutex.Unlock()

	s.topo = t
	return nil
}

// NetDevState describes the state of a network device.
type NetDevState int

const (
	// NetDevStateUnknown indicates that the device state can't be determined.
	NetDevStateUnknown NetDevState = iota
	// NetDevStateDown indicates that the device is down. This could mean the device is disabled
	// or physically disconnected.
	NetDevStateDown
	// NetDevStateNotReady indicates that the device is connected but not ready to communicate.
	NetDevStateNotReady
	// NetDevStateReady indicates that the device is up and ready to communicate.
	NetDevStateReady
)

// NetDevStateProvider is an interface for a type that can be used to get the state of a network
// device.
type NetDevStateProvider interface {
	GetNetDevState(string) (NetDevState, error)
}

// WaitFabricReadyParams defines the parameters for a WaitFabricReady call.
type WaitFabricReadyParams struct {
	StateProvider  NetDevStateProvider
	FabricIfaces   []string      // Fabric interfaces that must become ready
	IterationSleep time.Duration // Time between iterations if some interfaces aren't ready
	IgnoreUnusable bool          // Ignore interfaces that are in an unusable state
}

// WaitFabricReady loops until either all fabric interfaces are ready, or the context is canceled.
func WaitFabricReady(ctx context.Context, log logging.Logger, params WaitFabricReadyParams) error {
	if common.InterfaceIsNil(params.StateProvider) {
		return errors.New("nil NetDevStateProvider")
	}

	if len(params.FabricIfaces) == 0 {
		return errors.New("no fabric interfaces requested")
	}

	params.FabricIfaces = common.DedupeStringSlice(params.FabricIfaces)

	ch := make(chan error)
	go loopFabricReady(log, params, ch)

	select {
	case <-ctx.Done():
		return ctx.Err()
	case err := <-ch:
		return err
	}
}

func loopFabricReady(log logging.Logger, params WaitFabricReadyParams, ch chan error) {
	readySet := common.NewStringSet()
	unusableSet := common.NewStringSet()
	log.Trace("waiting for fabric interfaces to become ready...")
	for {
		for _, iface := range params.FabricIfaces {
			// No need to check again if we marked it ready or unusable
			if readySet.Has(iface) || unusableSet.Has(iface) {
				continue
			}

			state, err := params.StateProvider.GetNetDevState(iface)
			if err != nil {
				log.Errorf("error while checking state of fabric interface %q: %s", iface, err.Error())
				ch <- err
				return
			}

			switch state {
			case NetDevStateReady:
				log.Debugf("fabric interface %q is ready", iface)
				readySet.Add(iface)
			case NetDevStateDown, NetDevStateUnknown:
				// Down or unknown can be interpreted as disabled/unusable
				if params.IgnoreUnusable {
					log.Tracef("ignoring unusable fabric interface %q", iface)
					unusableSet.Add(iface)
				} else {
					ch <- errors.Errorf("requested fabric interface %q is unusable", iface)
					return
				}
			}
		}

		if len(unusableSet) == len(params.FabricIfaces) {
			ch <- errors.Errorf("no usable fabric interfaces requested: %s", strings.Join(params.FabricIfaces, ", "))
			return
		}

		// All usable interfaces up
		if len(readySet)+len(unusableSet) == len(params.FabricIfaces) {
			break
		}

		numNotReady := len(params.FabricIfaces) - len(readySet)
		readyMsg := ""
		if len(readySet) != 0 {
			readyMsg = fmt.Sprintf("; ready: %s", readySet.String())
		}
		log.Errorf("%d fabric interface(s) not ready (requested: %s%s)",
			numNotReady, strings.Join(params.FabricIfaces, ", "), readyMsg)
		time.Sleep(params.IterationSleep)
	}

	ch <- nil
}
