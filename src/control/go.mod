module github.com/daos-stack/daos/src/control

// NB: When updating minimum Go build version, don't forget to update:
// - rpm packaging version checks: utils/rpms/daos.spec
// - debian packaging version checks: debian/control
// Scons uses this file to extract the minimum version.
go 1.24.0

require (
	github.com/Jille/raft-grpc-transport v1.6.1
	github.com/desertbit/columnize v2.1.0+incompatible
	github.com/desertbit/go-shlex v0.1.1
	github.com/desertbit/grumble v1.2.0
	github.com/dustin/go-humanize v1.0.1
	github.com/google/go-cmp v0.7.0
	github.com/google/uuid v1.6.0
	github.com/hashicorp/go-hclog v1.6.3
	github.com/hashicorp/raft v1.7.3
	github.com/hashicorp/raft-boltdb/v2 v2.3.1
	github.com/jessevdk/go-flags v1.6.1
	github.com/mitchellh/hashstructure/v2 v2.0.2
	github.com/pkg/errors v0.9.1
	github.com/prometheus/client_golang v1.23.2
	github.com/prometheus/client_model v0.6.2
	github.com/prometheus/common v0.67.5
	go.etcd.io/bbolt v1.4.3
	golang.org/x/net v0.49.0
	golang.org/x/sys v0.40.0
	google.golang.org/genproto/googleapis/rpc v0.0.0-20260112192933-99fd39fd28a9
	google.golang.org/grpc v1.78.0
	google.golang.org/protobuf v1.36.11
	gopkg.in/yaml.v2 v2.4.0
)

require (
	github.com/armon/go-metrics v0.4.1 // indirect
	github.com/beorn7/perks v1.0.1 // indirect
	github.com/boltdb/bolt v1.3.1 // indirect
	github.com/cespare/xxhash/v2 v2.3.0 // indirect
	github.com/desertbit/closer/v3 v3.7.5 // indirect
	github.com/desertbit/readline v1.5.1 // indirect
	github.com/fatih/color v1.18.0 // indirect
	github.com/hashicorp/errwrap v1.1.0 // indirect
	github.com/hashicorp/go-immutable-radix v1.3.1 // indirect
	github.com/hashicorp/go-metrics v0.5.4 // indirect
	github.com/hashicorp/go-msgpack v1.1.5 // indirect
	github.com/hashicorp/go-msgpack/v2 v2.1.5 // indirect
	github.com/hashicorp/go-multierror v1.1.1 // indirect
	github.com/hashicorp/go-uuid v1.0.1 // indirect
	github.com/hashicorp/golang-lru v1.0.2 // indirect
	github.com/mattn/go-colorable v0.1.14 // indirect
	github.com/mattn/go-isatty v0.0.20 // indirect
	github.com/munnerz/goautoneg v0.0.0-20191010083416-a7dc8b61c822 // indirect
	github.com/prometheus/procfs v0.19.2 // indirect
	go.yaml.in/yaml/v2 v2.4.3 // indirect
	golang.org/x/text v0.33.0 // indirect
)
