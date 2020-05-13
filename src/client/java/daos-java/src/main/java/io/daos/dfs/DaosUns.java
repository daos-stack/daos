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

/**
 * A wrapper class of DAOS Unified Namespace. There are four DAOS UNS methods,
 * {@link #createPath()}, {@link #resolvePath(String)}, {@link #destroyPath()} and
 * {@link #parseAttribute(String)}, wrapped in this class.
 *
 * Due to complexity of DAOS UNS attribute, duns_attr_t, protobuf and c plugin, protobuf-c, are introduced to
 * pass parameters accurately and efficiently. check DunsAttribute.proto and its auto-generated classes under
 * package io.daos.dfs.uns.
 *
 * The typical usage is,
 * 1, create path
 * <code>
 *     DaosUns.DaosUnsBuilder builder = new DaosUns.DaosUnsBuilder();
 *     builder.path(file.getAbsolutePath());
 *     builder.poolId(poolId);
 *     // set more parameters
 *     ...
 *     DaosUns uns = builder.build();
 *     String cid = uns.createPath();
 * </code>
 *
 * 2, resolve path
 * <code>
 *     DunsAttribute attribute = DaosUns.resolvePath(file.getAbsolutePath());
 * </code>
 *
 * 3, check DaosUnsIT for more complex usage
 */
public class DaosUns {

    private DaosUnsBuilder builder;

    private DunsAttribute attribute;

    private static final Logger log = LoggerFactory.getLogger(DaosUns.class);

    private DaosUns () {}

    /**
     * create UNS path with info of type, pool UUID and container UUID set.
     * A new container will be created with some properties from {@link DaosUnsBuilder}.
     *
     * @return container UUID
     * @throws IOException
     */
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

    /**
     * extract and parse extended attributes from given <code>path</code>.
     *
     * @param path
     * OS file path
     * @return UNS attribute
     * @throws IOException
     */
    public static DunsAttribute resolvePath(String path) throws IOException{
        byte[] bytes = DaosFsClient.dunsResolvePath(path);
        return DunsAttribute.parseFrom(bytes);
    }

    /**
     * Destroy a container and remove the path associated with it in the UNS.
     *
     * @throws IOException
     */
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

    /**
     * parse input string to UNS attribute.
     *
     * @param input
     * attribute string
     * @return UNS attribute
     * @throws IOException
     */
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

    /**
     * A builder class to build {@link DaosUns} instance. Most of methods are same as ones
     * in {@link io.daos.dfs.DaosFsClient.DaosFsClientBuilder}, like {@link #ranks(String)},
     * {@link #serverGroup(String)}, {@link #poolFlags(int)}.
     *
     * For other methods, they are specific for DAOS UNS, like {@link #layout(Layout)} and
     * {@link #putEntry(PropType, PropValue)}. Some parameters are of types auto-generated
     * by protobuf-c.
     */
    public static class DaosUnsBuilder implements Cloneable {
        private String path;
        private String poolUuid;
        private String contUuid;
        private Layout layout = Layout.POSIX;
        private DaosObjectType objectType = DaosObjectType.OC_SX;
        private long chunkSize = Constants.FILE_DEFAULT_CHUNK_SIZE;
        private boolean onLustre;
        private Map<PropType, PropValue> propMap = new HashMap<>();
        private int propReserved;

        private String ranks = Constants.POOL_DEFAULT_RANKS;
        private String serverGroup = Constants.POOL_DEFAULT_SERVER_GROUP;
        private int poolFlags = Constants.ACCESS_FLAG_POOL_READWRITE;

        /**
         * file denoted by <code>path</code> should not exist.
         *
         * @param path
         * OS file path extended attributes associated with
         * @return this object
         */
        public DaosUnsBuilder path(String path) {
            this.path = path;
            return this;
        }

        public DaosUnsBuilder poolId(String poolUuid) {
            this.poolUuid = poolUuid;
            return this;
        }

        public DaosUnsBuilder containerId(String contUuid) {
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

        /**
         * put entry as type-value pair. For <code>value</code>, there is method
         * {@link PropValue#getValueClass(PropType)} for you to get correct value class.
         *
         * @param propType
         * enum values of {@link PropType}
         * @param value
         * value object
         * @return
         */
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

        /**
         * verify and map parameters to UNS attribute objects whose classes are auto-generated by protobuf-c.
         * Then, create {@link DaosUns} object with the UNS attribute, which is to be serialized when interact
         * with native code.
         *
         * @return {@link DaosUns} object
         */
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
                    Class<?> valueClass = PropValue.getValueClass(entry.getKey());
                    if (valueClass == Long.class) {
                        eb.setVal((Long)entry.getValue().getValue());
                    } else if (valueClass == String.class) {
                        eb.setStr((String)entry.getValue().getValue());
                    } else {
                        eb.setPval((DaosAcl)entry.getValue().getValue());
                    }
                    builder.addEntries(eb.build());
                }
                attrBuilder.setProperties(builder.build());
            }
        }
    }

    /**
     * A property value class of corresponding {@link PropType}.
     * The actual value classes can be determined by call {@link #getValueClass(PropType)}.
     * Currently, there are three value classes, {@link Long}, {@link String} and {@link DaosAcl}.
     */
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

        public static Class<?> getValueClass(PropType propType) {
            switch (propType) {
                case DAOS_PROP_PO_SPACE_RB:
                case DAOS_PROP_CO_LAYOUT_VER:
                case DAOS_PROP_CO_LAYOUT_TYPE:
                case DAOS_PROP_CO_CSUM_CHUNK_SIZE:
                case DAOS_PROP_CO_CSUM_SERVER_VERIFY:
                case DAOS_PROP_CO_CSUM:
                case DAOS_PROP_CO_REDUN_FAC:
                case DAOS_PROP_CO_REDUN_LVL:
                case DAOS_PROP_CO_SNAPSHOT_MAX:
                    return Long.class;
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
                    return String.class;
                case DAOS_PROP_PO_ACL:
                case DAOS_PROP_CO_ACL:
                    return DaosAcl.class;
                default: throw new IllegalArgumentException("no value class for " + propType);
            }
        }
    }
}
