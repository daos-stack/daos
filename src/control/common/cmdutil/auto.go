package cmdutil

type ConfGenCmd struct {
	AccessPoints    string `default:"localhost" short:"a" long:"access-points" description:"Comma separated list of access point addresses <ipv4addr/hostname> to host management service"`
	NrEngines       int    `short:"e" long:"num-engines" description:"Set the number of DAOS Engine sections to be populated in the config file output. If unset then the value will be set to the number of NUMA nodes on storage hosts in the DAOS system."`
	SCMOnly         bool   `short:"s" long:"scm-only" description:"Create a SCM-only config without NVMe SSDs."`
	NetClass        string `default:"infiniband" short:"c" long:"net-class" description:"Set the network device class to be used" choice:"ethernet" choice:"infiniband"`
	NetProvider     string `short:"p" long:"net-provider" description:"Set the network fabric provider to be used"`
	UseTmpfsSCM     bool   `short:"t" long:"use-tmpfs-scm" description:"Use tmpfs for scm rather than PMem"`
	ExtMetadataPath string `short:"m" long:"control-metadata-path" description:"External storage path to store control metadata. Set this to a persistent location and specify --use-tmpfs-scm to create an MD-on-SSD config"`
	FabricPorts     string `short:"f" long:"fabric-ports" description:"Allow custom fabric interface ports to be specified for each engine config section. Comma separated port numbers, one per engine"`
}
