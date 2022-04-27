/*
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.obj;

import com.google.protobuf.InvalidProtocolBufferException;
import io.daos.*;
import io.daos.obj.attr.DaosObjectAttribute;
import io.netty.buffer.ByteBuf;
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
 * For update/fetch methods, {@link IODataDescSync} should be created from this object with list of entries to describe
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
      client.punchObject(objectPtr, 0L);
    } catch (DaosIOException e) {
      throw new DaosObjectException(oid, "failed to punch entire object ", e);
    }
  }

  private ByteBuf encodeKeys(List<String> keys) {
    int bufferLen = 0;
    List<byte[]> keyBytesList = new ArrayList<>(keys.size());
    for (String key : keys) {
      if (DaosUtils.isBlankStr(key)) {
        throw new IllegalArgumentException("one of akey is blank");
      }
      byte bytes[] = DaosUtils.keyToBytes(key);
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
      client.punchObjectDkeys(objectPtr, 0L, dkeys.size(), buffer.memoryAddress(), buffer.readableBytes());
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
      client.punchObjectAkeys(objectPtr, 0L, nbrOfAkyes, buffer.memoryAddress(), buffer.readableBytes());
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
   * {@link IODataDescSync} describes list of {@link IODataDescSync.Entry} to fetch akeyes' data under dkey.
   * Check {@link #createDataDescForFetch(String, IODataDescSync.IodType, int)} and
   * {@link IODataDescSync#addEntryForFetch(String, long, int)}.
   * User should release internal buffer of <code>desc</code> by himself.
   * @throws DaosObjectException
   */
  public void fetch(IODataDescSync desc) throws DaosObjectException {
    checkOpen();
    desc.encode();

    if (log.isDebugEnabled()) {
      log.debug(oid + " fetch object with description: " + desc.toString(MAX_DEBUG_SIZE));
    }
    ByteBuf descBuffer = desc.getDescBuffer();
    try {
      client.fetchObject(objectPtr, 0L, desc.getNbrOfEntries(), descBuffer.memoryAddress(),
          descBuffer.capacity());
      desc.parseFetchResult();
    } catch (DaosIOException e) {
      DaosObjectException de = new DaosObjectException(oid, "failed to fetch object with description " +
        desc.toString(MAX_EXCEPTION_SIZE), e);
      desc.setCause(de);
      throw de;
    }
  }

  /**
   * Same as {@link #fetch(IODataDescSync)}, but fetch object with {@link IOSimpleDataDesc}.
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
      client.fetchObjectSimple(objectPtr, 0L, desc.getDescBuffer().memoryAddress(), async);
      if (!async) {
        desc.parseFetchResult();
      }
    } catch (DaosIOException e) {
      DaosObjectException de = new DaosObjectException(oid, "failed to fetch object with description " +
          desc.toString(MAX_EXCEPTION_SIZE), e);
      desc.setCause(de);
      throw de;
    }
  }

  /**
   * Same as {@link #fetch(IODataDescSync)}, but fetch object with {@link IOSimpleDDAsync}.
   *
   * @param desc
   * request and data description
   * @throws DaosObjectException
   */
  public void fetchAsync(IOSimpleDDAsync desc) throws DaosObjectException {
    checkOpen();
    desc.encode();

    if (log.isDebugEnabled()) {
      log.debug(oid + " fetch object with description: " + desc.toString(MAX_DEBUG_SIZE));
    }
    try {
      client.fetchObjectAsync(objectPtr, 0L, desc.getDescBuffer().memoryAddress());
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
   * {@link IODataDescSync} describes list of {@link IODataDescSync.Entry} to update on dkey.
   * User should release internal buffer of <code>desc</code> by himself.
   * @throws DaosObjectException
   */
  public void update(IODataDescSync desc) throws DaosObjectException {
    checkOpen();
    desc.encode();

    if (log.isDebugEnabled()) {
      log.debug(oid + " update object with description: " + desc.toString(MAX_DEBUG_SIZE));
    }
    try {
      client.updateObject(objectPtr, 0L, desc.getNbrOfEntries(), desc.getDescBuffer().memoryAddress(),
          desc.getDescBuffer().capacity());
      desc.parseUpdateResult();
    } catch (DaosIOException e) {
      DaosObjectException de = new DaosObjectException(oid, "failed to update object with description " +
        desc.toString(MAX_EXCEPTION_SIZE), e);
      desc.setCause(de);
      throw de;
    }
  }

  /**
   * Same as {@link #update(IODataDescSync)}, but update object with {@link IOSimpleDataDesc}.
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
      client.updateObjectSimple(objectPtr, 0L, desc.getDescBuffer().memoryAddress(), desc.isAsync());
      if (!desc.isAsync()) {
        desc.parseUpdateResult();
      }
    } catch (DaosIOException e) {
      DaosObjectException de = new DaosObjectException(oid, "failed to update object with description " +
          desc.toString(MAX_EXCEPTION_SIZE), e);
      desc.setCause(de);
      throw de;
    }
  }

  /**
   * Same as {@link #update(IODataDescSync)}, but update object with {@link IOSimpleDDAsync}.
   *
   * @param desc
   * request and data description
   * @throws DaosObjectException
   */
  public void updateAsync(IOSimpleDDAsync desc) throws DaosObjectException {
    checkOpen();
    desc.encode();

    if (log.isDebugEnabled()) {
      log.debug(oid + " update object with description: " + desc.toString(MAX_DEBUG_SIZE));
    }
    try {
      client.updateObjectAsync(objectPtr, 0L, desc.getDescBuffer().memoryAddress());
    } catch (DaosIOException e) {
      DaosObjectException de = new DaosObjectException(oid, "failed to update object with description " +
          desc.toString(MAX_EXCEPTION_SIZE), e);
      desc.setCause(de);
      throw de;
    }
  }

  /**
   * update with {@link IODescUpdAsync}.
   *
   * @param desc
   * @throws DaosObjectException
   */
  public void updateAsync(IODescUpdAsync desc)
      throws DaosObjectException {
    checkOpen();

    if (log.isDebugEnabled()) {
      log.debug(oid + " update object with description " + desc);
    }
    try {
      client.updateObjNoDecode(objectPtr, desc.descMemoryAddress(), desc.getEqHandle(), desc.getEventId(),
          desc.getDestOffset(), desc.readableBytes(), desc.dataMemoryAddress());
    } catch (DaosIOException e) {
      throw new DaosObjectException(oid, "failed to update object with description " + desc,
          e);
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
    if (DaosUtils.isBlankStr(desc.getDkey())) {
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
    if (DaosUtils.isBlankStr(dkey)) {
      throw new IllegalArgumentException("dkey is blank");
    }
    if (DaosUtils.isBlankStr(akey)) {
      throw new IllegalArgumentException("akey is blank");
    }
    byte dkeyBytes[] = DaosUtils.keyToBytes(dkey);
    byte akeyBytes[] = DaosUtils.keyToBytes(akey);
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

  public DaosObjectId getOid() {
    return oid;
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
   * create a new instance of {@link IODataDescSync} for update.
   *
   * @param dkey
   * distribution key
   * @param iodType
   * type from {@link IODataDescSync.IodType}
   * @param recordSize
   * record size. Should be same record size as the first update if any. You can call
   * {@link DaosObject#getRecordSize(String, String)} to get correct value if you don't know yet.
   * @return {@link IODataDescSync}
   * @throws IOException
   */
  public static IODataDescSync createDataDescForUpdate(String dkey, IODataDescSync.IodType iodType, int recordSize)
      throws IOException {
    IODataDescSync desc = new IODataDescSync(dkey, iodType, recordSize, true);
    return desc;
  }

  /**
   * create a new instance of {@link IODataDescSync} for fetch.
   *
   * @param dkey
   * distribution key
   * @param iodType
   * type from {@link IODataDescSync.IodType}
   * @param recordSize
   * record size. Should be same record size as the first update if any. You can call
   * {@link DaosObject#getRecordSize(String, String)} to get correct value if you don't know yet.
   * @return {@link IODataDescSync}
   * @throws IOException
   */
  public IODataDescSync createDataDescForFetch(String dkey, IODataDescSync.IodType iodType, int recordSize)
      throws IOException {
    IODataDescSync desc = new IODataDescSync(dkey, iodType, recordSize, false);
    return desc;
  }

  /**
   * create a new instance of {@link IOSimpleDDAsync} for update.
   *
   * @param dkey
   * distribution key
   * @param eqHandle
   * handle of event queue
   * @return {@link IOSimpleDDAsync}
   * @throws IOException
   */
  public IOSimpleDDAsync createAsyncDataDescForUpdate(String dkey, long eqHandle) throws IOException {
    return new IOSimpleDDAsync(dkey, true, eqHandle);
  }

  /**
   * create a new instance of {@link IOSimpleDDAsync} for fetch.
   *
   * @param dkey
   * distribution key
   * @param eqHandle
   * handle of event queue
   * @return {@link IOSimpleDDAsync}
   * @throws IOException
   */
  public IOSimpleDDAsync createAsyncDataDescForFetch(String dkey, long eqHandle) throws IOException {
    return new IOSimpleDDAsync(dkey, false, eqHandle);
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
   * type from {@link IODataDescSync.IodType}
   * @param recordSize
   * record size
   * @param updateOrFetch
   * true for update. false for fetch
   * @return
   */
  public static IODataDescSync createReusableDesc(int maxKeyLen, int nbrOfEntries, int entryBufLen,
                                           IODataDescSync.IodType iodType, int recordSize, boolean updateOrFetch) {
    return new IODataDescSync(maxKeyLen, nbrOfEntries, entryBufLen, iodType, recordSize, updateOrFetch);
  }

  /**
   * create reusable IODataDesc with default maxKeyLen, nbrOfEntries and entryBufLen.
   * maxKeyLen: {@linkplain IODataDescSync#DEFAULT_LEN_REUSE_KEY}
   * nbrOfEntries: {@linkplain IODataDescSync#DEFAULT_NUMBER_OF_ENTRIES}
   * entryBufLen: {@linkplain IODataDescSync#DEFAULT_LEN_REUSE_BUFFER}.
   *
   * @param iodType
   * type from {@link IODataDescSync.IodType}
   * @param recordSize
   * record size
   * @param updateOrFetch
   * true for update. false for fetch
   * @return
   */
  public static IODataDescSync createReusableDesc(IODataDescSync.IodType iodType, int recordSize, boolean updateOrFetch) {
    return new IODataDescSync(iodType, recordSize, updateOrFetch);
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
  public static IOKeyDesc createKDWithAllParams(String dkey, int nbrOfKeys, int keyLen, int batchSize)
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
  public static IOKeyDesc createKDWithDefaultBs(String dkey, int nbrOfKeys, int keyLen) throws IOException {
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
  public static IOKeyDesc createKDWithNbrOfKeys(String dkey, int nbrOfKeys) throws IOException {
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
  public static IOKeyDesc createKD(String dkey) throws IOException {
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
  public static IOKeyDesc createKDWithKlAndBs(String dkey, int keyLen, int batchSize) throws IOException {
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
