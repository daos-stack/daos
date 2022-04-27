//
// (C) Copyright 2021-2022 Intel Corporation.
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
	"sync"
	"time"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/dlopen"
	"github.com/daos-stack/daos/src/control/logging"
)

// IsUnsupportedFabric returns true if the supplied error is
// an instance of errUnsupportedFabric.
func IsUnsupportedFabric(err error) bool {
	_, ok := errors.Cause(err).(*errUnsupportedFabric)
	return ok
}

type errUnsupportedFabric struct {
	provider string
}

func (euf *errUnsupportedFabric) Error() string {
	return fmt.Sprintf("fabric provider %q not supported", euf.provider)
}

// ErrUnsupportedFabric returns an error indicating that the denoted
// fabric provider is not supported by this build/platform.
func ErrUnsupportedFabric(provider string) error {
	return &errUnsupportedFabric{provider: provider}
}

// IsFabricNotReady returns true if the supplied error is an instance of errFabricNotReady.
func IsFabricNotReady(err error) bool {
	_, ok := errors.Cause(err).(*errFabricNotReady)
	return ok
}

type errFabricNotReady struct {
	iface string
}

func (e *errFabricNotReady) Error() string {
	return fmt.Sprintf("fabric interface %q is not ready", e.iface)
}

// ErrFabricNotReady returns an error indicating that the denoted fabric interface is not ready
// for use.
func ErrFabricNotReady(iface string) error {
	return &errFabricNotReady{iface: iface}
}

// IsProviderNotOnDevice indicates whether the error is an instance of
// errProviderNotOnDevice.
func IsProviderNotOnDevice(err error) bool {
	_, ok := errors.Cause(err).(*errProviderNotOnDevice)
	return ok
}

type errProviderNotOnDevice struct {
	provider string
	device   string
}

func (e *errProviderNotOnDevice) Error() string {
	return fmt.Sprintf("fabric provider %q not supported on network device %q", e.provider, e.device)
}

// ErrProviderNotOnDevice returns an error indicated that the fabric provider
// is not available on the given network device.
func ErrProviderNotOnDevice(provider, dev string) error {
	return &errProviderNotOnDevice{
		provider: provider,
		device:   dev,
	}
}

// FabricInterface represents basic information about a fabric interface.
type FabricInterface struct {
	// Name is the fabric device name.
	Name string `json:"name"`
	// OSName is the device name as reported by the operating system.
	OSName string `json:"os_name"`
	// NetInterface is the corresponding OS-level network interface device.
	NetInterface string `json:"net_interface"`
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
	if fi.OSName != "" && fi.OSName != fi.Name {
		osName = fmt.Sprintf(" (OS name: %s)", fi.OSName)
	}

	var netIF string
	if fi.NetInterface != "" {
		netIF = fmt.Sprintf(" (interface: %s)", fi.NetInterface)
	}

	providers := "none"
	if len(fi.Providers) > 0 {
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
		if cur.NetInterface == "" {
			cur.NetInterface = fi.NetInterface
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
	if fi == nil || fi.NetInterface == "" {
		return
	}

	if devices, exists := m[fi.NetInterface]; exists {
		delete(devices, fi.Name)
		if len(devices) == 0 {
			delete(m, fi.NetInterface)
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

	osDev := fi.NetInterface
	if osDev == "" {
		return
	}

	s.byNetDev.update(osDev, fi)
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

		if fi.DeviceClass == Loopback || fi.Name == "lo" {
			// Loopback is not a hardware device
			fi.NetInterface = name
			fis.Update(fi)
			continue
		}

		topoName, err := fi.TopologyName()
		if err != nil {
			o.log.Errorf("can't get topology name for %q: %s", name, err.Error())
			continue
		}

		dev, exists := devsByName[topoName]
		if !exists {
			o.log.Debugf("ignoring fabric interface %q (%s) not found in topology", name, topoName)
			fis.Remove(name)
			continue
		}

		if dev.Type == DeviceTypeNetInterface {
			fi.NetInterface = name
			fis.Update(fi)
			continue
		}

		siblings := o.topo.NUMANodes[dev.NUMANode.ID].PCIDevices[dev.PCIAddr]
		for _, sib := range siblings {
			if sib.Type == DeviceTypeNetInterface {
				fi.NetInterface = sib.Name
				fis.Update(fi)
				break
			}
		}

		if fi.NetInterface == "" {
			o.log.Errorf("no network device sibling found for fabric %q", name)
		}
	}

	return nil
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

		topoName, err := fi.TopologyName()
		if err != nil {
			n.log.Errorf("can't get topology name for %q: %s", name, err.Error())
			continue
		}

		dev, exists := devsByName[topoName]
		if !exists {
			n.log.Debugf("fabric interface %q (%s) not found in topology", name, topoName)
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

		if fi.NetInterface == "" {
			n.log.Debugf("fabric interface %q has no corresponding OS-level device", name)
			continue
		}

		ndc, err := n.provider.GetNetDevClass(fi.NetInterface)
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
	log      logging.Logger
	mutex    sync.Mutex
	config   *FabricScannerConfig
	builders []FabricInterfaceSetBuilder
	topo     *Topology
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

	topo := s.topo
	if topo == nil {
		var err error
		topo, err = s.config.TopologyProvider.GetTopology(ctx)
		if err != nil {
			return err
		}
	}

	s.builders = defaultFabricInterfaceSetBuilders(s.log,
		&FabricInterfaceSetBuilderConfig{
			Topology:                 topo,
			FabricInterfaceProviders: s.config.FabricInterfaceProviders,
			NetDevClassProvider:      s.config.NetDevClassProvider,
		})
	return nil
}

// Scan discovers fabric interfaces in the system.
func (s *FabricScanner) Scan(ctx context.Context) (*FabricInterfaceSet, error) {
	if s == nil {
		return nil, errors.New("FabricScanner is nil")
	}

	s.mutex.Lock()
	defer s.mutex.Unlock()

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

// FabricReadyChecker is an interface for a type that can be used to check whether a fabric
// interface is ready.
type FabricReadyChecker interface {
	CheckFabricReady(string) error
}

// WaitFabricReadyParams defines the parameters for a WaitFabricReady call.
type WaitFabricReadyParams struct {
	Checker        FabricReadyChecker
	FabricIfaces   []string      // Fabric interfaces that must become ready
	IterationSleep time.Duration // Time between iterations if some interfaces aren't ready
}

// WaitFabricReady loops until either all fabric interfaces are ready, or the context is canceled.
func WaitFabricReady(ctx context.Context, log logging.Logger, params WaitFabricReadyParams) error {
	if common.InterfaceIsNil(params.Checker) {
		return errors.New("nil FabricReadyChecker")
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
	log.Debug("waiting for fabric interfaces to become ready...")
	for {
		for _, iface := range params.FabricIfaces {
			// No need to check again if we marked it ready
			if readySet.Has(iface) {
				continue
			}

			err := params.Checker.CheckFabricReady(iface)

			switch {
			case err == nil:
				log.Debugf("fabric interface %q is ready", iface)
				readySet.Add(iface)
			case !IsFabricNotReady(err):
				log.Errorf("error while checking readiness of fabric interface %q: %s", iface, err.Error())
				ch <- err
				return
			}
		}

		// All interfaces up
		if len(readySet) == len(params.FabricIfaces) {
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
