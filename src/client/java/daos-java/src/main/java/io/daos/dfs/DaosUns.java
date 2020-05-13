/*
 * (C) Copyright 2018-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

package io.daos.dfs;

import io.daos.dfs.uns.*;
import org.apache.commons.lang.ObjectUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import sun.nio.ch.DirectBuffer;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.HashMap;
import java.util.Map;

public class DaosUns {

    private DaosUnsBuilder builder;

    private DunsAttribute attribute;

    private static final Logger log = LoggerFactory.getLogger(DaosUns.class);

    private DaosUns () {}

    public String createPath() throws IOException {
        long poolHandle = 0;

        if (attribute == null) {
            throw new IllegalStateException("DUNS attribute is not set");
        }
        byte[] bytes = attribute.toByteArray();
        ByteBuffer buffer = BufferAllocator.directBuffer(bytes.length);
        buffer.put(bytes);
        poolHandle = DaosFsClient.daosOpenPool(builder.poolUuid, builder.serverGroup,
                    builder.ranks, builder.poolFlags);
        try {
            String cuuid = DaosFsClient.dunsCreatePath(poolHandle, builder.path,
                                ((DirectBuffer)buffer).address(), bytes.length);
            log.info("UNS path {} created in pool {} and container {}",
                    builder.path, builder.poolUuid, cuuid);
            return cuuid;
        } finally {
            if (poolHandle != 0) {
                DaosFsClient.daosClosePool(poolHandle);
            }
        }
    }

    public static DunsAttribute resolvePath(String path) throws IOException{
        byte[] bytes = DaosFsClient.dunsResolvePath(path);
        return DunsAttribute.parseFrom(bytes);
    }

    public void destroyPath() throws IOException {
        long poolHandle = 0;

        poolHandle = DaosFsClient.daosOpenPool(builder.poolUuid, builder.serverGroup,
                builder.ranks, builder.poolFlags);
        try {
            DaosFsClient.dunsDestroyPath(poolHandle, builder.path);
            log.info("UNS path {} destroyed");
        } finally {
            if (poolHandle != 0) {
                DaosFsClient.daosClosePool(poolHandle);
            }
        }
    }

    public static DunsAttribute parseAttribute(String input) throws IOException {
        byte[] bytes = DaosFsClient.dunsParseAttribute(input);
        return DunsAttribute.parseFrom(bytes);
    }

    protected DunsAttribute getAttribute() {
        return attribute;
    }

    public String getPath() {
        return builder.path;
    }

    public String getPoolUuid() {
        return builder.poolUuid;
    }

    public String getContUuid() {
        return builder.contUuid;
    }

    public Layout getLayout() {
        return builder.layout;
    }

    public DaosObjectType getObjectType() {
        return builder.objectType;
    }

    public long getChunkSize() {
        return builder.chunkSize;
    }

    public boolean isOnLustre() {
        return builder.onLustre;
    }

    public PropValue getProperty(PropType type) {
        return builder.propMap.get(type);
    }

    public static class DaosUnsBuilder implements Cloneable {
        private String path;
        private String poolUuid;
        private String contUuid;
        private Layout layout = Layout.POSIX;
        private DaosObjectType objectType = DaosObjectType.OC_SX;
        private long chunkSize = Constants.FILE_DEFAULT_CHUNK_SIZE;
        private boolean onLustre;
        private Map<PropType, PropValue> propMap = new HashMap<>();
        private Properties properties;
        private int propReserved;

        private String ranks = Constants.POOL_DEFAULT_RANKS;
        private String serverGroup = Constants.POOL_DEFAULT_SERVER_GROUP;
        private int poolFlags = Constants.ACCESS_FLAG_POOL_READWRITE;

        public DaosUnsBuilder path(String path) {
            this.path = path;
            return this;
        }

        public DaosUnsBuilder poolUuid(String poolUuid) {
            this.poolUuid = poolUuid;
            return this;
        }

        public DaosUnsBuilder contUuid(String contUuid) {
            this.contUuid = contUuid;
            return this;
        }

        public DaosUnsBuilder layout(Layout layout) {
            this.layout = layout;
            return this;
        }

        public DaosUnsBuilder objectType(DaosObjectType objectType) {
            this.objectType = objectType;
            return this;
        }

        public DaosUnsBuilder chunkSize(long chunkSize) {
            if (chunkSize < 0) {
                throw new IllegalArgumentException("chunk size should be positive integer");
            }
            this.chunkSize = chunkSize;
            return this;
        }

        public DaosUnsBuilder onLustre(boolean onLustre) {
            this.onLustre = onLustre;
            return this;
        }

        public DaosUnsBuilder putEntry(PropType propType, PropValue value) {
            switch (propType) {
                case DAOS_PROP_PO_MIN:
                case DAOS_PROP_PO_MAX:
                case DAOS_PROP_CO_MIN:
                case DAOS_PROP_CO_MAX:
                    throw new IllegalArgumentException("invalid property type: " + propType);
            }
            propMap.put(propType, value);
            return this;
        }

        public DaosUnsBuilder propReserved(int propReserved) {
            this.propReserved = propReserved;
            return this;
        }

        public DaosUnsBuilder ranks(String ranks) {
            this.ranks = ranks;
            return this;
        }

        public DaosUnsBuilder serverGroup(String serverGroup) {
            this.serverGroup = serverGroup;
            return this;
        }

        public DaosUnsBuilder poolFlags(int poolFlags) {
            this.poolFlags = poolFlags;
            return this;
        }

        @Override
        public DaosUnsBuilder clone() throws CloneNotSupportedException {
            return (DaosUnsBuilder) super.clone();
        }

        public DaosUns build() {
            if (path == null) {
                throw new IllegalArgumentException("need path");
            }
            if (poolUuid == null) {
                throw new IllegalArgumentException("need pool UUID");
            }
            if (layout == Layout.UNKNOWN || layout == Layout.UNRECOGNIZED) {
                throw new IllegalArgumentException("layout should be posix or HDF5");
            }
            DaosUns duns = new DaosUns();
            duns.builder = (DaosUnsBuilder)ObjectUtils.clone(this);
            buildAttribute(duns);
            return duns;
        }

        private void buildAttribute(DaosUns duns) {
            DunsAttribute.Builder builder = DunsAttribute.newBuilder();
            builder.setPuuid(poolUuid);
            if (contUuid != null) {
                builder.setCuuid(contUuid);
            }
            builder.setLayoutType(layout);
            builder.setObjectType(objectType.name());
            builder.setChunkSize(chunkSize);
            builder.setOnLustre(onLustre);
            buildProperties(builder);
            duns.attribute = builder.build();
        }

        private void buildProperties(DunsAttribute.Builder attrBuilder) {
            if (!propMap.isEmpty()) {
                Properties.Builder builder = Properties.newBuilder();
                builder.setReserved(propReserved);
                Entry.Builder eb = Entry.newBuilder();
                for (Map.Entry<PropType, PropValue> entry : propMap.entrySet()) {
                    eb.clear();
                    eb.setType(entry.getKey()).setReserved(entry.getValue().getReserved());
                    switch (entry.getKey()) {
                        case DAOS_PROP_PO_SPACE_RB:
                        case DAOS_PROP_CO_LAYOUT_VER:
                        case DAOS_PROP_CO_LAYOUT_TYPE:
                        case DAOS_PROP_CO_CSUM_CHUNK_SIZE:
                        case DAOS_PROP_CO_CSUM_SERVER_VERIFY:
                        case DAOS_PROP_CO_CSUM:
                        case DAOS_PROP_CO_REDUN_FAC:
                        case DAOS_PROP_CO_REDUN_LVL:
                        case DAOS_PROP_CO_SNAPSHOT_MAX:
                            eb.setVal((Long)entry.getValue().getValue());
                            break;
                        case DAOS_PROP_PO_LABEL:
                        case DAOS_PROP_PO_SELF_HEAL:
                        case DAOS_PROP_PO_RECLAIM:
                        case DAOS_PROP_PO_OWNER:
                        case DAOS_PROP_PO_OWNER_GROUP:
                        case DAOS_PROP_PO_SVC_LIST:
                        case DAOS_PROP_CO_LABEL:
                        case DAOS_PROP_CO_COMPRESS:
                        case DAOS_PROP_CO_ENCRYPT:
                        case DAOS_PROP_CO_OWNER:
                        case DAOS_PROP_CO_OWNER_GROUP:
                            eb.setStr((String)entry.getValue().getValue());
                            break;
                        case DAOS_PROP_PO_ACL:
                        case DAOS_PROP_CO_ACL:
                            eb.setPval((DaosAcl)entry.getValue().getValue());
                            break;
                    }
                    builder.addEntries(eb.build());
                }
                attrBuilder.setProperties(builder.build());
            }
        }
    }

    public static class PropValue {
        private int reserved;
        private Object value;

        public PropValue(Object value, int reserved) {
            this.reserved = reserved;
            this.value = value;
        }

        public Object getValue() {
            return value;
        }

        public int getReserved() {
            return reserved;
        }
    }
}
