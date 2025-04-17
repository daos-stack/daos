module github.com/daos-stack/daos/src/control

// NB: When updating minimum Go build version, don't forget to update:
// - rpm packaging version checks: utils/rpms/daos.spec
// - debian packaging version checks: debian/control
// Scons uses this file to extract the minimum version.
go 1.21
toolchain go1.23.7

require (
	github.com/Jille/raft-grpc-transport v1.2.0
	github.com/desertbit/grumble v1.1.3
	github.com/dustin/go-humanize v1.0.0
	github.com/google/go-cmp v0.6.0
	github.com/google/uuid v1.6.0
	github.com/hashicorp/go-hclog v1.2.2
	github.com/hashicorp/raft v1.3.9
	github.com/hashicorp/raft-boltdb/v2 v2.0.0-20210409134258-03c10cc3d4ea
	github.com/jessevdk/go-flags v1.5.0
	github.com/mitchellh/hashstructure/v2 v2.0.2
	github.com/pkg/errors v0.9.1
	github.com/prometheus/client_golang v1.12.2
	github.com/prometheus/client_model v0.2.0
	github.com/prometheus/common v0.32.1
	go.etcd.io/bbolt v1.3.5
	golang.org/x/net v0.37.0
	golang.org/x/sys v0.31.0
	google.golang.org/genproto v0.0.0-20230410155749-daa745c078e1
	google.golang.org/grpc v1.66.2
	google.golang.org/protobuf v1.34.1
	gopkg.in/yaml.v2 v2.4.0
)

require (
	github.com/armon/go-metrics v0.4.0 // indirect
	github.com/beorn7/perks v1.0.1 // indirect
	github.com/cespare/xxhash/v2 v2.3.0 // indirect
	github.com/desertbit/closer/v3 v3.1.2 // indirect
	github.com/desertbit/columnize v2.1.0+incompatible // indirect
	github.com/desertbit/go-shlex v0.1.1 // indirect
	github.com/desertbit/readline v1.5.1 // indirect
	github.com/fatih/color v1.13.0 // indirect
	github.com/golang/protobuf v1.5.3 // indirect
	github.com/hashicorp/errwrap v1.1.0 // indirect
	github.com/hashicorp/go-immutable-radix v1.3.1 // indirect
	github.com/hashicorp/go-msgpack v1.1.5 // indirect
	github.com/hashicorp/go-multierror v1.1.0 // indirect
	github.com/hashicorp/go-uuid v1.0.1 // indirect
	github.com/hashicorp/golang-lru v0.5.4 // indirect
	github.com/mattn/go-colorable v0.1.12 // indirect
	github.com/mattn/go-isatty v0.0.14 // indirect
	github.com/matttproud/golang_protobuf_extensions v1.0.1 // indirect
	github.com/prometheus/procfs v0.7.3 // indirect
	golang.org/x/text v0.23.0 // indirect
)
