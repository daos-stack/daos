#
# (C) Copyright 2019-2020 Intel Corporation.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
# The Government's rights to use, modify, reproduce, release, perform, display,
# or disclose this software are subject to the terms of the Apache License as
# provided in Contract No. B609815.
# Any reproduction of computer software, computer software documentation, or
# portions thereof marked with this legend must also reproduce the markings.
#
DAOS_ROOT ?= $(shell git rev-parse --show-toplevel)

C_HEADER_FILES = include/daos/drpc.pb-c.h\
		 iosrv/srv.pb-c.h\
		 mgmt/acl.pb-c.h\
		 mgmt/cont.pb-c.h\
		 mgmt/pool.pb-c.h\
		 mgmt/storage_query.pb-c.h\
		 mgmt/svc.pb-c.h\
		 security/auth.pb-c.h\
		 tests/drpc/drpc_test.pb-c.h
C_SOURCE_FILES = common/drpc.pb-c.c\
		 iosrv/srv.pb-c.c\
		 mgmt/acl.pb-c.c\
		 mgmt/cont.pb-c.c\
		 mgmt/pool.pb-c.c\
		 mgmt/storage_query.pb-c.c\
		 mgmt/svc.pb-c.c\
		 security/auth.pb-c.c\
		 tests/drpc/drpc_test.pb-c.c
GO_CONTROL_FILES = common/proto/mgmt/acl.pb.go\
		   common/proto/mgmt/cont.pb.go\
		   common/proto/mgmt/mgmt.pb.go\
		   common/proto/mgmt/pool.pb.go\
		   common/proto/mgmt/svc.pb.go\
		   common/proto/mgmt/storage_query.pb.go\
		   common/proto/ctl/storage.pb.go\
		   common/proto/ctl/common.pb.go\
		   common/proto/ctl/storage_nvme.pb.go\
		   common/proto/ctl/storage_scm.pb.go\
		   common/proto/ctl/system.pb.go\
		   common/proto/ctl/control.pb.go\
 		   common/proto/ctl/network.pb.go\
 		   common/proto/ctl/firmware.pb.go\
		   common/proto/srv/srv.pb.go\
		   drpc/drpc.pb.go\
		   security/auth/auth.pb.go\
		   cmd/drpc_test/hello/drpc_test.pb.go
PROTO_SOURCE_DIR = $(DAOS_ROOT)/src/proto

all: proto-go proto-c

PROTOC := $(shell which protoc 2>/dev/null)
ifeq ($(PROTOC),)
	PROTOC = "needs-install"
endif

$(PROTOC):
	@echo "Please install the protobufs compiler"
	@echo "Details: https://github.com/daos-stack/daos/blob/master/doc/dev/development.md#protobuf-compiler"
	@false

GOPATH ?= $(shell go env GOPATH)
PROTOC_GEN_GO_MOD_TAG = $(shell awk '/protobuf/ {print $$2}' $(DAOS_ROOT)/src/control/go.mod)
PROTOC_GEN_GO_TAG ?= $(PROTOC_GEN_GO_MOD_TAG)
PROTOC_GEN_GO_IMP := github.com/golang/protobuf/protoc-gen-go
PROTOC_GEN_GO := $(GOPATH)/bin/protoc-gen-go

$(PROTOC_GEN_GO): $(PROTOC)
	@echo "Installing protobuf generator for Go..."
	[ -d $(GOPATH)/src/$(PROTOC_GEN_GO_IMP) ] &&\
		cd $(GOPATH)/src/$(PROTOC_GEN_GO_IMP) &&\
		git checkout master
	go get -u $(PROTOC_GEN_GO_IMP) &&\
	cd $(GOPATH)/src/$(PROTOC_GEN_GO_IMP) &&\
	git checkout $(PROTOC_GEN_GO_TAG) &&\
	go install $(PROTOC_GEN_GO_IMP)

GO_TARGETS = $(addprefix $(DAOS_ROOT)/src/control/,$(GO_CONTROL_FILES))

proto-go: $(PROTOC_GEN_GO) $(GO_TARGETS)

$(DAOS_ROOT)/src/control/common/proto/%.pb.go: $(PROTO_SOURCE_DIR)/%.proto
	protoc -I $(dir $<) --go_out=plugins=grpc:$(dir $@) $(notdir $<)

# Some of these don't follow a regular pattern, so define
# one-off recipes for them.
$(DAOS_ROOT)/src/control/drpc/%.pb.go: $(PROTO_SOURCE_DIR)/%.proto
	protoc -I $(dir $<) --go_out=plugins=grpc:$(dir $@) $(notdir $<)

$(DAOS_ROOT)/src/control/security/acl/%.pb.go: $(PROTO_SOURCE_DIR)/security/%.proto
	protoc -I $(dir $<) --go_out=plugins=grpc:$(dir $@) $(notdir $<)

$(DAOS_ROOT)/src/control/security/auth/%.pb.go: $(PROTO_SOURCE_DIR)/security/%.proto
	protoc -I $(dir $<) --go_out=plugins=grpc:$(dir $@) $(notdir $<)

$(DAOS_ROOT)/src/control/cmd/drpc_test/hello/%.pb.go: $(PROTO_SOURCE_DIR)/test/%.proto
	protoc -I $(dir $<) --go_out=plugins=grpc:$(dir $@) $(notdir $<)

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
	protoc -I $(dir $<) --c_out=$(dir $@) $(notdir $<)

$(DAOS_ROOT)/src/%.pb-c.c: $(PROTO_SOURCE_DIR)/%.proto
	protoc -I $(dir $<) --c_out=$(dir $@) $(notdir $<)

$(DAOS_ROOT)/src/iosrv/%.pb-c.h: $(PROTO_SOURCE_DIR)/srv/%.proto
	protoc -I $(dir $<) --c_out=$(dir $@) $(notdir $<)

$(DAOS_ROOT)/src/iosrv/%.pb-c.c: $(PROTO_SOURCE_DIR)/srv/%.proto
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
