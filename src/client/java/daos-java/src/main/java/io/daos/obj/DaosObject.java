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

package io.daos.obj;

import com.google.protobuf.InvalidProtocolBufferException;
import io.daos.BufferAllocator;
import io.daos.Constants;
import io.daos.DaosEventQueue;
import io.daos.DaosIOException;
import io.daos.obj.attr.DaosObjectAttribute;
import io.netty.buffer.ByteBuf;
import org.apache.commons.lang.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.io.UnsupportedEncodingException;
import java.util.ArrayList;
import java.util.List;

/**
 * A Java object representing underlying DAOS object. It should be instantiated from {@link DaosObjClient} with
 * {@link DaosObjectId} which is encoded already.<br/>
 * Before doing any update/fetch/list, {@link #open()} method should be called first. After that, you should close this
 * object by calling {@link #close()} method.<br/>
 * For update/fetch methods, {@link IODataDesc} should be created from this object with list of entries to describe
 * which akey and which part to be updated/fetched.<br/>
 * For key list methods, {@link IOKeyDesc} should be create from this object. You have several choices to specify
 * parameters, like number of keys to list, key length and batch size. By tuning these parameters, you can list dkeys or
 * akeys efficiently with proper amount of resources. See createKD... methods inside this class.
 */
public class DaosObject {

  private DaosObjClient client;

  private long contPtr;

  private DaosObjectId oid;

  private long objectPtr = -1;

  public static final int MAX_DEBUG_SIZE = 1024 * 1024;

  public static final int MAX_EXCEPTION_SIZE = 1024;

  private static final Logger log = LoggerFactory.getLogger(DaosObject.class);

  /**
   * construct new instance of DaosObject with given <code>oid</code>.
   * <code>oid</code> must be encoded.
   *
   * @param client
   * initialized DAOS object client
   * @param oid
   * encoded DAOS object id
   */
  protected DaosObject(DaosObjClient client, DaosObjectId oid) {
    this.oid = oid;
    this.client = client;
    this.contPtr = client.getContPtr();
    if (!oid.isEncoded()) {
      throw new IllegalArgumentException("DAOS object ID should be encoded.");
    }
  }

  /**
   * open object with default mode, {@linkplain OpenMode#UNKNOWN}.
   *
   * @throws DaosObjectException
   */
  public void open() throws DaosObjectException {
    open(OpenMode.UNKNOWN);
  }

  /**
   * is object open
   *
   * @return true for object open; false for not open or open failed
   */
  public boolean isOpen() {
    return objectPtr != -1;
  }

  /**
   * open object with given <code>mode</code> if it hasn't been opened yet.
   *
   * @param mode
   * open mode, see {@link OpenMode}
   * @throws DaosObjectException
   */
  public void open(OpenMode mode) throws DaosObjectException {
    if (objectPtr == -1) {
      ByteBuf buffer = oid.getBuffer();
      try {
        objectPtr = client.openObject(contPtr, buffer.memoryAddress(), mode.getValue());
      } catch (DaosIOException e) {
        throw new DaosObjectException(oid, "failed to open object with mode " + mode, e);
      }
    }
  }

  /**
   * punch entire object.
   *
   * @throws DaosObjectException
   */
  public void punch() throws DaosObjectException {
    checkOpen();
    if (log.isDebugEnabled()) {
      log.debug("punching object " + oid);
    }
    try {
      client.punchObject(objectPtr, 0);
    } catch (DaosIOException e) {
      throw new DaosObjectException(oid, "failed to punch entire object ", e);
    }
  }

  private ByteBuf encodeKeys(List<String> keys) throws DaosObjectException {
    int bufferLen = 0;
    List<byte[]> keyBytesList = new ArrayList<>(keys.size());
    for (String key : keys) {
      if (StringUtils.isBlank(key)) {
        throw new IllegalArgumentException("one of akey is blank");
      }
      byte bytes[];
      try {
        bytes = key.getBytes(Constants.KEY_CHARSET);
      } catch (UnsupportedEncodingException e) {
        throw new DaosObjectException(oid, "failed to encode " + key + " in " + Constants.KEY_CHARSET, e);
      }
      if (bytes.length > Short.MAX_VALUE) {
        throw new IllegalArgumentException("key length in " + Constants.KEY_CHARSET +
          " should not exceed " + Short.MAX_VALUE);
      }
      keyBytesList.add(bytes);
      bufferLen += (bytes.length + Constants.ENCODED_LENGTH_KEY);
    }
    if (bufferLen == 0) {
      return null;
    }
    // encode keys to buffer
    ByteBuf buffer = BufferAllocator.objBufWithNativeOrder(bufferLen);
    for (byte[] bytes : keyBytesList) {
      buffer.writeShort(bytes.length).writeBytes(bytes);
    }
    return buffer;
  }

  private String enumKeys(List<String> keys, int maxSize) {
    StringBuilder sb = new StringBuilder();
    int nbr = 0;
    for (String dkey : keys) {
      sb.append(dkey);
      nbr++;
      if (sb.length() < maxSize) {
        sb.append(',');
      } else {
        break;
      }
    }
    if (nbr < keys.size()) {
      sb.append("...");
    }
    return sb.toString();
  }

  /**
   * punch given <code>dkeys</code>.
   *
   * @param dkeys
   * dkey list
   * @throws DaosObjectException
   */
  public void punchDkeys(List<String> dkeys) throws DaosObjectException {
    checkOpen();
    ByteBuf buffer = encodeKeys(dkeys);
    if (buffer == null) {
      throw new DaosObjectException(oid, "no dkeys specified when punch dkeys");
    }
    if (log.isDebugEnabled()) {
      log.debug("punching dkeys: " + enumKeys(dkeys, MAX_DEBUG_SIZE) + oid);
    }
    try {
      client.punchObjectDkeys(objectPtr, 0, dkeys.size(), buffer.memoryAddress(), buffer.readableBytes());
    } catch (DaosIOException e) {
      throw new DaosObjectException(oid, "failed to punch " + dkeys.size() + " dkeys: " +
        enumKeys(dkeys, MAX_EXCEPTION_SIZE), e);
    } finally {
      buffer.release();
    }
  }

  /**
   * punch given <code>akeys</code> under <code>dkeys</code>.
   *
   * @param dkey
   * distribution key
   * @param akeys
   * akey list
   * @throws DaosObjectException
   */
  public void punchAkeys(String dkey, List<String> akeys) throws DaosObjectException {
    checkOpen();
    if (akeys.isEmpty()) {
      throw new DaosObjectException(oid, "no akeys specified when punch akeys");
    }
    int nbrOfAkyes = akeys.size();
    List<String> allKeys = new ArrayList<>(nbrOfAkyes + 1);
    allKeys.add(dkey);
    allKeys.addAll(akeys);
    ByteBuf buffer = encodeKeys(allKeys);
    if (log.isDebugEnabled()) {
      log.debug("punching akeys: " + enumKeys(akeys, MAX_DEBUG_SIZE) + oid);
    }
    try {
      client.punchObjectAkeys(objectPtr, 0, nbrOfAkyes, buffer.memoryAddress(), buffer.readableBytes());
    } catch (DaosIOException e) {
      throw new DaosObjectException(oid, "failed to punch " + akeys.size() + " akeys: " +
        enumKeys(akeys, MAX_EXCEPTION_SIZE) + " under dkey " + dkey, e);
    } finally {
      buffer.release();
    }
  }

  /**
   * query attribute.
   *
   * @return object attribute
   * @throws DaosObjectException
   */
  public DaosObjectAttribute queryAttribute() throws DaosObjectException {
    checkOpen();
    if (log.isDebugEnabled()) {
      log.debug("query object attribute, " + oid);
    }
    byte[] bytes;
    try {
      bytes = client.queryObjectAttribute(objectPtr);
    } catch (DaosIOException e) {
      throw new DaosObjectException(oid, "failed to query object attribute", e);
    }
    DaosObjectAttribute attribute;
    try {
      attribute = DaosObjectAttribute.parseFrom(bytes);
    } catch (InvalidProtocolBufferException e) {
      throw new DaosObjectException(oid, "failed to de-serialized attribute", e);
    }
    return attribute;
  }

  /**
   * fetch object with given <code>desc</code>. User should get result from each entry, like below code snippet.
   * <code>
   *   for (Entry e : desc.getAkeyEntries()) {
   *     int actualSize = e.getActualSize();
   *     if (actualSize > 0 ) {
   *       byte[] bytes = new byte[actualSize];
   *       e.read(bytes) // or e.read(byteBuffer)
   *     }
   *   }
   * </code>
   *
   * @param desc
   * {@link IODataDesc} describes list of {@link IODataDesc.Entry} to fetch akeyes' data under dkey.
   * Check {@link #createDataDescForFetch(String, IODataDesc.IodType, int)} and
   * {@link IODataDesc#addEntryForFetch(String, int, int)}.
   * User should release internal buffer of <code>desc</code> by himself.
   * @throws DaosObjectException
   */
  public void fetch(IODataDesc desc) throws DaosObjectException {
    checkOpen();
    desc.encode();

    if (log.isDebugEnabled()) {
      log.debug(oid + " fetch object with description: " + desc.toString(MAX_DEBUG_SIZE));
    }
    ByteBuf descBuffer = desc.getDescBuffer();
    try {
      client.fetchObject(objectPtr, 0, desc.getNbrOfEntries(), descBuffer.memoryAddress());
      desc.parseResult();
    } catch (DaosIOException e) {
      DaosObjectException de = new DaosObjectException(oid, "failed to fetch object with description " +
        desc.toString(MAX_EXCEPTION_SIZE), e);
      desc.setCause(de);
      throw de;
    }
  }

  /**
   * Same as {@link #fetch(IODataDesc)}, but fetch object with {@link IOSimpleDataDesc}.
   *
   * @param desc
   * request and data description
   * @throws DaosObjectException
   */
  public void fetchSimple(IOSimpleDataDesc desc) throws DaosObjectException {
    checkOpen();
    desc.encode();

    if (log.isDebugEnabled()) {
      log.debug(oid + " fetch object with description: " + desc.toString(MAX_DEBUG_SIZE));
    }
    try {
      boolean async = desc.isAsync();
      client.fetchObjectSimple(objectPtr, 0, desc.getDescBuffer().memoryAddress(), async);
      if (!async) {
        desc.parseResult();
      }
    } catch (DaosIOException e) {
      DaosObjectException de = new DaosObjectException(oid, "failed to fetch object with description " +
          desc.toString(MAX_EXCEPTION_SIZE), e);
      desc.setCause(de);
      throw de;
    }
  }

  /**
   * update object with given <code>desc</code>.
   *
   * @param desc
   * {@link IODataDesc} describes list of {@link IODataDesc.Entry} to update on dkey.
   * User should release internal buffer of <code>desc</code> by himself.
   * @throws DaosObjectException
   */
  public void update(IODataDesc desc) throws DaosObjectException {
    checkOpen();
    desc.encode();

    if (log.isDebugEnabled()) {
      log.debug(oid + " update object with description: " + desc.toString(MAX_DEBUG_SIZE));
    }
    try {
      client.updateObject(objectPtr, 0, desc.getNbrOfEntries(), desc.getDescBuffer().memoryAddress());
      desc.succeed();
    } catch (DaosIOException e) {
      DaosObjectException de = new DaosObjectException(oid, "failed to update object with description " +
        desc.toString(MAX_EXCEPTION_SIZE), e);
      desc.setCause(de);
      throw de;
    }
  }

  /**
   * Same as {@link #fetch(IODataDesc)}, but fetch object with {@link IOSimpleDataDesc}.
   *
   * @param desc
   * request and data description
   * @throws DaosObjectException
   */
  public void updateSimple(IOSimpleDataDesc desc) throws DaosObjectException {
    checkOpen();
    desc.encode();

    if (log.isDebugEnabled()) {
      log.debug(oid + " update object with description: " + desc.toString(MAX_DEBUG_SIZE));
    }
    try {
      client.updateObjectSimple(objectPtr, 0, desc.getDescBuffer().memoryAddress(), desc.isAsync());
      if (!desc.isAsync()) {
        desc.succeed();
      }
    } catch (DaosIOException e) {
      DaosObjectException de = new DaosObjectException(oid, "failed to update object with description " +
          desc.toString(MAX_EXCEPTION_SIZE), e);
      desc.setCause(de);
      throw de;
    }
  }

  /**
   * list object dkeys. dkeys can also get from {@link IOKeyDesc#getResultKeys()} after this method until
   * {@link IOKeyDesc#continueList()}, which resets some fields for next key listing, is called.<br/>
   * User should check {@link IOKeyDesc#reachEnd()} to see if all keys are listed. If not reach end, You should
   * continue the listing by calling {@link IOKeyDesc#continueList()} and {@link DaosObject#listDkeys(IOKeyDesc)}
   * or {@link DaosObject#listAkeys(IOKeyDesc)}.
   *
   * @param desc
   * key description
   * @return list of dkeys
   * @throws DaosObjectException
   */
  public List<String> listDkeys(IOKeyDesc desc) throws DaosObjectException {
    checkOpen();
    desc.encode();

    if (log.isDebugEnabled()) {
      log.debug(oid + " list dkeys with description: " + desc.toString());
    }
    try {
      client.listObjectDkeys(objectPtr, desc.getDescBuffer().memoryAddress(),
        desc.getKeyBuffer().memoryAddress(), desc.getKeyBuffer().capacity(),
        desc.getAnchorBuffer().memoryAddress(),
        desc.getBatchSize());
    } catch (DaosIOException e) {
      throw new DaosObjectException(oid, "failed to list dkeys with description: " +desc, e);
    }
    try {
      return desc.parseResult();
    } catch (UnsupportedEncodingException e) {
      throw new DaosObjectException(oid, "failed to parse result of listed dkeys", e);
    }
  }

  /**
   * list object akeys. akeys can also get from {@link IOKeyDesc#getResultKeys()} after this method until
   * {@link IOKeyDesc#continueList()}, which resets some fields for next key listing, is called.<br/>
   * User should check {@link IOKeyDesc#reachEnd()} to see if all keys are listed. If not reach end, You should
   * continue the listing by calling {@link IOKeyDesc#continueList()} and {@link DaosObject#listDkeys(IOKeyDesc)}
   * or {@link DaosObject#listAkeys(IOKeyDesc)}.
   *
   * @param desc
   * @return
   * @throws DaosObjectException
   */
  public List<String> listAkeys(IOKeyDesc desc) throws DaosObjectException {
    checkOpen();
    if (StringUtils.isBlank(desc.getDkey())) {
      throw new DaosObjectException(oid, "dkey is needed when list akeys");
    }
    desc.encode();

    if (log.isDebugEnabled()) {
      log.debug(oid + " list akeys with description: " + desc.toString());
    }
    try {
      client.listObjectAkeys(objectPtr, desc.getDescBuffer().memoryAddress(),
        desc.getKeyBuffer().memoryAddress(), desc.getKeyBuffer().capacity(),
        desc.getAnchorBuffer().memoryAddress(), desc.getBatchSize());
    } catch (DaosIOException e) {
      throw new DaosObjectException(oid, "failed to list akeys with description: " +desc, e);
    }
    try {
      return desc.parseResult();
    } catch (UnsupportedEncodingException e) {
      throw new DaosObjectException(oid, "failed to parse result of listed akeys", e);
    }
  }

  /**
   * get record size of given <code>dkey</code> and <code>akey</code>.
   *
   * @param dkey
   * distribution key
   * @param akey
   * attribute key
   * @return record size. 0 if dkey or akey don't exist.
   * @throws DaosObjectException
   */
  public int getRecordSize(String dkey, String akey) throws DaosObjectException {
    checkOpen();
    if (StringUtils.isBlank(dkey)) {
      throw new IllegalArgumentException("dkey is blank");
    }
    if (StringUtils.isBlank(akey)) {
      throw new IllegalArgumentException("akey is blank");
    }
    byte dkeyBytes[];
    byte akeyBytes[];
    try {
      dkeyBytes = dkey.getBytes(Constants.KEY_CHARSET);
      if (dkeyBytes.length > Short.MAX_VALUE) {
        throw new IllegalArgumentException("dkey length in " + Constants.KEY_CHARSET +
          " should not exceed " + Short.MAX_VALUE);
      }
    } catch (UnsupportedEncodingException e) {
      throw new DaosObjectException(oid, "failed to encode dkey " + dkey + " in " + Constants.KEY_CHARSET, e);
    }
    try {
      akeyBytes = akey.getBytes(Constants.KEY_CHARSET);
      if (akeyBytes.length > Short.MAX_VALUE) {
        throw new IllegalArgumentException("akey length in " + Constants.KEY_CHARSET +
          " should not exceed " + Short.MAX_VALUE);
      }
    } catch (UnsupportedEncodingException e) {
      throw new DaosObjectException(oid, "failed to encode akey " + akey + " in " + Constants.KEY_CHARSET, e);
    }
    ByteBuf buffer = BufferAllocator.objBufWithNativeOrder(dkeyBytes.length + akeyBytes.length + 4);
    buffer.writeShort(dkeyBytes.length);
    buffer.writeBytes(dkeyBytes);
    buffer.writeShort(akeyBytes.length);
    buffer.writeBytes(akeyBytes);
    if (log.isDebugEnabled()) {
      log.debug("get record size for " + dkey + ", akey " + akey);
    }
    try {
      return client.getRecordSize(objectPtr, buffer.memoryAddress());
    } catch (DaosIOException e) {
      throw new DaosObjectException(oid, "failed to get record size for " + dkey + ", akey " + akey, e);
    } finally {
      buffer.release();
    }
  }

  private void checkOpen() throws DaosObjectException {
    if (objectPtr == -1) {
      throw new DaosObjectException(oid, "object is not open.");
    }
  }

  /**
   * close object if it's open.
   *
   * @throws DaosObjectException
   */
  public void close() throws DaosObjectException {
    if (objectPtr != -1) {
      try {
        client.closeObject(objectPtr);
      } catch (DaosIOException e) {
        throw new DaosObjectException(oid, "failed to close object", e);
      } finally {
        if (oid != null) {
          oid.release();
        }
      }
    } else {
      if (oid != null) {
        oid.release();
      }
    }
  }

  /**
   * create a new instance of {@link IODataDesc} for update
   *
   * @param dkey
   * distribution key
   * @param iodType
   * type from {@link IODataDesc.IodType}
   * @param recordSize
   * record size. Should be same record size as the first update if any. You can call
   * {@link DaosObject#getRecordSize(String, String)} to get correct value if you don't know yet.
   * @return {@link IODataDesc}
   * @throws IOException
   */
  public static IODataDesc createDataDescForUpdate(String dkey, IODataDesc.IodType iodType, int recordSize)
      throws IOException {
    IODataDesc desc = new IODataDesc(dkey, iodType, recordSize, true);
    return desc;
  }

  /**
   * create a new instance of {@link IODataDesc} for fetch
   *
   * @param dkey
   * distribution key
   * @param iodType
   * type from {@link IODataDesc.IodType}
   * @param recordSize
   * record size. Should be same record size as the first update if any. You can call
   * {@link DaosObject#getRecordSize(String, String)} to get correct value if you don't know yet.
   * @return {@link IODataDesc}
   * @throws IOException
   */
  public static IODataDesc createDataDescForFetch(String dkey, IODataDesc.IodType iodType, int recordSize)
      throws IOException {
    IODataDesc desc = new IODataDesc(dkey, iodType, recordSize, false);
    return desc;
  }

  /**
   * create reusable {@link IOSimpleDataDesc} object.
   * It's for asynchronous description if <code>eq</code> is not null.
   * Otherwise, it's for synchronous description.
   *
   * @param maxKeyStrLen
   * max key string length
   * @param nbrOfEntries
   * number of akey entries available
   * @param entryBufLen
   * entry's buffer length
   * @param eq
   * per-thread {@link DaosEventQueue} instance
   * @return IOSimpleDataDesc instance
   */
  public static IOSimpleDataDesc createSimpleDesc(int maxKeyStrLen, int nbrOfEntries, int entryBufLen,
                                                DaosEventQueue eq) {
    return new IOSimpleDataDesc(maxKeyStrLen, nbrOfEntries, entryBufLen, eq == null ? 0L : eq.getEqWrapperHdl());
  }

  public static SimpleDataDescGrp createSimpleDataDescGrp(int nbrOfDescs, int maxKeyStrLen,
                                                          int nbrOfEntries, int entryBufLen,
                                                          DaosEventQueue eq) {
    return new SimpleDataDescGrp(nbrOfDescs, maxKeyStrLen, nbrOfEntries, entryBufLen, eq);
  }

  /**
   * create reusable IODataDesc object.
   *
   * @param maxKeyLen
   * max length of akey and dkey
   * @param nbrOfEntries
   * number of akey entries
   * @param entryBufLen
   * entry buffer length
   * @param iodType
   * type from {@link IODataDesc.IodType}
   * @param recordSize
   * record size
   * @param updateOrFetch
   * true for update. false for fetch
   * @return
   */
  public IODataDesc createReusableDesc(int maxKeyLen, int nbrOfEntries, int entryBufLen,
                                      IODataDesc.IodType iodType, int recordSize, boolean updateOrFetch) {
    return new IODataDesc(maxKeyLen, nbrOfEntries, entryBufLen, iodType, recordSize, updateOrFetch);
  }

  /**
   * create reusable IODataDesc with default maxKeyLen, nbrOfEntries and entryBufLen.
   * maxKeyLen: {@linkplain IODataDesc#DEFAULT_LEN_REUSE_KEY}
   * nbrOfEntries: {@linkplain IODataDesc#DEFAULT_NUMBER_OF_ENTRIES}
   * entryBufLen: {@linkplain IODataDesc#DEFAULT_LEN_REUSE_BUFFER}.
   *
   * @param iodType
   * type from {@link IODataDesc.IodType}
   * @param recordSize
   * record size
   * @param updateOrFetch
   * true for update. false for fetch
   * @return
   */
  public IODataDesc createReusableDesc(IODataDesc.IodType iodType, int recordSize, boolean updateOrFetch) {
    return new IODataDesc(iodType, recordSize, updateOrFetch);
  }

  /**
   * create new instance of {@link IOKeyDesc} with all parameters provided.
   *
   * @param dkey
   * distribution key for listing akeys. null for listing dkeys
   * @param nbrOfKeys
   * number of keys to list. The listing could stop if <code>nbrOfKeys</code> exceeds actual number of keys
   * @param keyLen
   * approximate key length, so that buffer size can be well-calculated. It may be increased after key2big error when
   * list keys.
   * @param batchSize
   * how many keys to list per native method call
   * @return new instance of IOKeyDesc
   * @throws IOException
   */
  public IOKeyDesc createKDWithAllParams(String dkey, int nbrOfKeys, int keyLen, int batchSize)
                  throws IOException {
    return new IOKeyDesc(dkey, nbrOfKeys, keyLen, batchSize);
  }

  /**
   * create new instance of {@link IOKeyDesc} with default batch size,
   * {@linkplain Constants#KEY_LIST_BATCH_SIZE_DEFAULT}.
   *
   * @param dkey
   * distribution key for listing akeys. null for listing dkeys
   * @param nbrOfKeys
   * number of keys to list. The listing could stop if <code>nbrOfKeys</code> exceeds actual number of keys
   * @param keyLen
   * approximate key length, so that buffer size can be well-calculated. It may be increased after key2big error when
   * list keys.
   * @return new instance of IOKeyDesc
   * @throws IOException
   */
  public IOKeyDesc createKDWithDefaultBs(String dkey, int nbrOfKeys, int keyLen) throws IOException {
    return new IOKeyDesc(dkey, nbrOfKeys, keyLen);
  }

  /**
   * create new instance {@link IOKeyDesc} with number of key specified and with default key length,
   * {@linkplain Constants#KEY_LIST_LEN_DEFAULT} and batch size, {@linkplain Constants#KEY_LIST_BATCH_SIZE_DEFAULT}.
   *
   * @param dkey
   * distribution key for listing akeys. null for listing dkeys
   * @param nbrOfKeys
   * number of keys to list. The listing could stop if <code>nbrOfKeys</code> exceeds actual number of keys
   * @return new instance of IOKeyDesc
   * @throws IOException
   */
  public IOKeyDesc createKDWithNbrOfKeys(String dkey, int nbrOfKeys) throws IOException {
    return new IOKeyDesc(dkey, nbrOfKeys);
  }

  /**
   * create new instance {@link IOKeyDesc} with all defaults to list all keys (Integer.MAX_VALUE) with default key
   * length, {@linkplain Constants#KEY_LIST_LEN_DEFAULT} and batch size,
   * {@linkplain Constants#KEY_LIST_BATCH_SIZE_DEFAULT}.
   *
   * @param dkey
   * distribution key for listing akeys. null for listing dkeys
   * @return new instance of IOKeyDesc
   * @throws IOException
   */
  public IOKeyDesc createKD(String dkey) throws IOException {
    return new IOKeyDesc(dkey);
  }

  /**
   * create new instance {@link IOKeyDesc} with key length and batch size specified.
   *
   * @param dkey
   * distribution key for listing akeys. null for listing dkeys
   * @param keyLen
   * approximate key length, so that buffer size can be well-calculated
   * @param batchSize
   * how many keys to list per native method call
   * @return new instance of IOKeyDesc
   * @throws IOException
   */
  public IOKeyDesc createKDWithKlAndBs(String dkey, int keyLen, int batchSize) throws IOException {
    return new IOKeyDesc(dkey, Integer.MAX_VALUE, keyLen, batchSize);
  }

  @Override
  public String toString() {
    return "DaosObject{" +
        "client=" + client +
        ", oid=" + oid +
        '}';
  }
}
