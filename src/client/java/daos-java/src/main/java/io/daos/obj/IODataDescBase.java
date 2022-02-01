/*
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.obj;

import io.daos.DaosUtils;
import io.netty.buffer.ByteBuf;

import java.util.ArrayList;
import java.util.List;

public abstract class IODataDescBase implements IODataDesc {

  protected String dkey;

  protected byte[] dkeyBytes;

  protected final List<IODataDesc.Entry> akeyEntries;

  protected boolean updateOrFetch;

  protected int totalDescBufferLen;

  protected int totalRequestBufLen;

  protected int totalRequestSize;

  protected ByteBuf descBuffer;

  protected Throwable cause;

  protected boolean encoded;

  protected boolean resultParsed;

  protected boolean discarded;

  protected IODataDescBase(String dkey, boolean updateOrFetch) {
    this.dkey = dkey;
    if (dkey != null) {
      dkeyBytes = DaosUtils.keyToBytes8(dkey);
    }
    this.akeyEntries = new ArrayList<>();
    this.updateOrFetch = updateOrFetch;
  }

  @Override
  public String getDkey() {
    return dkey;
  }

  @Override
  public int getTotalRequestSize() {
    return totalRequestSize;
  }

  /**
   * number of records to fetch or update.
   *
   * @return number of records
   */
  @Override
  public int getNbrOfEntries() {
    return akeyEntries.size();
  }

  /**
   * total length of all encoded entries, including reserved buffer for holding sizes of returned data and actual record
   * size.
   *
   * @return total length
   */
  @Override
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

  public Throwable getCause() {
    return cause;
  }

  /**
   * get reference to the Description Buffer after being encoded.
   * The buffer's reader index and write index should be restored if user
   * changed them.
   *
   * @return ByteBuf
   */
  public ByteBuf getDescBuffer() {
    if (encoded) {
      return descBuffer;
    }
    throw new IllegalStateException("not encoded yet");
  }

  @Override
  public List<Entry> getAkeyEntries() {
    return akeyEntries;
  }

  @Override
  public BaseEntry getEntry(int index) {
    return (BaseEntry) akeyEntries.get(index);
  }

  public boolean isDiscarded() {
    return discarded;
  }

  public void discard() {
    discarded = true;
  }

  public abstract class BaseEntry extends Entry {
    /**
     * get size of actual data returned.
     *
     * @return actual data size returned
     */
    @Override
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
    @Override
    protected void setActualSize(int actualSize) {
      if (!updateOrFetch) {
        this.actualSize = actualSize;
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
    @Override
    public ByteBuf getFetchedData() {
      if (!updateOrFetch) {
        return dataBuffer;
      }
      throw new UnsupportedOperationException("only support for fetch, akey: " + key);
    }

    @Override
    public boolean isFetchBufReleased() {
      if (!updateOrFetch) {
        return encoded && (dataBuffer == null);
      }
      throw new UnsupportedOperationException("only support for fetch, akey: " + key);
    }
  }
}
