#
# (C) Copyright 2019-2021 Intel Corporation.
#
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
DAOS_ROOT ?= $(shell git rev-parse --show-toplevel)

C_HEADER_FILES = include/daos/drpc.pb-c.h\
		 engine/srv.pb-c.h\
		 engine/event.pb-c.h\
		 mgmt/acl.pb-c.h\
		 mgmt/cont.pb-c.h\
		 mgmt/pool.pb-c.h\
		 mgmt/smd.pb-c.h\
		 bio/smd.pb-c.h\
		 mgmt/server.pb-c.h\
		 mgmt/svc.pb-c.h\
		 mgmt/check.pb-c.h\
		 chk/chk.pb-c.h\
		 security/auth.pb-c.h\
		 tests/drpc/drpc_test.pb-c.h
C_SOURCE_FILES = common/drpc.pb-c.c\
		 engine/srv.pb-c.c\
		 engine/event.pb-c.c\
		 mgmt/acl.pb-c.c\
		 mgmt/cont.pb-c.c\
		 mgmt/pool.pb-c.c\
		 mgmt/smd.pb-c.c\
		 bio/smd.pb-c.c\
		 mgmt/server.pb-c.c\
		 mgmt/svc.pb-c.c\
		 mgmt/check.pb-c.c\
		 chk/chk.pb-c.c\
		 security/auth.pb-c.c\
		 tests/drpc/drpc_test.pb-c.c
GO_CONTROL_FILES = common/proto/shared/ranks.pb.go\
		   common/proto/shared/event.pb.go\
		   common/proto/mgmt/acl.pb.go\
		   common/proto/mgmt/cont.pb.go\
		   common/proto/mgmt/check.pb.go\
		   common/proto/mgmt/mgmt.pb.go\
		   common/proto/mgmt/pool.pb.go\
		   common/proto/mgmt/svc.pb.go\
		   common/proto/mgmt/system.pb.go\
		   common/proto/ctl/smd.pb.go\
		   common/proto/ctl/server.pb.go\
		   common/proto/ctl/storage.pb.go\
		   common/proto/ctl/common.pb.go\
		   common/proto/ctl/storage_nvme.pb.go\
		   common/proto/ctl/storage_scm.pb.go\
		   common/proto/ctl/ctl.pb.go\
		   common/proto/ctl/network.pb.go\
		   common/proto/ctl/support.pb.go\
		   common/proto/ctl/firmware.pb.go\
		   common/proto/ctl/ranks.pb.go\
		   common/proto/chk/chk.pb.go\
		   common/proto/chk/faults.pb.go\
		   common/proto/srv/srv.pb.go\
		   drpc/drpc.pb.go\
		   security/auth/auth.pb.go\
		   cmd/drpc_test/hello/drpc_test.pb.go
CTRL_SOURCE_ROOT = $(DAOS_ROOT)/src/control
PROTO_SOURCE_DIR = $(DAOS_ROOT)/src/proto
uniq = $(if $1,$(firstword $1) $(call uniq,$(filter-out $(firstword $1),$1)))

all: proto-go proto-c

clean: clean-gen
	rm -f $(GO_TARGETS) $(C_TARGETS)

PROTOC := $(shell which protoc 2>/dev/null)
ifeq ($(PROTOC),)
	PROTOC = "needs-install"
endif

$(PROTOC):
	@echo "Please install the protobufs compiler"
	@echo "Details: https://github.com/daos-stack/daos/blob/master/doc/dev/development.md#protobuf-compiler"
	@false

GOPATH ?= $(shell go env GOPATH)
PROTOC_GEN_GO_PKG := google.golang.org/protobuf
PROTOC_GEN_GO_CMD := $(PROTOC_GEN_GO_PKG)/cmd/protoc-gen-go
PROTOC_GEN_GO_MOD_TAG = $(shell awk '/$(subst /,\/,$(PROTOC_GEN_GO_PKG))/ {print $$2}' $(DAOS_ROOT)/src/control/go.mod)
PROTOC_GEN_GO_TAG ?= $(PROTOC_GEN_GO_MOD_TAG)
PROTOC_GEN_GO := $(GOPATH)/bin/$(notdir $(PROTOC_GEN_GO_CMD))

$(PROTOC_GEN_GO): $(PROTOC)
	@echo "Installing protobuf generator ($(PROTOC_GEN_GO_TAG)) for Go..."
	go install $(PROTOC_GEN_GO_CMD)@$(PROTOC_GEN_GO_TAG)

PROTOC_GEN_GO_GRPC_PKG := google.golang.org/grpc
PROTOC_GEN_GO_GRPC_CMD := $(PROTOC_GEN_GO_GRPC_PKG)/cmd/protoc-gen-go-grpc
PROTOC_GEN_GO_GRPC_TAG ?= latest
PROTOC_GEN_GO_GRPC := $(GOPATH)/bin/$(notdir $(PROTOC_GEN_GO_GRPC_CMD))

$(PROTOC_GEN_GO_GRPC): $(PROTOC)
	@echo "Installing gRPC protobuf generator ($(PROTOC_GEN_GO_GRPC_TAG)) for Go..."
	go install $(PROTOC_GEN_GO_GRPC_CMD)@$(PROTOC_GEN_GO_GRPC_TAG)

clean-gen:
	rm -f $(PROTOC_GEN_GO) $(PROTOC_GEN_GO_GRPC)

GO_TARGETS = $(addprefix $(CTRL_SOURCE_ROOT)/,$(GO_CONTROL_FILES))
GO_PKG_DIRS = $(call uniq,$(dir $(GO_TARGETS)))

proto-go: $(PROTOC_GEN_GO) $(PROTOC_GEN_GO_GRPC) $(GO_PKG_DIRS) $(GO_TARGETS)

$(GO_PKG_DIRS):
	mkdir -p $@

$(CTRL_SOURCE_ROOT)/common/proto/%.pb.go: $(PROTO_SOURCE_DIR)/%.proto
	protoc -I $(PROTO_SOURCE_DIR) --go_out=$(shell dirname $(dir $@)) --go_opt=paths=source_relative \
				      --go-grpc_out=$(shell dirname $(dir $@)) --go-grpc_opt=paths=source_relative $<

# Some of these don't follow a regular pattern, so define
# one-off recipes for them.
$(CTRL_SOURCE_ROOT)/drpc/%.pb.go: $(PROTO_SOURCE_DIR)/%.proto
	protoc -I $(dir $<) --go_out=$(dir $@) --go_opt=paths=source_relative \
			    --go-grpc_out=$(dir $@) --go-grpc_opt=paths=source_relative $<

$(CTRL_SOURCE_ROOT)/security/auth/%.pb.go: $(PROTO_SOURCE_DIR)/security/%.proto
	protoc -I $(dir $<) --go_out=$(dir $@) --go_opt=paths=source_relative \
			    --go-grpc_out=$(dir $@) --go-grpc_opt=paths=source_relative $<

$(CTRL_SOURCE_ROOT)/cmd/drpc_test/hello/%.pb.go: $(PROTO_SOURCE_DIR)/test/%.proto
	protoc -I $(dir $<) --go_out=$(dir $@) --go_opt=paths=source_relative \
			    --go-grpc_out=$(dir $@) --go-grpc_opt=paths=source_relative $<

PROTOC_GEN_C := $(shell which protoc-gen-c 2>/dev/null)
ifeq ($(PROTOC_GEN_C),)
	PROTOC_GEN_C = "needs-install"
endif

$(PROTOC_GEN_C):
	@echo "Please install the protobuf-c plugin"
	@false

C_TARGETS = $(addprefix $(DAOS_ROOT)/src/,$(C_HEADER_FILES)) \
	    $(addprefix $(DAOS_ROOT)/src/,$(C_SOURCE_FILES))

proto-c: $(PROTOC_GEN_C) $(C_TARGETS)

$(DAOS_ROOT)/src/%.pb-c.h: $(PROTO_SOURCE_DIR)/%.proto
	protoc -I $(dir $<) -I $(PROTO_SOURCE_DIR) --c_out=$(dir $@) $(notdir $<)

$(DAOS_ROOT)/src/%.pb-c.c: $(PROTO_SOURCE_DIR)/%.proto
	protoc -I $(dir $<) -I $(PROTO_SOURCE_DIR) --c_out=$(dir $@) $(notdir $<)

$(DAOS_ROOT)/src/bio/smd.pb-c.h: $(PROTO_SOURCE_DIR)/ctl/smd.proto
	protoc -I $(dir $<) --c_out=$(dir $@) $(notdir $<)

$(DAOS_ROOT)/src/bio/smd.pb-c.c: $(PROTO_SOURCE_DIR)/ctl/smd.proto
	protoc -I $(dir $<) --c_out=$(dir $@) $(notdir $<)

$(DAOS_ROOT)/src/engine/event.pb-c.h: $(PROTO_SOURCE_DIR)/shared/event.proto
	protoc -I $(dir $<) --c_out=$(dir $@) $(notdir $<)

$(DAOS_ROOT)/src/engine/event.pb-c.c: $(PROTO_SOURCE_DIR)/shared/event.proto
	protoc -I $(dir $<) --c_out=$(dir $@) $(notdir $<)

$(DAOS_ROOT)/src/engine/%.pb-c.h: $(PROTO_SOURCE_DIR)/srv/%.proto
	protoc -I $(dir $<) -I $(PROTO_SOURCE_DIR) --c_out=$(dir $@) $(notdir $<)

$(DAOS_ROOT)/src/engine/%.pb-c.c: $(PROTO_SOURCE_DIR)/srv/%.proto
	protoc -I $(dir $<) -I $(PROTO_SOURCE_DIR) --c_out=$(dir $@) $(notdir $<)

$(DAOS_ROOT)/src/mgmt/smd.pb-c.h: $(PROTO_SOURCE_DIR)/ctl/smd.proto
	protoc -I $(dir $<) --c_out=$(dir $@) $(notdir $<)

$(DAOS_ROOT)/src/mgmt/smd.pb-c.c: $(PROTO_SOURCE_DIR)/ctl/smd.proto
	protoc -I $(dir $<) --c_out=$(dir $@) $(notdir $<)

$(DAOS_ROOT)/src/mgmt/server.pb-c.h: $(PROTO_SOURCE_DIR)/ctl/server.proto
	protoc -I $(dir $<) --c_out=$(dir $@) $(notdir $<)

$(DAOS_ROOT)/src/mgmt/server.pb-c.c: $(PROTO_SOURCE_DIR)/ctl/server.proto
	protoc -I $(dir $<) --c_out=$(dir $@) $(notdir $<)

$(DAOS_ROOT)/src/tests/drpc/%.pb-c.h: $(PROTO_SOURCE_DIR)/test/%.proto
	protoc -I $(dir $<) --c_out=$(dir $@) $(notdir $<)

$(DAOS_ROOT)/src/tests/drpc/%.pb-c.c: $(PROTO_SOURCE_DIR)/test/%.proto
	protoc -I $(dir $<) --c_out=$(dir $@) $(notdir $<)

$(DAOS_ROOT)/src/include/daos/drpc.pb-c.h: $(PROTO_SOURCE_DIR)/drpc.proto
	protoc -I $(dir $<) --c_out=$(dir $@) $(notdir $<)
	rm $(basename $@).c

$(DAOS_ROOT)/src/common/drpc.pb-c.c: $(PROTO_SOURCE_DIR)/drpc.proto
	protoc -I $(dir $<) --c_out=$(dir $@) $(notdir $<)
	rm $(basename $@).h
