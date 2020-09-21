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

import java.io.IOException;
import java.io.UnsupportedEncodingException;

/**
 * IO description for fetching and updating object records on given dkey. Each record is described in {@link SimpleEntry}.
 * To make JNI call efficient and avoid memory fragmentation, the dkey and entries are serialized to direct buffers
 * which then de-serialized in native code.
 *
 * <p>
 *   There are two types of buffers, Description Buffer and Data Buffers. The Description Buffer holds entries
 *   description, like their akey, type, size. The Data Buffers of entries holds actual data for either update or fetch.
 *   {@link #release()} method should be called after object update or fetch. For update, user is responsible for
 *   releasing data buffers. For fetch, user can determine who release fetch buffers.
 *   See {@link SimpleEntry#release(boolean)}.
 * </p>
 */
public class IOSimpleDataDesc {

  private String dkey;

  private boolean dkeyChanged;

  private final int maxKenLen;

  private int dkeyLen;

  private final SimpleEntry[] akeyEntries;

  private boolean updateOrFetch;

  private final DaosEventQueue.Event event;

  private final int totalDescBufferLen;

  private final int totalRequestBufLen;

  private int totalRequestSize;

  private int nbrOfAkeysToRequest;

  private final ByteBuf descBuffer;

  private Throwable cause;

  private boolean resultParsed;

  private final boolean async;

  private boolean released;

  private int retCode;

  public static final int RET_CODE_SUCCEEDED = 0;

  protected IOSimpleDataDesc(int maxKeyStrLen, int nbrOfEntries, int entryBufLen, DaosEventQueue.Event event) {
    if (maxKeyStrLen > Short.MAX_VALUE/2 || maxKeyStrLen <= 0) {
      throw new IllegalArgumentException("number of entries should be positive and no larger than " +
          Short.MAX_VALUE/2 + ". " + maxKeyStrLen);
    }
    this.maxKenLen = maxKeyStrLen * 2; // 2 bytes per string character
    if (nbrOfEntries > Short.MAX_VALUE || nbrOfEntries < 0) {
      throw new IllegalArgumentException("number of entries should be positive and no larger than " + Short.MAX_VALUE +
          ". " + nbrOfEntries);
    }
    this.event = event;
    this.async = event != null;
    // 8 for storing native desc pointer
    // 2 for storing maxKenLen
    // 2 for number of entries
    // 2 for dkey length
    // 2 for actual number of entries starting from first entry having data
    int tmpLen = (16 + maxKenLen);
    // 8 for native EQ pointer
    // 2 for event ID
    tmpLen += async ? 10 : 0;
    this.akeyEntries = new SimpleEntry[nbrOfEntries];
    for (int i = 0; i < nbrOfEntries; i++) {
      SimpleEntry entry = new SimpleEntry(entryBufLen);
      akeyEntries[i] = entry;
      tmpLen += entry.getDescLen();
    }
    totalRequestBufLen = tmpLen;
    // for rc and returned actual size
    tmpLen += 4 + akeyEntries.length * Constants.ENCODED_LENGTH_EXTENT;
    totalDescBufferLen = tmpLen;
    this.descBuffer = BufferAllocator.objBufWithNativeOrder(totalDescBufferLen);
    prepareNativeDesc();
    if (!async) {
      // native desc handle written to the start of descBuffer in native code.
      DaosObjClient.allocateSimpleDesc(descBuffer.memoryAddress(), false);
      checkNativeDesc();
    } // group allocate native desc otherwise
    this.updateOrFetch = true; // default to update
  }

  private void checkLen(int len, String keyType) {
    if (len > maxKenLen) {
      throw new IllegalArgumentException(keyType + " length should not exceed "
          + maxKenLen/2 + ". " + len/2);
    }
  }

  protected void checkNativeDesc() {
    descBuffer.readerIndex(0);
    if (descBuffer.readLong() == 0L) {
      throw new IllegalStateException("no native desc created");
    }
  }

  public void setUpdateOrFetch(boolean updateOrFetch) {
    this.updateOrFetch = updateOrFetch;
  }

  public boolean isUpdateOrFetch() {
    return updateOrFetch;
  }

  public String getDkey() {
    return dkey;
  }

  public int getTotalRequestSize() {
    return totalRequestSize;
  }

  public boolean isAsync() {
    return async;
  }

  /**
   * duplicate this object and all its entries.
   * Do not forget to release this object and its entries.
   *
   * @return duplicated IODataDesc
   * @throws IOException
   */
//  public IODataDescSimple duplicate() throws IOException {
//    List<Entry> newEntries = new ArrayList<>(akeyEntries.size());
//    for (Entry e : akeyEntries) {
//      newEntries.add(e.duplicate());
//    }
//    return new IODataDescSimple(dkey, newEntries, updateOrFetch);
//  }

  private String updateOrFetchStr(boolean v) {
    return v ? "update" : "fetch";
  }

  /**
   * number of records to fetch or update.
   *
   * @return number of records
   */
  public int getNbrOfEntries() {
    return akeyEntries.length;
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

  public void setDkey(String dkey) throws UnsupportedEncodingException {
    this.dkey = dkey;
    this.dkeyLen = dkey.length() * 2;
    checkLen(dkeyLen, "dkey");
    dkeyChanged = true;
  }

  /**
   * encode dkey + entries descriptions to the Description Buffer.
   * encode entries data to Data Buffer.
   */
  public void encode() {
    if (nbrOfAkeysToRequest == 0) {
      throw new IllegalArgumentException("at least one of entries should have data");
    }
    descBuffer.readerIndex(0);
    descBuffer.writerIndex(20);
    if (async) { // assuming same event queue
      descBuffer.writeShort(event.id);
    }
    if (dkeyChanged) {
      descBuffer.writeShort(dkeyLen);
      writeKey(dkey);
    } else {
      descBuffer.writerIndex(descBuffer.writerIndex() + 2 + maxKenLen);
    }
    descBuffer.writeShort(nbrOfAkeysToRequest);
    int count = 0;
    for (SimpleEntry entry : akeyEntries) {
      if (!entry.isReused()) {
        break;
      }
      entry.encode(descBuffer);
      count++;
    }
    if (nbrOfAkeysToRequest > count) {
      throw new IllegalStateException("number of akeys to request " + nbrOfAkeysToRequest + ", should not exceed " +
          "total reused entries, " + count);
    }
  }

  private void prepareNativeDesc() {
    // skip native handle
    descBuffer.writeLong(0L);
    descBuffer.writeShort(maxKenLen);
    descBuffer.writeShort(akeyEntries.length);
    if (async) { // for asynchronous
      descBuffer.writeLong(event.eqHandle);
      // skip event id
      descBuffer.writerIndex(descBuffer.writerIndex() + 2);
    }
    // skip dkeylen, dkey and nbr of requests
    descBuffer.writerIndex(descBuffer.writerIndex() + 2 + maxKenLen + 2);
    for (SimpleEntry entry : akeyEntries) {
      entry.putAddress(descBuffer);
    }
  }

  private void writeKey(String dkey) {
    int pos = descBuffer.writerIndex();
    for (int i = 0; i < dkey.length(); i++) {
      descBuffer.writeShort(dkey.charAt(i));
    }
    descBuffer.writerIndex(pos + maxKenLen);
  }

  public void reuse() {
    this.resultParsed = false;
    this.nbrOfAkeysToRequest = 0;
    this.totalRequestSize = 0;
    this.dkeyChanged = false;
    this.retCode = Integer.MAX_VALUE;
    for (SimpleEntry e : akeyEntries) {
      e.reused = false;
      e.akeyChanged = false;
    }
  }

  /**
   * if the object update or fetch succeeded.
   *
   * @return true or false
   */
  public boolean isSucceeded() {
    return retCode == RET_CODE_SUCCEEDED;
  }

  public Throwable getCause() {
    return cause;
  }

  public int getRetCode() {
    return retCode;
  }

  protected void setCause(Throwable de) {
    cause = de;
  }

  protected void succeed() {
    if (async) {
      descBuffer.writerIndex(descBuffer.capacity());
      descBuffer.readerIndex(totalRequestBufLen);
      retCode = descBuffer.readInt();
    } else {
      retCode = RET_CODE_SUCCEEDED;
    }
    resultParsed = true;
  }

  /**
   * parse result after JNI call.
   */
  protected void parseResult() {
    if (!updateOrFetch) {
      if (resultParsed) {
        return;
      }
      int nbrOfReq = nbrOfAkeysToRequest;
      int count = 0;
      // update actual size
      int idx = totalRequestBufLen;
      descBuffer.writerIndex(descBuffer.capacity());
      retCode = descBuffer.readInt();
      if (retCode != RET_CODE_SUCCEEDED) {
        resultParsed = true;
        return;
      }
      for (SimpleEntry entry : akeyEntries) {
        if (count < nbrOfReq) {
          descBuffer.readerIndex(idx);
          entry.setActualSize(descBuffer.readInt());
          ByteBuf dataBuffer = entry.dataBuffer;
          dataBuffer.writerIndex(dataBuffer.readerIndex() + entry.actualSize);
          idx += Constants.ENCODED_LENGTH_EXTENT;
          continue;
        }
        break;
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
    return descBuffer;
  }

  public SimpleEntry[] getAkeyEntries() {
    return akeyEntries;
  }

  public SimpleEntry getEntry(int index) {
    return akeyEntries[index];
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
    if (!released) {
      if (!async) {
        descBuffer.readerIndex(0);
        descBuffer.writerIndex(descBuffer.capacity());
        long nativeDescPtr = descBuffer.readLong();
        if (hasNativeDec(nativeDescPtr)) {
          DaosObjClient.releaseDescSimple(nativeDescPtr);
        }
      }
      this.descBuffer.release();
      this.released = true;
    }
    if (updateOrFetch || releaseFetchBuffer) {
      for (SimpleEntry entry : akeyEntries) {
        entry.releaseBuffer();
      }
    }
  }

  private boolean hasNativeDec(long nativeDescPtr) {
    return nativeDescPtr != 0L;
  }

  @Override
  public String toString() {
    return toString(2048);
  }

  public String toString(int maxSize) {
    StringBuilder sb = new StringBuilder();
    sb.append("dkey: ").append(dkey).append(", akey entries\n");
    int nbr = 0;
    for (SimpleEntry e : akeyEntries) {
      sb.append("[").append(e.toString()).append("]");
      nbr++;
      if (sb.length() < maxSize) {
        sb.append(',');
      } else {
        break;
      }
    }
    if (nbr < akeyEntries.length) {
      sb.append("...");
    }
    return sb.toString();
  }

  /**
   * A entry to describe record update or fetch on given akey. For array, each entry object represents consecutive
   * records of given key. Multiple entries should be created for non-consecutive records of given key.
   */
  public class SimpleEntry {
    private String akey;
    private boolean akeyChanged;
    private int akeyLen;
    private int offset;
    private boolean reused;
    private int dataSize;
    private ByteBuf dataBuffer;

    private int actualSize; // to get from value buffer

    /**
     * construction for reusable entry.
     *
     * @param bufferLen
     * @throws IOException
     */
    protected SimpleEntry(int bufferLen) {
      this.dataBuffer = BufferAllocator.objBufWithNativeOrder(bufferLen);
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
      throw new UnsupportedOperationException("only support for fetch, akey: " + akey);
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
      throw new UnsupportedOperationException("only support for fetch, akey: " + akey);
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
      throw new UnsupportedOperationException("only support for fetch, akey: " + akey);
    }

    /**
     * length of this entry when encoded into the Description Buffer.
     *
     * @return length
     */
    public int getDescLen() {
      // 18 = dkey len(2) + recx idx(4) + recx nr(4) + data buffer mem address(8)
      return 18 + maxKenLen;
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
     *  null for reusing existing akey.
     * @param offset
     * @param buf
     * reused data buffer
     * @throws UnsupportedEncodingException
     */
    public void setEntryForUpdate(String akey, int offset, ByteBuf buf) {
      if (buf.readerIndex() != 0) {
        throw new IllegalArgumentException("buffer's reader index should be 0. " + buf.readerIndex());
      }
      setEntry(akey, offset, buf, 0);
    }

    /**
     * set Akey and its info for fetch.
     * {@link #reuseBuffer()} is not necessary to be called since it'll be called automatically inside
     * this method.
     *
     * @param akey
     * null for reusing existing akey.
     * @param offset
     * @param fetchDataSize
     * @throws UnsupportedEncodingException
     */
    public void setEntryForFetch(String akey, int offset, int fetchDataSize) {
      this.dataBuffer.clear();
      setEntry(akey, offset, this.dataBuffer, fetchDataSize);
    }

    private void setEntry(String akey, int offset, ByteBuf buf, int fetchDataSize) {
      if (akey != null) {
        setAkey(akey);
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
      if (buf != dataBuffer) {
        throw new IllegalArgumentException("buffer mismatch");
      }
      nbrOfAkeysToRequest++;
      totalRequestSize += dataSize;
      this.reused = true;
    }

    private void setAkey(String akey) {
      this.akey = akey;
      this.akeyLen = akey.length() * 2;
      checkLen(akeyLen, "akey");
      this.akeyChanged = true;
    }

    public String getAkey() {
      return akey;
    }

    /**
     * encode entry to the description buffer which will be decoded in native code.<br/>
     *
     * @param descBuffer
     * the description buffer
     */
    protected void encode(ByteBuf descBuffer) {
      if (akeyChanged) {
        descBuffer.writeShort(akeyLen);
        writeKey(akey);
      } else {
        descBuffer.writerIndex(descBuffer.writerIndex() + 2 + maxKenLen);
      }
      descBuffer.writeInt(offset);
      descBuffer.writeInt(dataSize);
      // skip memory address
      descBuffer.writerIndex(descBuffer.writerIndex() + 8);
    }

    /**
     * depend on encoded of IODataDesc to protect entry from encoding multiple times.
     *
     * @param descBuffer
     */
    private void reuseEntry(ByteBuf descBuffer) {

    }

    private void putAddress(ByteBuf descBuffer) {
      // skip akeylen, akey, offset and length
      descBuffer.writerIndex(descBuffer.writerIndex() + maxKenLen + 10);
      descBuffer.writeLong(dataBuffer.memoryAddress());
    }

    public void releaseBuffer() {
      if (dataBuffer != null) {
        dataBuffer.release();
        dataBuffer = null;
      }
    }

    public boolean isFetchBufReleased() {
      if (!updateOrFetch) {
        return dataBuffer == null;
      }
      throw new UnsupportedOperationException("only support for fetch, akey: " + akey);
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
//    public Entry duplicate() throws IOException {
//      if (updateOrFetch) {
//        return new Entry(key, type, recordSize, offset, dataBuffer);
//      }
//      return new Entry(key, type, recordSize, offset, dataSize);
//    }

    public ByteBuf getDataBuffer() {
      return dataBuffer;
    }

    @Override
    public String toString() {
      StringBuilder sb = new StringBuilder();
      sb.append(updateOrFetch ? "update " : "fetch ").append("entry: ");
      sb.append(akey).append('|')
        .append(offset).append('|')
        .append(dataSize);
      return sb.toString();
    }
  }
}
