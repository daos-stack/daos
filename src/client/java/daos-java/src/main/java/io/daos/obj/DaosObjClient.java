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

import io.daos.*;
import org.apache.commons.lang.ObjectUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import sun.nio.ch.DirectBuffer;

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

//  private DataTypesSizes dtSizes = new DataTypesSizes();
//
//  private int daosIodSingleSize;
//  private int daosIodArraySize;
//  private int dIovTSize;

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
//    ByteBuffer buffer = BufferAllocator.directBuffer(8);
//    buffer.order(Constants.DEFAULT_ORDER);
//    getDataTypesSizes(((DirectBuffer)buffer).address());
//    dtSizes.parse(buffer);
//    setDaosIodSize();
    setInited(true);
    log.info("DaosObjClient for {}, {} initialized", builder.getPoolId(), builder.getContId());
  }

//  private void setDaosIodSize() {
//    dIovTSize = dtSizes.getVoidPtrSize() + 2*dtSizes.getSizeTSzie(); // daos_key_t
//    daosIodSingleSize = dIovTSize + // daos_key_t
//                    dtSizes.getIodTypeTSize() + // daos_iod_type_t
//                    8 + // daos_size_t
//                    dtSizes.getUnsignedIntSize() + // unsigned int iod_nr
//                    8; // daos_recx_t * or rx_idx field
//    daosIodArraySize = daosIodSingleSize +
//                    8; // rx_nr
//    if (log.isDebugEnabled()) {
//      StringBuilder sb = new StringBuilder();
//      sb.append("iodTypeTSize: ").append(dtSizes.getIodTypeTSize()).append("\n");
//      sb.append("sizeTSize: ").append(dtSizes.getSizeTSzie()).append("\n");
//      sb.append("unsignedIntSize: ").append(dtSizes.getUnsignedIntSize()).append("\n");
//      sb.append("voidPtrSize: ").append(dtSizes.getVoidPtrSize()).append("\n");
//      sb.append("dIovTSize: ").append(dIovTSize).append("\n");
//      sb.append("daosIodSingleSize: ").append(daosIodSingleSize).append("\n");
//      sb.append("daosIodArraySize: ").append(daosIodArraySize).append("\n");
//      log.debug(sb.toString());
//    }
//  }
//
//  public DataTypesSizes getDtSizes() {
//    return dtSizes;
//  }
//
//  public int getDaosIodSingleSize() {
//    return daosIodSingleSize;
//  }
//
//  public int getDaosIodArraySize() {
//    return daosIodArraySize;
//  }
//
//  public int getdIovTSize() {
//    return dIovTSize;
//  }

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
   * @param dataLen
   * data length in buffer
   */
  native void punchObjectDkeys(long objectPtr, long flags, int nbrOfDkeys, long dkeysBufferAddress, int dataLen);

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
   * @param dataLen
   * data length in buffer
   */
  native void punchObjectAkeys(long objectPtr, long flags, int nbrOfAkeys, long keysBufferAddress, int dataLen);

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
   * @param nbrOfDesc
   * number of description in <code>descBuffer</code>
   * @param descBufferAddress
   * address of direct byte buffer holds serialized dkey and list of {@link IODataDesc} of akeys, types, offset, record
   * sizes, index in value buffer and how many records to fetch
   * @param dataBufferAddress
   * address of direct data buffer which holds all records described in <code>descBuffer</code>. Actual fetch lengths
   * of each IODesc also updated in this buffer, like "actual len1+data1+actual len2+data2..."
   */
  native void fetchObject(long objectPtr, long flags, int nbrOfDesc, long descBufferAddress, long dataBufferAddress);

  /**
   * update object records of given dkey and akeys.
   *
   * @param objectPtr
   * handle of opened object
   * @param flags
   * update flags (currently ignored)
   * @param nbrOfDesc
   * number of description in <code>descBuffer</code>
   * @param descBufferAddress
   * address of direct byte buffer holds serialized dkey and serialized list of {@link IODataDesc} of akeys, types,
   * offset and record sizes, index in value buffer and how many records to update
   * @param dataBufferAddress
   * address of direct data buffer which holds all records described in <code>descBuffer</code>
   */
  native void updateObject(long objectPtr, long flags, int nbrOfDesc, long descBufferAddress, long dataBufferAddress);

  /**
   * list dkeys of given object.
   *
   * @param objectPtr
   * handle of opened object
   * @param descBufferAddress
   * address of description buffer
   * @param keyBufferAddress
   * address of direct byte buffer holds dkeys
   * @param keyBufferLen
   * length of keyBuffer
   * @param anchorBufferAddress
   * address of direct byte buffer holds anchor
   * @param nbrOfDesc
   * maximum number of dkeys to list. If actual number of dkeys exceed this value, user should call this method again
   * with <code>anchorBuffer</code>
   */
  native void listObjectDkeys(long objectPtr, long descBufferAddress, long keyBufferAddress, int keyBufferLen,
                              long anchorBufferAddress, int nbrOfDesc);

  /**
   * list akeys of given object and dkey.
   *
   * @param objectPtr
   * handle of opened object
   * @param descBufferAddress
   * address of description buffer, including dkey
   * @param keyBufferAddress
   * address of direct byte buffer holds akeys
   * @param keyBufferLen
   * length of keyBuffer
   * @param anchorBufferAddress
   * address of direct byte buffer holds anchor
   * @param nbrOfDesc
   * maximum number of akeys to list. If actual number of akeys exceed this value, user should call this method again
   * with <code>anchorBuffer</code>
   */
  native void listObjectAkeys(long objectPtr, long descBufferAddress, long keyBufferAddress, int keyBufferLen,
                              long anchorBufferAddress, int nbrOfDesc);

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

  public static class DataTypesSizes {
    private short voidPtrSize;
    private short sizeTSzie;
    private short iodTypeTSize;
    private short unsignedIntSize;

    void parse(ByteBuffer buffer) {
      voidPtrSize = buffer.getShort();
      sizeTSzie = buffer.getShort();
      iodTypeTSize = buffer.getShort();
      unsignedIntSize = buffer.getShort();
    }

    public short getIodTypeTSize() {
      return iodTypeTSize;
    }

    public short getSizeTSzie() {
      return sizeTSzie;
    }

    public short getUnsignedIntSize() {
      return unsignedIntSize;
    }

    public short getVoidPtrSize() {
      return voidPtrSize;
    }
  }
}
