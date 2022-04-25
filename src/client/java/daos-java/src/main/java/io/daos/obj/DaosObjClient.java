/*
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.obj;

import io.daos.*;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

/**
 * A shareable Java DAOS Object client to wrap all object related APIs.
 * {@link DaosObject} should be create from this client for calling DAOS object APIs indirectly.
 * It registers itself to shutdown manager in {@link DaosClient} to release resources in case of abnormal shutdown.
 */
public class DaosObjClient extends ShareableClient implements ForceCloseable {

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

  private void init() throws IOException {
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
   * @param contPtr
   * container handle
   * @param objectType
   * object feature bits
   * @param objectClassName
   * object type name, see {@link DaosObjectClass}
   * @param objClassHint
   * object class hint
   * @param args
   * reserved
   */
  native static void encodeObjectId(long oidBufferAddress, long contPtr, int objectType, String objectClassName,
                                           int objClassHint, int args);

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
   * @throws DaosIOException
   * {@link DaosIOException}
   */
  native long openObject(long contPtr, long oidBufferAddress, int mode) throws DaosIOException;

  /**
   * close object.
   *
   * @param objectPtr
   * handle of opened object
   * @throws DaosIOException
   * {@link DaosIOException}
   */
  native void closeObject(long objectPtr) throws DaosIOException;

  /**
   * punch an entire object with all associated with it.
   *
   * @param objectPtr
   * handle of opened object
   * @param flags
   * punch flags (currently ignored)
   * @throws DaosIOException
   * {@link DaosIOException}
   */
  native void punchObject(long objectPtr, long flags) throws DaosIOException;

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
   * @throws DaosIOException
   * {@link DaosIOException}
   */
  native void punchObjectDkeys(long objectPtr, long flags, int nbrOfDkeys, long dkeysBufferAddress, int dataLen)
      throws DaosIOException;

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
   * @throws DaosIOException
   * {@link DaosIOException}
   */
  native void punchObjectAkeys(long objectPtr, long flags, int nbrOfAkeys, long keysBufferAddress, int dataLen)
      throws DaosIOException;

  /**
   * query attributes of an object.
   *
   * @param objectPtr
   * handle of opened object
   * @return attributes serialized by protobuf. see DaosObjectAttribute.proto.
   * @throws DaosIOException
   * {@link DaosIOException}
   */
  native byte[] queryObjectAttribute(long objectPtr) throws DaosIOException;

  /**
   * allocate native simple desc struct.
   *
   * @param memoryAddress
   * memory address of desc buffer.
   * @param async
   * true or false
   */
  native static void allocateSimpleDesc(long memoryAddress, boolean async);

  /**
   * allocate <code>nbrOfDescs</code> native descs in a group.
   *
   * @param memoryAddress
   * memory address of direct buffer to hold handle of native descs.
   * @param nbrOfDescs
   * number of descriptions to allocate.
   * @return handler of desc group
   */
  native static long allocateSimDescGroup(long memoryAddress, int nbrOfDescs);

  /**
   * allocate native desc for {@link IODescUpdAsync}.
   *
   * @param memoryAddress
   */
  native static void allocateDescUpdAsync(long memoryAddress);

  /**
   * release native desc for {@link IODescUpdAsync}.
   *
   * @param nativeDescPtr
   */
  native static void releaseDescUpdAsync(long nativeDescPtr);

  /**
   * release description group allocated by {@link #allocateSimDescGroup(long, int)}.
   *
   * @param descGrpHdl
   * handle of desc group
   */
  native static void releaseSimDescGroup(long descGrpHdl);

  /**
   * fetch object records of given dkey and akeys.
   *
   * @param objectPtr
   * handle of opened object
   * @param flags
   * Fetch flags (currently ignored)
   * @param nbrOfEntries
   * number of entries in <code>descBuffer</code>
   * @param descBufferAddress
   * address of direct byte buffer holds serialized dkey and list of akeys, types, offset, record
   * sizes, index in value buffer from {@link IODataDescSync} and how many records to fetch
   * @param descBufferCap
   * desc buffer capacity
   * @throws DaosIOException
   * {@link DaosIOException}
   */
  native void fetchObject(long objectPtr, long flags, int nbrOfEntries, long descBufferAddress, int descBufferCap)
      throws DaosIOException;

  /**
   * fetch object with simple desc.
   *
   * @param objectPtr
   * handle of opened object
   * @param flags
   * Fetch flags (currently ignored)
   * @param descBufferAddress
   * address of direct byte buffer holds serialized dkey and list of akeys, offset, index in value buffer from
   * {@link IOSimpleDataDesc} and how many records to fetch
   * @param async
   * is asynchronous?
   * @throws DaosIOException
   */
  native void fetchObjectSimple(long objectPtr, long flags, long descBufferAddress, boolean async)
      throws DaosIOException;

  /**
   * fetch object asynchronously.
   *
   * @param objectPtr
   * handle of opened object
   * @param flags
   * Fetch flags (currently ignored)
   * @param descBufferAddress
   * address of direct byte buffer holds serialized dkey and list of akeys, offset, index in value buffer from
   * {@link IOSimpleDDAsync} and how many records to fetch
   * @throws DaosIOException
   */
  native void fetchObjectAsync(long objectPtr, long flags, long descBufferAddress)
      throws DaosIOException;

  /**
   * update object records of given dkey and akeys.
   *
   * @param objectPtr
   * handle of opened object
   * @param flags
   * update flags (currently ignored)
   * @param nbrOfEntries
   * number of entries in <code>descBuffer</code>
   * @param descBufferAddress
   * address of direct byte buffer holds serialized dkey and serialized list of akeys, types,
   * offset and record sizes, index in value buffer from {@link IODataDescSync} and how many records to update
   * @param descBufferCap
   * desc buffer capacity
   * @throws DaosIOException
   * {@link DaosIOException}
   */
  native void updateObject(long objectPtr, long flags, int nbrOfEntries, long descBufferAddress,
                           int descBufferCap) throws DaosIOException;

  /**
   * update object with simple desc.
   *
   * @param objectPtr
   * handle of opened object
   * @param flags
   * update flags (currently ignored)
   * @param descBufferAddress
   * address of direct byte buffer holds serialized dkey and serialized list of akeys, types,
   * offset and index in value buffer from {@link IOSimpleDataDesc} and how many records to update
   * @param async
   * is asynchronous?
   * @throws DaosIOException
   */
  native void updateObjectSimple(long objectPtr, long flags, long descBufferAddress, boolean async)
      throws DaosIOException;

  /**
   * update object asynchronously.
   *
   * @param objectPtr
   * handle of opened object
   * @param flags
   * update flags (currently ignored)
   * @param descBufferAddress
   * address of direct byte buffer holds serialized dkey and serialized list of akeys, types,
   * offset and index in value buffer from {@link IOSimpleDDAsync} and how many records to update
   * @throws DaosIOException
   */
  native void updateObjectAsync(long objectPtr, long flags, long descBufferAddress)
      throws DaosIOException;

  /**
   * update object with one entry. Dkey and Akey are described in {@link IODescUpdAsync}.
   *
   * @param objectPtr
   * @param descMemAddress
   * @param eqWrapHdl
   * @param eventId
   * @param offset
   * @param dataLen
   * @param dataMemAddress
   * @throws DaosIOException
   */
  native void updateObjNoDecode(long objectPtr, long descMemAddress, long eqWrapHdl, short eventId,
                             long offset, int dataLen, long dataMemAddress) throws DaosIOException;

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
   * @throws DaosIOException
   * {@link DaosIOException}
   */
  native void listObjectDkeys(long objectPtr, long descBufferAddress, long keyBufferAddress, int keyBufferLen,
                              long anchorBufferAddress, int nbrOfDesc) throws DaosIOException;

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
   * @throws DaosIOException
   * {@link DaosIOException}
   */
  native void listObjectAkeys(long objectPtr, long descBufferAddress, long keyBufferAddress, int keyBufferLen,
                              long anchorBufferAddress, int nbrOfDesc) throws DaosIOException;

  /**
   * get record size of given dkey and akey encoded in direct buffer with given <code>address</code>.
   *
   * @param objectPtr
   * handle of opened object
   * @param address
   * address of direct byte buffer holds dkey and akey
   * @return record size
   * @throws DaosIOException
   */
  native int getRecordSize(long objectPtr, long address) throws DaosIOException;

  /**
   * release the native IO desc identified by <code>descPtr</code>.
   *
   * @param descPtr
   * pointer of native IO desc
   */
  public static native void releaseDesc(long descPtr);

  public static native void releaseDescSimple(long nativeDescPtr);

  public long getContPtr() {
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

  @Override
  public String toString() {
    return "DaosObjClient{" + super.toString() + "}";
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
      DaosObjClientBuilder builder;
      try {
        builder = clone();
      } catch (CloneNotSupportedException e) {
        throw new IllegalStateException("clone not supported", e);
      }
      DaosObjClient objClient;
      if (!builder.shareFsClient) {
        objClient = new DaosObjClient(poolId, contId, builder);
      } else {
        //check existing client
        if (poolId == null || contId == null) {
          throw new IllegalArgumentException("need pool UUID/label and container UUID/label");
        }
        String key = poolId + contId;
        objClient = pcObjMap.get(key);
        if (objClient == null) {
          objClient = new DaosObjClient(poolId, contId, builder);
          pcObjMap.putIfAbsent(key, objClient);
        }
        objClient = pcObjMap.get(key);
      }
      synchronized (objClient) {
        objClient.init();
        objClient.incrementRef();
      }
      return objClient;
    }

    protected DaosClient buildDaosClient() throws IOException {
      return (DaosClient) super.build();
    }
  }
}
