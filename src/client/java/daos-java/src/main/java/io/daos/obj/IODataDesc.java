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

import io.daos.BufferAllocator;
import io.daos.Constants;
import io.netty.buffer.ByteBuf;
import org.apache.commons.lang.StringUtils;

import java.io.IOException;
import java.io.UnsupportedEncodingException;
import java.util.ArrayList;
import java.util.List;

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
 *   See {@link Entry#release(boolean)}.
 * </p>
 * <p>
 *   For update entries, user should call {@link #createEntryForUpdate(String, IodType, int, int, ByteBuf)}.
 *   And {@link #createEntryForFetch(String, IodType, int, int, int)} for fetch entries. Results of fetch should be get
 *   from each entry by calling {@link Entry#getFetchedData()} For each IODataDesc object, there must be only one type
 *   of action, either update or fetch, among all its entries.
 * </p>
 */
public class IODataDesc {

  private String dkey;

  private byte[] dkeyBytes;

  private final int maxDkeyLen;

  private final List<Entry> akeyEntries;

  private final boolean updateOrFetch;

  private int totalDescBufferLen;

  private int totalRequestBufLen;

  private int totalRequestSize;

  private int nbrOfAkeysWithData;

  private ByteBuf descBuffer;

  private Throwable cause;

  private boolean encoded;

  private boolean resultParsed;

  public static final short DEFAULT_LEN_REUSE_DKEY = 64;

  public static final short DEFAULT_LEN_REUSE_AKEY = 64;

  public static final int DEFAULT_LEN_REUSE_BUFFER = 2 * 1024 * 1024;

  public static final int DEFAULT_NUMBER_OF_ENTRIES = 5;

  protected IODataDesc(IodType iodType, int recordSize, boolean updateOrFetch) {
    this(DEFAULT_LEN_REUSE_DKEY, DEFAULT_LEN_REUSE_AKEY, DEFAULT_NUMBER_OF_ENTRIES,
        DEFAULT_LEN_REUSE_BUFFER, iodType, recordSize, updateOrFetch);
  }

  protected IODataDesc(int maxDkeyLen, int maxAkeyLen, int nbrOfEntries, int entryBufLen,
                       IodType iodType, int recordSize, boolean updateOrFetch) {
    if (maxDkeyLen <= 0) {
      throw new IllegalArgumentException("dkey length should be positive. " + maxDkeyLen);
    }
    if (maxDkeyLen > Short.MAX_VALUE || maxAkeyLen > Short.MAX_VALUE) {
      throw new IllegalArgumentException("dkey and akey length in should not exceed "
          + Short.MAX_VALUE + ". dkey len: " + maxDkeyLen + ", akey len: " + maxAkeyLen);
    }
    if (nbrOfEntries > Short.MAX_VALUE || nbrOfEntries < 0) {
      throw new IllegalArgumentException("number of entries should be positive and no larger than " + Short.MAX_VALUE +
          ". " + nbrOfEntries);
    }
    this.maxDkeyLen = maxDkeyLen;
    // 8 for whether reusing native data desc or not
    // 2 for storing maxDkeyLen
    // 2 for actual number of entries starting from first entry having data
    totalRequestBufLen += (Constants.ENCODED_LENGTH_KEY + maxDkeyLen + 8 + 2 + 2);
    this.akeyEntries = new ArrayList<>();
    for (int i = 0; i < nbrOfEntries; i++) {
      Entry entry = updateOrFetch ? createReusableEntryForUpdate(maxAkeyLen, entryBufLen, iodType, recordSize) :
          createReusableEntryForFetch(maxAkeyLen, entryBufLen, iodType, recordSize);
      akeyEntries.add(entry);
      totalRequestBufLen += entry.getDescLen();
    }
    totalDescBufferLen += totalRequestBufLen;
    if (!updateOrFetch) { // for returned actual size and actual record size
      totalDescBufferLen += akeyEntries.size() * Constants.ENCODED_LENGTH_EXTENT * 2;
    }
    this.updateOrFetch = updateOrFetch;
  }

  /**
   * constructor with list of {@link Entry}.
   *
   * @param dkey
   * distribution key
   * @param keyEntries
   * list of description entries
   * @param updateOrFetch
   * true for update; false for fetch
   * @throws IOException
   */
  protected IODataDesc(String dkey, List<Entry> keyEntries, boolean updateOrFetch) throws IOException {
    this.maxDkeyLen = -1;
    this.dkey = dkey;
    this.dkeyBytes = dkey.getBytes(Constants.KEY_CHARSET);
    if (dkeyBytes.length > Short.MAX_VALUE) {
      throw new IllegalArgumentException("dkey length in " + Constants.KEY_CHARSET + " should not exceed "
                      + Short.MAX_VALUE);
    }
    this.akeyEntries = keyEntries;
    this.updateOrFetch = updateOrFetch;
    // 8 for whether reusing native data desc or not
    totalRequestBufLen += (Constants.ENCODED_LENGTH_KEY + dkeyBytes.length + 8);
    for (Entry entry : keyEntries) {
      if (updateOrFetch != entry.updateOrFetch) {
        throw new IllegalArgumentException("entry is " + updateOrFetchStr(entry.updateOrFetch) +". should be " +
            updateOrFetchStr(updateOrFetch));
      }
      totalRequestBufLen += entry.getDescLen();
      totalRequestSize += entry.getRequestSize();
    }

    totalDescBufferLen += totalRequestBufLen;
    if (!updateOrFetch) { // for returned actual size and actual record size
      totalDescBufferLen += keyEntries.size() * Constants.ENCODED_LENGTH_EXTENT * 2;
    }
  }

  public String getDkey() {
    return dkey;
  }

  public int getTotalRequestSize() {
    return totalRequestSize;
  }

  /**
   * duplicate this object and all its entries.
   * Do not forget to release this object and its entries.
   *
   * @return duplicated IODataDesc
   * @throws IOException
   */
  public IODataDesc duplicate() throws IOException {
    List<Entry> newEntries = new ArrayList<>(akeyEntries.size());
    for (Entry e : akeyEntries) {
      newEntries.add(e.duplicate());
    }
    return new IODataDesc(dkey, newEntries, updateOrFetch);
  }

  private String updateOrFetchStr(boolean v) {
    return v ? "update" : "fetch";
  }

  /**
   * number of records to fetch or update.
   *
   * @return number of records
   */
  public int getNbrOfEntries() {
    return akeyEntries.size();
  }

  /**
   * total length of all encoded entries, including reserved buffer for holding sizes of returned data and actual record
   * size.
   *
   * @return total length
   */
  public int getDescBufferLen() {
    return totalDescBufferLen;
  }

  /**
   * total length of all encoded entries to request data.
   *
   * @return
   */
  public int getRequestBufLen() {
    return totalRequestBufLen;
  }

  /**
   * encode dkey + entries descriptions to the Description Buffer.
   * encode entries data to Data Buffer.
   */
  public void encode() {
    if (maxDkeyLen < 0) { // not reusable
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

  private void encodeFirstTime() {
    if (!resultParsed) {
      if (encoded) {
        return;
      }
      boolean reusable = isReusable();
      if (reusable && dkey == null) {
        throw new IllegalArgumentException("please set dkey first");
      }
      this.descBuffer = BufferAllocator.objBufWithNativeOrder(getDescBufferLen());
      descBuffer.writerIndex(reusable ? (8 + 2 + 2) : 8);
      encodeDkey(reusable);
      for (Entry entry : akeyEntries) {
        entry.encode(descBuffer);
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
        descBuffer.writeShort(maxDkeyLen);
        descBuffer.writeShort(nbrOfAkeysWithData);
      } else {
        descBuffer.writeLong(-1L); // initial
      }
      descBuffer.writerIndex(lastPos);
      encoded = true;
      return;
    }
    throw new IllegalStateException("result is parsed. cannot encode again");
  }

  public void setDkey(String dkey) throws UnsupportedEncodingException {
    if (maxDkeyLen < 0) {
      throw new UnsupportedOperationException("cannot set dkey in non-reusable desc");
    }
    resultParsed = false;
    encoded = false;
    if (dkey.equals(this.dkey)) { // in case of same dkey
      return;
    }
    byte[] dkeyBytes = dkey.getBytes(Constants.KEY_CHARSET);
    if (dkeyBytes.length > maxDkeyLen) {
      throw new IllegalArgumentException("dkey length in " + Constants.KEY_CHARSET + " should not exceed max dkey len: "
          + maxDkeyLen);
    }
    this.dkey = dkey;
    this.dkeyBytes = dkeyBytes;
  }

  private void reuse() {
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
    descBuffer.writerIndex(Constants.ENCODED_LENGTH_KEY + 8 + 2 + 2 + maxDkeyLen);
    for (Entry entry : akeyEntries) {
      if (!entry.isReused()) {
        break;
      }
      entry.encode(descBuffer);
      nbrOfAkeysWithData++;
      totalRequestSize += entry.getRequestSize();
    }
    if (nbrOfAkeysWithData == 0) {
      throw new IllegalArgumentException("at least one of entries should have been reused");
    }
    int lastPos = descBuffer.writerIndex();
    descBuffer.writerIndex(8 + 2);
    descBuffer.writeShort(nbrOfAkeysWithData);
    encodeDkey(true);
    descBuffer.writerIndex(lastPos);
    encoded = true;
  }

  private void encodeDkey(boolean reusable) {
    descBuffer.writeShort(dkeyBytes.length);
    descBuffer.writeBytes(dkeyBytes);
    if (reusable) {
      int pad = maxDkeyLen - dkeyBytes.length;
      if (pad > 0) {
        descBuffer.writerIndex(descBuffer.writerIndex() + pad);
      }
    }
  }

  public boolean isReusable() {
    return maxDkeyLen > 0;
  }

  /**
   * if the object update or fetch succeeded.
   *
   * @return true or false
   */
  public boolean isSucceeded() {
    return resultParsed;
  }

  public Throwable getCause() {
    return cause;
  }

  protected void setCause(Throwable de) {
    cause = de;
  }

  protected void succeed() {
    resultParsed = true;
    for (IODataDesc.Entry entry : getAkeyEntries()) {
      entry.reused = false;
    }
  }

  /**
   * parse result after JNI call.
   */
  protected void parseResult() {
    if (!updateOrFetch) {
      if (resultParsed) {
        return;
      }
      int nbrOfReq = isReusable() ? nbrOfAkeysWithData : akeyEntries.size();
      int count = 0;
      // update actual size
      int idx = getRequestBufLen();
      descBuffer.writerIndex(descBuffer.capacity());
      for (IODataDesc.Entry entry : akeyEntries) {
        if (count < nbrOfReq) {
          descBuffer.readerIndex(idx);
          entry.setActualSize(descBuffer.readInt());
          entry.setActualRecSize(descBuffer.readInt());
          ByteBuf dataBuffer = entry.dataBuffer;
          dataBuffer.writerIndex(dataBuffer.readerIndex() + entry.actualSize);
          idx += 2 * Constants.ENCODED_LENGTH_EXTENT;
        }
        count++;
        entry.reused = false;
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
  protected ByteBuf getDescBuffer() {
    if (encoded) {
      return descBuffer;
    }
    throw new IllegalStateException("not encoded yet");
  }

  public List<Entry> getAkeyEntries() {
    return akeyEntries;
  }

  public Entry getEntry(int index) {
    return akeyEntries.get(index);
  }

  public static Entry createReusableEntryForUpdate(int maxAkeyLen, int bufferLen, IodType iodType, int recordSize) {
    return new IODataDesc.Entry(maxAkeyLen, bufferLen, iodType, recordSize, true);
  }

  public static Entry createReusableEntryForFetch(int maxAkeyLen, int bufferLen, IodType iodType, int recordSize) {
    return new IODataDesc.Entry(maxAkeyLen, bufferLen, iodType, recordSize, false);
  }

  /**
   * create data description entry for fetch.
   *
   * @param key
   * distribution key
   * @param type
   * iod type, {@see io.daos.obj.IODataDesc.IodType}
   * @param recordSize
   * record size. Should be provided with correct value. Or you may get error or wrong fetch result. You can call
   * {@link DaosObject#getRecordSize(String, String)} to get correct value if you don't know yet.
   * @param offset
   * offset inside akey from which to fetch data, should be a multiple of recordSize
   * @param dataSize
   * size of data to fetch, make it a multiple of recordSize as much as possible. zeros are padded to make actual
   * request size a multiple of recordSize.
   * @return data description entry
   * @throws IOException
   */
  public static Entry createEntryForFetch(String key, IODataDesc.IodType type, int recordSize, int offset,
                                              int dataSize) throws IOException {
    return new IODataDesc.Entry(key, type, recordSize, offset, dataSize);
  }

  /**
   * create data description entry for update.
   *
   * @param key
   * distribution key
   * @param type
   * iod type, {@see io.daos.obj.IODataDesc.IodType}
   * @param recordSize
   * record size. Should be same record size as the first update if any. You can call
   * {@link DaosObject#getRecordSize(String, String)} to get correct value if you don't know yet.
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
  public static Entry createEntryForUpdate(String key, IODataDesc.IodType type, int recordSize, int offset,
                                               ByteBuf dataBuffer) throws IOException {
    return new IODataDesc.Entry(key, type, recordSize, offset, dataBuffer);
  }

  /**
   * release all buffers created from this object and its entry objects. Be noted, the fetch data buffers are
   * released too if this desc is for fetch. If you don't want release them too early, please call
   * {@link #release(boolean)} with false as parameter.
   */
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
      descBuffer.readerIndex(0);
      descBuffer.writerIndex(descBuffer.capacity());
      long nativeDescPtr = descBuffer.readLong();
      if (hasNativeDec(nativeDescPtr)) {
        DaosObjClient.releaseDesc(nativeDescPtr);
      }
      this.descBuffer.release();
      descBuffer = null;
    }
    if (releaseFetchBuffer && !updateOrFetch) {
      akeyEntries.forEach(e -> e.releaseFetchDataBuffer());
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
  public static class Entry {
    private String key;
    private final IodType type;
    private byte[] keyBytes;
    private final int maxAkeyLen;
    private boolean reused;
    private int offset;
    private final int recordSize;
    private int dataSize;
    private int paddedDataSize;
    private ByteBuf dataBuffer;
    private final boolean updateOrFetch;

    private boolean encoded;

    private int actualSize; // to get from value buffer
    private int actualRecSize;

    /**
     * construction for reusable entry.
     *
     * @param maxAkeyLen
     * @param bufferLen
     * @param iodType
     * @param recordSize
     * @throws IOException
     */
    protected Entry(int maxAkeyLen, int bufferLen, IodType iodType, int recordSize, boolean updateOrFetch) {
      if (maxAkeyLen <= 0) {
        throw new IllegalArgumentException("max akey length must be positive value, " + maxAkeyLen);
      }
      if (maxAkeyLen > Short.MAX_VALUE) {
        throw new IllegalArgumentException("max akey length should not exceed "
            + Short.MAX_VALUE + ". " + maxAkeyLen);
      }
      this.maxAkeyLen = maxAkeyLen;
      this.dataBuffer = BufferAllocator.objBufWithNativeOrder(bufferLen);
      this.type = iodType;
      if (iodType == IodType.NONE) {
        throw new IllegalArgumentException("need valid IodType, either " + IodType.ARRAY + " or " +
            IodType.SINGLE);
      }
      this.recordSize = recordSize;
      this.updateOrFetch = updateOrFetch;
    }

    private Entry(String key, IodType type, int recordSize, int offset, int dataSize,
                    boolean updateOrFetch) throws IOException {
      this.maxAkeyLen = -1;
      if (StringUtils.isBlank(key)) {
        throw new IllegalArgumentException("key is blank");
      }
      this.key = key;
      this.type = type;
      this.keyBytes = key.getBytes(Constants.KEY_CHARSET);
      if (keyBytes.length > Short.MAX_VALUE) {
        throw new IllegalArgumentException("akey length in " + Constants.KEY_CHARSET + " should not exceed "
            + Short.MAX_VALUE + ", akey: " + key);
      }
      this.offset = offset;
      this.recordSize = recordSize;
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
      this.updateOrFetch = updateOrFetch;
    }

    /**
     * construction for fetch.
     *
     * @param key
     * akey to fetch data from
     * @param type
     * akey value type
     * @param recordSize
     * akey record size
     * @param offset
     * offset inside akey, should be a multiple of recordSize
     * @param dataSize
     * size of data to fetch, make it a multiple of recordSize as much as possible. zeros are padded to make actual
     * request size a multiple of recordSize.
     * @throws IOException
     */
    protected Entry(String key, IodType type, int recordSize, int offset, int dataSize) throws IOException {
      this(key, type, recordSize, offset, dataSize, false);
    }

    /**
     * construction for update.
     *
     * @param key
     * akey to update on
     * @param type
     * akey value type
     * @param recordSize
     * akey record size
     * @param offset
     * offset inside akey, should be a multiple of recordSize
     * @param dataBuffer
     * byte buffer (direct buffer preferred) holding data to update. make sure dataBuffer is ready for being read,
     * for example, buffer position and limit are set correctly for reading.
     * make size a multiple of recordSize as much as possible. zeros are padded to make actual request size a multiple
     * of recordSize. user should release the buffer by himself.
     * @throws IOException
     */
    protected Entry(String key, IodType type, int recordSize, int offset, ByteBuf dataBuffer) throws IOException {
      this(key, type, recordSize, offset, dataBuffer.readableBytes(), true);
      this.dataBuffer = dataBuffer;
    }

    /**
     * get size of actual data returned.
     *
     * @return actual data size returned
     */
    public int getActualSize() {
      if (!updateOrFetch) {
        return actualSize;
      }
      throw new UnsupportedOperationException("only support for fetch, akey: " + key);
    }

    /**
     * set size of actual data returned after fetch.
     *
     * @param actualSize
     */
    public void setActualSize(int actualSize) {
      if (!updateOrFetch) {
        this.actualSize = actualSize;
        return;
      }
      throw new UnsupportedOperationException("only support for fetch, akey: " + key);
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
    public int getDescLen() {
      // 11 or 19 = key len(2) + iod_type(1) + iod_size(4) + [recx idx(4) + recx nr(4)] + data buffer mem address(8)
      if (type == IodType.ARRAY) {
        return 23 + calcKeyLen();
      }
      return 15 + calcKeyLen();
    }

    private int calcKeyLen() {
      // 2 for storing maxAkeyLen
      return maxAkeyLen < 0 ? keyBytes.length : (2 + maxAkeyLen);
    }

    private boolean isReusable() {
      return maxAkeyLen > 0;
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
    public void setKey(String akey, int offset, ByteBuf buf) throws UnsupportedEncodingException {
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
    public void setKey(String akey, int offset, int fetchDataSize) throws UnsupportedEncodingException {
      this.dataBuffer.clear();
      setKey(akey, offset, this.dataBuffer, fetchDataSize);
    }

    private void setKey(String akey, int offset, ByteBuf buf, int fetchDataSize) throws UnsupportedEncodingException {
      if (StringUtils.isBlank(akey)) {
        throw new IllegalArgumentException("key is blank");
      }
      if (!akey.equals(this.key)) {
        this.key = akey;
        this.keyBytes = akey.getBytes(Constants.KEY_CHARSET);
        if (keyBytes.length > maxAkeyLen) {
          throw new IllegalArgumentException("akey length in " + Constants.KEY_CHARSET + " should not exceed "
              + maxAkeyLen + ", akey: " + key);
        }
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

    public String getKey() {
      return key;
    }

    /**
     * encode entry to the description buffer which will be decoded in native code.<br/>
     *
     * @param descBuffer
     * the description buffer
     */
    protected void encode(ByteBuf descBuffer) {
      if (maxAkeyLen < 0) {
        encodeEntryFirstTime(descBuffer);
        return;
      }
      if (!encoded) {
        encodeEntryFirstTime(descBuffer);
        return;
      }
      reuseEntry(descBuffer);
    }

    /**
     * depend on encoded of IODataDesc to protect entry from encoding multiple times.
     *
     * @param descBuffer
     */
    private void reuseEntry(ByteBuf descBuffer) {
      if (!reused) {
        throw new IllegalStateException("please set akey first");
      }
      // skip maxAkeyLen
      descBuffer.writerIndex(descBuffer.writerIndex() + 2);
      descBuffer.writeShort(keyBytes.length)
          .writeBytes(keyBytes);
      int pad = maxAkeyLen - keyBytes.length;
      if (pad > 0) {
        descBuffer.writerIndex(descBuffer.writerIndex() + pad);
      }
      // skip type and record size
      descBuffer.writerIndex(descBuffer.writerIndex() + 1 + 4);
      if (type == IodType.ARRAY) {
        descBuffer.writeInt(offset/recordSize);
        descBuffer.writeInt(paddedDataSize/recordSize);
      }
      // skip address
      descBuffer.writerIndex(descBuffer.writerIndex() + 8);
    }

    private void encodeEntryFirstTime(ByteBuf descBuffer) {
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
      int keyLen;
      byte[] bytes;
      if (keyBytes != null) {
        keyLen = keyBytes.length;
        bytes = keyBytes;
      } else { // in case of akey is not set when encode first time
        if (dataSize > 0) {
          throw new IllegalArgumentException("akey cannot be blank when it has data");
        }
        keyLen = 0;
        bytes = new byte[0];
      }
      if (reusable) {
        descBuffer.writeShort(maxAkeyLen);
      }
      descBuffer.writeShort(keyLen)
          .writeBytes(bytes);
      if (reusable) {
        int pad = maxAkeyLen - keyLen;
        if (pad > 0) {
          descBuffer.writerIndex(descBuffer.writerIndex() + pad);
        }
      }
      descBuffer.writeByte(type.value)
          .writeInt(recordSize);
      if (type == IodType.ARRAY) {
        descBuffer.writeInt(offset/recordSize);
        descBuffer.writeInt(paddedDataSize/recordSize);
      }
      descBuffer.writeLong(memoryAddress);
      encoded = true;
    }

    public void releaseFetchDataBuffer() {
      if (!updateOrFetch && dataBuffer != null) {
        dataBuffer.release();
        dataBuffer = null;
      }
    }

    public boolean isFetchBufReleased() {
      if (!updateOrFetch) {
        return encoded && (dataBuffer == null);
      }
      throw new UnsupportedOperationException("only support for fetch, akey: " + key);
    }

    public int getRequestSize() {
      return dataSize;
    }

    public int getOffset() {
      return offset;
    }

    /**
     * duplicate this object.
     * Do not forget to release this object.
     *
     * @return duplicated Entry
     * @throws IOException
     */
    public Entry duplicate() throws IOException {
      if (updateOrFetch) {
        return new Entry(key, type, recordSize, offset, dataBuffer);
      }
      return new Entry(key, type, recordSize, offset, dataSize);
    }

    public ByteBuf getDataBuffer() {
      return dataBuffer;
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
