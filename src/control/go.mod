module github.com/daos-stack/daos/src/control

go 1.13

require (
	github.com/Jille/raft-grpc-transport v1.2.0
	github.com/armon/go-metrics v0.4.0 // indirect
	github.com/desertbit/go-shlex v0.1.1
	github.com/desertbit/grumble v1.1.3
	github.com/dustin/go-humanize v1.0.0
	github.com/google/go-cmp v0.5.8
	github.com/google/uuid v1.3.0
	github.com/hashicorp/go-hclog v1.2.2
	github.com/hashicorp/go-uuid v1.0.1 // indirect
	github.com/hashicorp/raft v1.3.9
	github.com/hashicorp/raft-boltdb/v2 v2.0.0-20210409134258-03c10cc3d4ea
	github.com/jessevdk/go-flags v1.5.0
	github.com/mitchellh/hashstructure/v2 v2.0.2
	github.com/pkg/errors v0.9.1
	github.com/prometheus/client_golang v1.12.2
	github.com/prometheus/client_model v0.2.0
	github.com/prometheus/common v0.32.1
	go.etcd.io/bbolt v1.3.5
	golang.org/x/net v0.0.0-20220725212005-46097bf591d3
	golang.org/x/sys v0.0.0-20220722155257-8c9f86f7a55f
	google.golang.org/genproto v0.0.0-20220725144611-272f38e5d71b
	google.golang.org/grpc v1.48.0
	google.golang.org/protobuf v1.28.0
	gopkg.in/yaml.v2 v2.4.0
)
