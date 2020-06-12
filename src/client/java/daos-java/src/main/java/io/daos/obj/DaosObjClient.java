/*
 * (C) Copyright 2018-2019 Intel Corporation.
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

package io.daos.obj;

import io.daos.DaosClient;
import io.daos.DaosObjectType;
import io.daos.ForceCloseable;
import io.daos.SharableClient;
import org.apache.commons.lang.ObjectUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

/**
 * A sharable Java DAOS Object client to wrap all object related APIs.
 * {@link DaosObject} and {@link DaosObjectId} should be create from this client.
 * It registers itself to shutdown manager in {@link DaosClient} to release resources in case of abnormal shutdown.
 */
public class DaosObjClient extends SharableClient implements ForceCloseable {

  private long contPtr;

  // keyed by poolId+contId
  private static final Map<String, DaosObjClient> pcObjMap = new ConcurrentHashMap<>();

  private static final Logger log = LoggerFactory.getLogger(DaosObjClient.class);

  static {
    DaosClient.initClient();
  }

  private DaosObjClient(String poolId, String contId, DaosObjClientBuilder builder) {
    super(poolId, contId, builder);
  }

  private synchronized void init() throws IOException {
    if (isInited()) {
      return;
    }
    DaosObjClientBuilder builder = getBuilder();
    setClient(builder.buildDaosClient());
    DaosClient client = getClient();
    contPtr = client.getContPtr();
    client.registerForShutdown(this);
    setInited(true);
    log.info("DaosObjClient for {}, {} initialized", builder.getPoolId(), builder.getContId());
  }

  /**
   * get a DAOS object with given <code>DaosObjectId</code>.
   *
   * @param oid
   * DAOS object id, either encoded or not
   * @return a instance of {@link DaosObject}
   */
  public DaosObject getObject(DaosObjectId oid) {
    if (!oid.isEncoded()) {
      throw new IllegalArgumentException("DAOS object ID should be encoded.");
    }
    return new DaosObject(this, oid);
  }

  /**
   * encode object id with object feature bits and object type.
   * encoded object id is set back to <code>buffer</code>.
   *
   * @param oidBufferAddress
   * address of direct byte buffer with original object id's high and low. encode object id is set back to this buffer.
   * @param feats
   * object feature bits
   * @param objectTypeName
   * object type name, see {@link DaosObjectType}
   * @param args
   * reserved
   */
  native static void encodeObjectId(long oidBufferAddress, int feats, String objectTypeName,
                                           int args);

  /**
   * open object denoted by object id stored in <code>buffer</code>.
   *
   * @param contPtr
   * opened container handler
   * @param oidBufferAddress
   * address of direct byte buffer with original object id's high and low
   * @param mode
   * open mode, see {@link OpenMode}
   * @return handle of opened object
   * @throws IOException
   * {@link io.daos.DaosIOException}
   */
  native long openObject(long contPtr, long oidBufferAddress, int mode) throws IOException;

  /**
   * close object.
   *
   * @param objectPtr
   * handle of opened object
   * @throws IOException
   * {@link io.daos.DaosIOException}
   */
  native void closeObject(long objectPtr) throws IOException;

  /**
   * punch an entire object with all associated with it.
   *
   * @param objectPtr
   * handle of opened object
   * @param flags
   * punch flags (currently ignored)
   */
  native void punchObject(long objectPtr, long flags);

  /**
   * punch dkeys (with all its akeys) from an object.
   *
   * @param objectPtr
   * handle of opened object
   * @param flags
   * punch flags (currently ignored)
   * @param nbrOfDkeys
   * number of dkeys
   * @param dkeysBufferAddress
   * address of direct byte buffer into which dkeys written in format, len1+key1+len2+key2...
   */
  native void punchObjectDkeys(long objectPtr, long flags, int nbrOfDkeys, long dkeysBufferAddress, int bufferLen);

  /**
   * punch akeys (with all records) from an object.
   * 
   * @param objectPtr
   * handle of opened object
   * @param flags
   * punch flags (currently ignored)
   * @param nbrOfAkeys
   * number of akeys
   * @param keysBufferAddress
   * address of direct byte buffer dkey and akeys written into direct byte buffer in format,
   * dkey len+dkey+akey1 len+akey1+akey2 len+akey2...
   */
  native void punchObjectAkeys(long objectPtr, long flags, int nbrOfAkeys, long keysBufferAddress);

  /**
   * query attributes of an object.
   *
   * @param objectPtr
   * handle of opened object
   * @return attributes serialized by protobuf. see DaosObjectAttribute.proto.
   */
  native byte[] queryObjectAttribute(long objectPtr);

  /**
   * fetch object records of given dkey and akeys.
   *
   * @param objectPtr
   * handle of opened object
   * @param flags
   * Fetch flags (currently ignored)
   * @param dkey
   * Distribution key associated with the fetch operation
   * @param nbrOfDesc
   * number of description in <code>descBuffer</code>
   * @param descBufferAddress
   * address of direct byte buffer holds serialized list of {@link IODesc} of akeys, types, record sizes and how many
   * records to fetch
   * @param dataBufferAddress
   * address of direct data buffer which holds all records described in <code>descBuffer</code>. Actual fetch lengths
   * of each IODesc also updated in this buffer
   */
  native void fetchObject(long objectPtr, long flags, String dkey, int nbrOfDesc, long descBufferAddress,
                                        long dataBufferAddress);

  /**
   * update object records of given dkey and akeys.
   *
   * @param objectPtr
   * handle of opened object
   * @param flags
   * update flags (currently ignored)
   * @param dkey
   * Distribution key associated with the update operation
   * @param nbrOfDesc
   * number of description in <code>descBuffer</code>
   * @param descBufferAddress
   * address of direct byte buffer holds serialized list of {@link IODesc} of akeys, types, record sizes and how many
   * records to update
   * @param dataBufferAddress
   * address of direct data buffer which holds all records described in <code>descBuffer</code>
   */
  native void updateObject(long objectPtr, long flags, String dkey, int nbrOfDesc, long descBufferAddress,
                                        long dataBufferAddress);

  /**
   * list dkeys of given object.
   *
   * @param objectPtr
   * handle of opened object
   * @param keyBufferAddress
   * address of direct byte buffer holds dkeys
   * @param anchorBufferAddress
   * address of direct byte buffer holds anchor
   * @param maxNbr
   * maximum number of dkeys to list. If actual number of dkeys exceed this value, user should call this method again
   * with <code>anchorBuffer</code>
   */
  native void listDkeys(long objectPtr, long keyBufferAddress, long anchorBufferAddress, int maxNbr);

  /**
   * list akeys of given object and dkey.
   *
   * @param objectPtr
   * handle of opened object
   * @param dkey
   * distribution key
   * @param keyBufferAddress
   * address of direct byte buffer holds akeys
   * @param anchorBufferAddress
   * address of direct byte buffer holds anchor
   * @param maxNbr
   * maximum number of akeys to list. If actual number of akeys exceed this value, user should call this method again
   * with <code>anchorBuffer</code>
   */
  native void listAkeys(long objectPtr, String dkey, long keyBufferAddress, long anchorBufferAddress,
                                      int maxNbr);

  protected long getContPtr() {
    return contPtr;
  }

  @Override
  protected synchronized void disconnect(boolean force) throws IOException {
    decrementRef();
    DaosObjClientBuilder builder = getBuilder();
    if ((force || getRefCnt() <= 0) && isInited()) {
      if (force) {
        getClient().forceClose();
      } else {
        getClient().close();
      }
      log.info("DaosObjClient for {}, {} disconnected", builder.getPoolId(), builder.getContId());
    }
    setInited(false);
    pcObjMap.remove(builder.getPoolId() + builder.getContId());
  }

  @Override
  protected DaosObjClientBuilder getBuilder() {
    return (DaosObjClientBuilder)super.getBuilder();
  }

  public static class DaosObjClientBuilder extends DaosClient.DaosClientBuilder<DaosObjClientBuilder> {
    private boolean shareFsClient;

    /**
     * share {@link DaosObjClient} instance or not.
     *
     * @param shareFsClient
     * default is true
     * @return DaosFsClientBuilder
     */
    public DaosObjClientBuilder shareFsClient(boolean shareFsClient) {
      this.shareFsClient = shareFsClient;
      return this;
    }

    @Override
    public DaosObjClientBuilder clone() throws CloneNotSupportedException {
      return (DaosObjClientBuilder) super.clone();
    }

    @Override
    public DaosObjClient build() throws IOException {
      String poolId = getPoolId();
      String contId = getContId();
      DaosObjClientBuilder builder = (DaosObjClientBuilder) ObjectUtils.clone(this);
      DaosObjClient objClient;
      if (!builder.shareFsClient) {
        objClient = new DaosObjClient(poolId, contId, builder);
      } else {
        //check existing client
        if (poolId == null) {
          throw new IllegalArgumentException("need pool UUID.");
        }
        if (contId == null) {
          throw new IllegalArgumentException("need container UUID.");
        }
        String key = poolId + contId;
        objClient = pcObjMap.get(key);
        if (objClient == null) {
          objClient = new DaosObjClient(poolId, contId, builder);
          pcObjMap.putIfAbsent(key, objClient);
        }
        objClient = pcObjMap.get(key);
      }
      objClient.init();
      objClient.incrementRef();
      return objClient;
    }

    protected DaosClient buildDaosClient() throws IOException {
      return (DaosClient) super.build();
    }
  }
}
