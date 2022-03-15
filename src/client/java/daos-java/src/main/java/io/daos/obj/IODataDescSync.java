/*
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.obj;

import io.daos.BufferAllocator;
import io.daos.Constants;
import io.daos.DaosUtils;
import io.netty.buffer.ByteBuf;

import java.io.IOException;
import java.io.UnsupportedEncodingException;

/**
 * IO description for fetching and updating object records on given dkey. Each record is described in {@link Entry}.
 * To make JNI call efficient and avoid memory fragmentation, the dkey and entries are serialized to direct buffers
 * which then de-serialized in native code.
 *
 * <p>
 *   There are two types of buffers, Description Buffer and Data Buffers. The Description Buffer holds entries
 *   description, like their akey, type, size. The Data Buffers of entries holds actual data for either update or fetch.
 *   {@link #release()} method should be called after object update or fetch. For update, user is responsible for
 *   releasing data buffers. For fetch, user can determine who release fetch buffers.
 *   See {@link Entry#releaseDataBuffer()}.
 * </p>
 * <p>
 *   For update entries, user should call {@link #addEntryForUpdate(String, long, ByteBuf)}.
 *   And {@link #addEntryForFetch(String, long, int)} for fetch entries. Results of fetch should be get
 *   from each entry by calling {@link Entry#getFetchedData()} For each IODataDesc object, there must be only one type
 *   of action, either update or fetch, among all its entries.
 * </p>
 */
public class IODataDescSync extends IODataDescBase {

  private final IodType type;

  private final int recordSize;

  private final int maxKeyLen;

  private int nbrOfAkeysWithData;

  public static final short DEFAULT_LEN_REUSE_KEY = 64;

  public static final int DEFAULT_LEN_REUSE_BUFFER = 2 * 1024 * 1024;

  public static final int DEFAULT_NUMBER_OF_ENTRIES = 5;

  /**
   * constructor for reusing with defaults.
   * key len: {@linkplain #DEFAULT_LEN_REUSE_KEY}
   * buffer len: {@linkplain #DEFAULT_LEN_REUSE_BUFFER}
   * number of entries: {@linkplain #DEFAULT_NUMBER_OF_ENTRIES}.
   *
   * @param iodType
   * iod type from {@link IodType}
   * @param recordSize
   * record size. Should be same record size as the first update if any. You can call
   * {@link DaosObject#getRecordSize(String, String)} to get correct value if you don't know yet.
   * @param updateOrFetch
   * true for update. false for fetch
   */
  protected IODataDescSync(IodType iodType, int recordSize, boolean updateOrFetch) {
    this(DEFAULT_LEN_REUSE_KEY, DEFAULT_NUMBER_OF_ENTRIES,
        DEFAULT_LEN_REUSE_BUFFER, iodType, recordSize, updateOrFetch);
  }

  /**
   * constructor for reusing with customized values of key length, number of entries
   * and buffer length.
   *
   * @param maxKeyLen
   * max length of dkey and akey
   * @param nbrOfEntries
   * number of entries/akeys
   * @param entryBufLen
   * buffer length
   * @param iodType
   * iod type from {@link IodType}
   * @param recordSize
   * record size. Should be same record size as the first update if any. You can call
   * {@link DaosObject#getRecordSize(String, String)} to get correct value if you don't know yet.
   * @param updateOrFetch
   * true for update. false for fetch
   */
  protected IODataDescSync(int maxKeyLen, int nbrOfEntries, int entryBufLen,
                           IodType iodType, int recordSize, boolean updateOrFetch) {
    super(null, updateOrFetch);
    if (maxKeyLen <= 0) {
      throw new IllegalArgumentException("dkey length should be positive. " + maxKeyLen);
    }
    if (maxKeyLen > Short.MAX_VALUE) {
      throw new IllegalArgumentException("dkey and akey length in should not exceed "
          + Short.MAX_VALUE + ". key len: " + maxKeyLen);
    }
    if (nbrOfEntries > Short.MAX_VALUE || nbrOfEntries < 0) {
      throw new IllegalArgumentException("number of entries should be positive and no larger than " + Short.MAX_VALUE +
          ". " + nbrOfEntries);
    }
    this.maxKeyLen = maxKeyLen;
    if (iodType == IodType.NONE) {
      throw new IllegalArgumentException("need valid IodType, either " + IodType.ARRAY + " or " +
        IodType.SINGLE);
    }
    this.type = iodType;
    this.recordSize = recordSize;
    // 8 for whether reusing native data desc or not
    // 2 for storing maxDkeyLen
    // 2 for actual number of entries starting from first entry having data
    // 1 for iod type
    // 4 for record size
    // 8 + 2 + 2 + 1 + 4 = 17
    totalRequestBufLen += (Constants.ENCODED_LENGTH_KEY + maxKeyLen + 17);
    for (int i = 0; i < nbrOfEntries; i++) {
      Entry entry = addReusableEntry(entryBufLen);
      totalRequestBufLen += entry.getDescLen();
      totalRequestSize += entry.getRequestSize();
    }
    totalDescBufferLen += totalRequestBufLen;
    if (!updateOrFetch) { // for returned actual size and actual record size
      totalDescBufferLen += akeyEntries.size() * Constants.ENCODED_LENGTH_EXTENT * 2;
    }
  }

  /**
   * constructor for non-reusable description.
   * User should call {@link #addEntryForFetch(String, long, int)} or
   * {@link #addEntryForUpdate(String, long, ByteBuf)} to add entries.
   * {@link #release()} should be called after it's done.
   *
   * @param dkey
   * distribution key
   * @param iodType
   * iod type from {@link IodType}
   * @param recordSize
   * record size
   * @param updateOrFetch
   * true for update; false for fetch
   * @throws IOException
   */
  protected IODataDescSync(String dkey, IodType iodType, int recordSize, boolean updateOrFetch) throws IOException {
    super(dkey, updateOrFetch);
    this.maxKeyLen = -1;
    this.dkey = dkey;
    this.dkeyBytes = DaosUtils.keyToBytes(dkey);
    if (iodType == IodType.NONE) {
      throw new IllegalArgumentException("need valid IodType, either " + IodType.ARRAY + " or " +
        IodType.SINGLE);
    }
    this.type = iodType;
    this.recordSize = recordSize;
  }

  /**
   * duplicate this object and all its entries if it's non-reusable desc.
   * Reusable desc should not call this method. Otherwise UnsupportedOperationException will be thrown.
   * Do not forget to release this object and its entries.
   *
   * @return duplicated IODataDesc
   * @throws IOException
   * @throws UnsupportedOperationException
   */
  @Override
  public IODataDescSync duplicate() throws IOException {
    if (isReusable()) {
      throw new UnsupportedOperationException("reusable desc cannot be duplicated");
    }
    IODataDescSync dup = new IODataDescSync(dkey, IodType.ARRAY, recordSize, updateOrFetch);
    for (Entry e : akeyEntries) {
      if (updateOrFetch) {
        dup.addEntryForUpdate(e.key, e.offset, e.dataBuffer);
      } else {
        dup.addEntryForFetch(e.key, e.offset, e.dataSize);
      }
    }
    return dup;
  }

  /**
   * encode dkey + entries descriptions to the Description Buffer.
   * encode entries data to Data Buffer.
   */
  @Override
  public void encode() {
    if (maxKeyLen < 0) { // not reusable
      encodeFirstTime();
      return;
    }
    // reusable
    if (descBuffer == null) {
      encodeFirstTime();
      return;
    }
    reuse();
  }

  private void calcNonReusableLen() {
    if (akeyEntries.isEmpty()) {
      throw new IllegalStateException("no entry added");
    }
    // 8 for whether reusing native data desc or not
    // 13 = 8 + 1 + 4
    totalRequestBufLen += (Constants.ENCODED_LENGTH_KEY + dkeyBytes.length + 13);
    for (Entry entry : akeyEntries) {
      totalRequestBufLen += entry.getDescLen();
      totalRequestSize += entry.getRequestSize();
    }

    totalDescBufferLen += totalRequestBufLen;
    if (!updateOrFetch) { // for returned actual size and actual record size
      totalDescBufferLen += akeyEntries.size() * Constants.ENCODED_LENGTH_EXTENT * 2;
    }
  }

  private void encodeFirstTime() {
    if (!resultParsed) {
      if (encoded) {
        return;
      }
      boolean reusable = isReusable();
      if (reusable) {
        if (dkey == null) {
          throw new IllegalArgumentException("please set dkey first");
        }
      } else {
        calcNonReusableLen(); // total length before allocating buffer
      }
      this.descBuffer = BufferAllocator.objBufWithNativeOrder(totalDescBufferLen);
      // 17 = (8 + 2 + 2 + 1 + 4)
      // 13 = 8 + 1 + 4
      descBuffer.writerIndex(reusable ? 17 : 13);
      encodeKey(dkeyBytes, reusable);
      for (Entry entry : akeyEntries) {
        entry.encode();
        if (entry.getRequestSize() > 0) {
          nbrOfAkeysWithData++;
        }
      }
      if (nbrOfAkeysWithData == 0) {
        throw new IllegalArgumentException("at least one of entries should have been reused");
      }
      int lastPos = descBuffer.writerIndex();
      descBuffer.writerIndex(0);
      if (reusable) {
        descBuffer.writeLong(0L); // initial
        descBuffer.writeShort(maxKeyLen);
        descBuffer.writeShort(nbrOfAkeysWithData);
      } else {
        descBuffer.writeLong(-1L); // initial
      }
      descBuffer.writeByte(type.value).writeInt(recordSize);
      descBuffer.writerIndex(lastPos);
      encoded = true;
      return;
    }
    throw new IllegalStateException("result is parsed. cannot encode again");
  }

  @Override
  public void setDkey(String dkey) {
    if (maxKeyLen < 0) {
      throw new UnsupportedOperationException("cannot set dkey in non-reusable desc");
    }
    resultParsed = false;
    encoded = false;
    if (dkey.equals(this.dkey)) { // in case of same dkey
      return;
    }
    byte[] dkeyBytes = DaosUtils.keyToBytes(dkey);
    this.dkey = dkey;
    this.dkeyBytes = dkeyBytes;
  }

  public void reuse() {
    if (resultParsed) {
      throw new IllegalStateException("please set dkey to reuse this desc");
    }
    if (encoded) {
      return;
    }
    descBuffer.readerIndex(0);
    descBuffer.writerIndex(descBuffer.capacity());
    long ptr = descBuffer.readLong();
    if (!hasNativeDec(ptr)) {
      throw new IllegalStateException("no native desc pointer found");
    }
    nbrOfAkeysWithData = 0;
    totalRequestSize = 0;
    // 17 = (address)8 + (maxkeylen)2 + (nbrOfAkeysWithData)2 + 1 + 4
    descBuffer.writerIndex(Constants.ENCODED_LENGTH_KEY + 17 + maxKeyLen);
    for (Entry entry : akeyEntries) {
      if (!((SyncEntry)entry).isReused()) {
        break;
      }
      entry.encode();
      nbrOfAkeysWithData++;
      totalRequestSize += entry.getRequestSize();
    }
    if (nbrOfAkeysWithData == 0) {
      throw new IllegalArgumentException("at least one of entries should have been reused");
    }
    int lastPos = descBuffer.writerIndex();
    descBuffer.writerIndex(8 + 2);
    descBuffer.writeShort(nbrOfAkeysWithData);
    // skip type and record size
    descBuffer.writerIndex(descBuffer.writerIndex() + 5);
    encodeKey(dkeyBytes, true);
    descBuffer.writerIndex(lastPos);
    encoded = true;
  }

  private void encodeKey(byte[] keyBytes, boolean reusable) {
    descBuffer.writeShort(keyBytes.length);
    descBuffer.writeBytes(keyBytes);
    if (reusable) {
      int pad = maxKeyLen - keyBytes.length;
      if (pad > 0) {
        descBuffer.writerIndex(descBuffer.writerIndex() + pad);
      }
    }
  }

  public boolean isReusable() {
    return maxKeyLen > 0;
  }

  /**
   * if the object update or fetch succeeded.
   *
   * @return true or false
   */
  @Override
  public boolean isSucceeded() {
    return resultParsed;
  }

  protected void setCause(Throwable de) {
    cause = de;
  }

  protected void parseUpdateResult() {
    resultParsed = true;
    for (Entry entry : getAkeyEntries()) {
      ((SyncEntry)entry).reused = false;
    }
  }

  /**
   * parse result after JNI call.
   */
  protected void parseFetchResult() {
    if (!updateOrFetch) {
      if (resultParsed) {
        return;
      }
      int nbrOfReq = isReusable() ? nbrOfAkeysWithData : akeyEntries.size();
      int count = 0;
      // update actual size
      int idx = getRequestBufLen();
      descBuffer.writerIndex(descBuffer.capacity());
      for (Entry entry : akeyEntries) {
        if (count < nbrOfReq) {
          descBuffer.readerIndex(idx);
          entry.setActualSize(descBuffer.readInt());
          ((SyncEntry)entry).setActualRecSize(descBuffer.readInt());
          ByteBuf dataBuffer = entry.dataBuffer;
          dataBuffer.writerIndex(dataBuffer.readerIndex() + entry.actualSize);
          idx += 2 * Constants.ENCODED_LENGTH_EXTENT;
        }
        count++;
        ((SyncEntry)entry).reused = false;
      }
      resultParsed = true;
      return;
    }
    throw new UnsupportedOperationException("only support for fetch");
  }

  /**
   * get reference to the Description Buffer after being encoded.
   * The buffer's reader index and write index should be restored if user
   * changed them.
   *
   * @return ByteBuf
   */
  @Override
  public ByteBuf getDescBuffer() {
    if (encoded) {
      return descBuffer;
    }
    throw new IllegalStateException("not encoded yet");
  }

  @Override
  public SyncEntry getEntry(int index) {
    return (SyncEntry) akeyEntries.get(index);
  }

  /**
   * create data description entry for fetch.
   *
   * @param key
   * distribution key
   * @param offset
   * offset inside akey from which to fetch data, should be a multiple of recordSize
   * @param dataSize
   * size of data to fetch, make it a multiple of recordSize as much as possible. zeros are padded to make actual
   * request size a multiple of recordSize.
   * @return data description entry
   * @throws IOException
   */
  public SyncEntry addEntryForFetch(String key, long offset,
                                              int dataSize) throws IOException {
    if (updateOrFetch) {
      throw new IllegalArgumentException("It's desc for update");
    }
    SyncEntry e = new SyncEntry(key, offset, dataSize);
    akeyEntries.add(e);
    return e;
  }

  /**
   * create data description entry for update.
   *
   * @param key
   * distribution key
   * @param offset
   * offset inside akey from which to update data, should be a multiple of recordSize
   * @param dataBuffer
   * byte buffer (direct buffer preferred) holding data to update. make sure dataBuffer is ready for being read,
   * for example, buffer position and limit are set correctly for reading.
   * make size a multiple of recordSize as much as possible. zeros are padded to make actual request size a multiple
   * of recordSize.
   * @return data description entry
   * @throws IOException
   */
  public SyncEntry addEntryForUpdate(String key, long offset, ByteBuf dataBuffer) throws IOException {
    if (!updateOrFetch) {
      throw new IllegalArgumentException("It's desc for fetch");
    }
    SyncEntry e = new SyncEntry(key, offset, dataBuffer);
    akeyEntries.add(e);
    return e;
  }

  private SyncEntry addReusableEntry(int bufferLen) {
    SyncEntry e = new SyncEntry(bufferLen);
    akeyEntries.add(e);
    return e;
  }

  /**
   * release all buffers created from this object and its entry objects. Be noted, the fetch data buffers are
   * released too if this desc is for fetch. If you don't want release them too early, please call
   * {@link #release(boolean)} with false as parameter.
   */
  @Override
  public void release() {
    release(true);
  }

  /**
   * same as {@link #release()}, but give user a choice whether release fetch buffers or not.
   *
   * @param releaseFetchBuffer
   * true to release all fetch buffers, false otherwise.
   */
  public void release(boolean releaseFetchBuffer) {
    if (descBuffer != null) {
      if (encoded) {
        descBuffer.readerIndex(0);
        descBuffer.writerIndex(descBuffer.capacity());
        long nativeDescPtr = descBuffer.readLong();
        if (hasNativeDec(nativeDescPtr)) {
          DaosObjClient.releaseDesc(nativeDescPtr);
        }
      }
      this.descBuffer.release();
      descBuffer = null;
    }
    if ((releaseFetchBuffer & !updateOrFetch) | updateOrFetch) {
      akeyEntries.forEach(e -> e.releaseDataBuffer());
      akeyEntries.clear();
    }
  }

  private boolean hasNativeDec(long nativeDescPtr) {
    return nativeDescPtr != 0L && nativeDescPtr != -1L;
  }

  @Override
  public String toString() {
    return toString(2048);
  }

  public String toString(int maxSize) {
    StringBuilder sb = new StringBuilder();
    sb.append("dkey: ").append(dkey).append(", akey entries\n");
    int nbr = 0;
    for (Entry e : akeyEntries) {
      sb.append("[").append(e.toString()).append("]");
      nbr++;
      if (sb.length() < maxSize) {
        sb.append(',');
      } else {
        break;
      }
    }
    if (nbr < akeyEntries.size()) {
      sb.append("...");
    }
    return sb.toString();
  }

  /**
   * A entry to describe record update or fetch on given akey. For array, each entry object represents consecutive
   * records of given key. Multiple entries should be created for non-consecutive records of given key.
   */
  public class SyncEntry extends BaseEntry {
    private boolean reused;
    private int paddedDataSize;
    private int actualRecSize;

    /**
     * construction for reusable entry.
     *
     * @param bufferLen
     * @throws IOException
     */
    protected SyncEntry(int bufferLen) {
      this.dataBuffer = BufferAllocator.objBufWithNativeOrder(bufferLen);
    }

    /**
     * constructor for fetch.
     *
     * @param key
     * akey to update on
     * akey record size
     * @param offset
     * offset inside akey, should be a multiple of recordSize
     * @param dataSize
     * size of data to fetch
     * @throws IOException
     */
    private SyncEntry(String key, long offset, int dataSize)
        throws IOException {
      if (DaosUtils.isBlankStr(key)) {
        throw new IllegalArgumentException("key is blank");
      }
      this.key = key;
      int limit = maxKeyLen > 0 ? maxKeyLen : Short.MAX_VALUE;
      this.keyBytes = DaosUtils.keyToBytes(key, limit);
      this.offset = offset;
      this.dataSize = dataSize;
      if (offset%recordSize != 0) {
        throw new IllegalArgumentException("offset (" + offset + ") should be a multiple of recordSize (" + recordSize +
            ")." + ", akey: " + key);
      }
      if (dataSize <= 0) {
        throw new IllegalArgumentException("data size should be positive, " + dataSize);
      }
      switch (type) {
        case SINGLE:
          if (offset != 0) {
            throw new IllegalArgumentException("offset should be zero for " + type + ", akey: " + key);
          }
          if (dataSize > recordSize) {
            throw new IllegalArgumentException("data size should be no more than record size for " + type +
                ", akey: " + key);
          }
          break;
        case NONE: throw new IllegalArgumentException("need valid IodType, either " + IodType.ARRAY + " or " +
            IodType.SINGLE + ", akey: " + key);
      }
      // pad data size and make it a multiple of record size
      int r = dataSize % recordSize;
      if (r != 0) {
        paddedDataSize = dataSize + (recordSize - r);
      } else {
        paddedDataSize = dataSize;
      }
    }

    /**
     * construction for update.
     *
     * @param key
     * akey to update on
     * @param offset
     * offset inside akey, should be a multiple of recordSize
     * @param dataBuffer
     * byte buffer (direct buffer preferred) holding data to update. make sure dataBuffer is ready for being read,
     * for example, buffer position and limit are set correctly for reading.
     * make size a multiple of recordSize as much as possible. zeros are padded to make actual request size a multiple
     * of recordSize. user should release the buffer by himself.
     * @throws IOException
     */
    protected SyncEntry(String key, long offset, ByteBuf dataBuffer) throws IOException {
      this(key, offset, dataBuffer.readableBytes());
      this.dataBuffer = dataBuffer;
    }

    /**
     * get actual record size.
     *
     * @return record size
     */
    public int getActualRecSize() {
      if (!updateOrFetch) {
        return actualRecSize;
      }
      throw new UnsupportedOperationException("only support for fetch, akey: " + key);
    }

    /**
     * set actual record size.
     *
     * @param actualRecSize
     */
    public void setActualRecSize(int actualRecSize) {
      if (!updateOrFetch) {
        this.actualRecSize = actualRecSize;
        return;
      }
      throw new UnsupportedOperationException("only support for fetch, akey: " + key);
    }

    /**
     * get data buffer holding fetched data. User should read data without changing buffer's readerIndex and writerIndex
     * since the indices are managed based on the actual data returned.
     *
     * @return data buffer with writerIndex set to existing readerIndex + actual data size
     */
    public ByteBuf getFetchedData() {
      if (!updateOrFetch) {
        return dataBuffer;
      }
      throw new UnsupportedOperationException("only support for fetch, akey: " + key);
    }

    /**
     * length of this entry when encoded into the Description Buffer.
     *
     * @return length
     */
    @Override
    public int getDescLen() {
      // 10 or 22 = key len(2) + [recx idx(8) + recx nr(4)] + data buffer mem address(8)
      if (type == IodType.ARRAY) {
        return 22 + calcKeyLen();
      }
      return 10 + calcKeyLen();
    }

    private int calcKeyLen() {
      return maxKeyLen < 0 ? keyBytes.length : maxKeyLen;
    }

    public boolean isReused() {
      return reused;
    }

    /**
     * this method should be called before reusing the data buffer.
     * The data buffer will be cleared before returning to user.
     *
     * @return reused original buffer
     */
    public ByteBuf reuseBuffer() {
      this.dataBuffer.clear();
      return this.dataBuffer;
    }

    /**
     * set Akey and its info for update.
     * User should call {@link #reuseBuffer()} before calling this method.
     *
     * @param akey
     * @param offset
     * @param buf
     * reused data buffer
     * @throws UnsupportedEncodingException
     */
    public void setKey(String akey, long offset, ByteBuf buf) throws UnsupportedEncodingException {
      if (!isReusable()) {
        throw new UnsupportedOperationException("entry is not reusable");
      }
      if (buf.readerIndex() != 0) {
        throw new IllegalArgumentException("buffer's reader index should be 0. " + buf.readerIndex());
      }
      setKey(akey, offset, buf, 0);
    }

    /**
     * set Akey and its info for fetch.
     * {@link #reuseBuffer()} is not necessary to be called since it'll be called automatically inside
     * this method.
     *
     * @param akey
     * @param offset
     * @param fetchDataSize
     * @throws UnsupportedEncodingException
     */
    public void setKey(String akey, long offset, int fetchDataSize) throws UnsupportedEncodingException {
      if (!isReusable()) {
        throw new UnsupportedOperationException("entry is not reusable");
      }
      this.dataBuffer.clear();
      setKey(akey, offset, this.dataBuffer, fetchDataSize);
    }

    private void setKey(String akey, long offset, ByteBuf buf, int fetchDataSize) throws UnsupportedEncodingException {
      if (DaosUtils.isBlankStr(akey)) {
        throw new IllegalArgumentException("key is blank");
      }
      if (!akey.equals(this.key)) {
        this.key = akey;
        this.keyBytes = DaosUtils.keyToBytes(akey, maxKeyLen);
      }
      this.offset = offset;
      if (updateOrFetch) {
        this.dataSize = buf.readableBytes();
      } else {
        this.dataSize = fetchDataSize;
        if (dataSize > buf.capacity()) {
          throw new IllegalArgumentException("data size, " + dataSize + "should not exceed buffer capacity, " +
              buf.capacity());
        }
      }
      if (dataSize <= 0) {
        throw new IllegalArgumentException("data size should be positive, " + dataSize);
      }
      if (offset%recordSize != 0) {
        throw new IllegalArgumentException("offset (" + offset + ") should be a multiple of recordSize (" + recordSize +
            ")." + ", akey: " + key);
      }
      if (buf != dataBuffer) {
        throw new IllegalArgumentException("buffer mismatch");
      }
      if (type == IodType.SINGLE) {
        if (offset != 0) {
          throw new IllegalArgumentException("offset should be zero for " + type + ", akey: " + key);
        }
        if (dataSize > recordSize) {
          throw new IllegalArgumentException("data size should be no more than record size for " + type +
              ", akey: " + key);
        }
      }
      // pad data size and make it a multiple of record size
      int r = dataSize % recordSize;
      if (r != 0) {
        paddedDataSize = dataSize + (recordSize - r);
      } else {
        paddedDataSize = dataSize;
      }
      this.reused = true;
    }

    /**
     * encode entry to the description buffer which will be decoded in native code.
     */
    @Override
    protected void encode() {
      if (maxKeyLen < 0) {
        encodeEntryFirstTime();
        return;
      }
      if (!encoded) {
        encodeEntryFirstTime();
        return;
      }
      reuseEntry();
    }

    /**
     * depend on encoded of IODataDesc to protect entry from encoding multiple times.
     */
    private void reuseEntry() {
      if (!reused) {
        throw new IllegalStateException("please set akey first");
      }
      encodeKey(keyBytes, isReusable());
      if (type == IodType.ARRAY) {
        descBuffer.writeLong(offset/recordSize);
        descBuffer.writeInt(paddedDataSize/recordSize);
      }
      // skip address
      descBuffer.writerIndex(descBuffer.writerIndex() + 8);
    }

    private void encodeEntryFirstTime() {
      if (encoded) {
        return;
      }
      long memoryAddress;
      boolean reusable = isReusable();
      if (!updateOrFetch) {
        if (!reusable) {
          dataBuffer = BufferAllocator.objBufWithNativeOrder(paddedDataSize);
        }
        memoryAddress = dataBuffer.memoryAddress();
      } else {
        memoryAddress = dataBuffer.memoryAddress() + dataBuffer.readerIndex();
      }
      byte[] bytes;
      if (keyBytes != null) {
        bytes = keyBytes;
      } else { // in case of akey is not set when encode first time
        if (dataSize > 0) {
          throw new IllegalArgumentException("akey cannot be blank when it has data");
        }
        bytes = new byte[0];
      }
      encodeKey(bytes, reusable);
      if (type == IodType.ARRAY) {
        descBuffer.writeLong(offset/recordSize);
        descBuffer.writeInt(paddedDataSize/recordSize);
      }
      descBuffer.writeLong(memoryAddress);
      encoded = true;
    }

    public boolean isFetchBufReleased() {
      if (!updateOrFetch) {
        return encoded && (dataBuffer == null);
      }
      throw new UnsupportedOperationException("only support for fetch, akey: " + key);
    }

    @Override
    public String toString() {
      StringBuilder sb = new StringBuilder();
      sb.append(updateOrFetch ? "update " : "fetch ").append("entry: ");
      sb.append(key).append('|')
        .append(type).append('|')
        .append(recordSize).append('|')
        .append(offset).append('|')
        .append(dataSize);
      return sb.toString();
    }
  }

  public enum IodType {
    NONE((byte)0), SINGLE((byte)1), ARRAY((byte)2);

    private byte value;

    IodType(byte value) {
      this.value = value;
    }

    public byte getValue() {
      return value;
    }
  }
}
