/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.obj;

import io.netty.buffer.ByteBuf;

import java.io.IOException;
import java.util.List;

public interface IODataDesc {

  String getDkey();

  void setDkey(String dkey);

  int getNbrOfEntries();

  List<IODataDesc.Entry> getAkeyEntries();

  IODataDesc.Entry getEntry(int index);

  ByteBuf getDescBuffer();

  int getDescBufferLen();

  int getTotalRequestSize();

  int getRequestBufLen();

  void encode();

  void reuse();

  boolean isReusable();

  boolean isSucceeded();

  Throwable getCause();

  IODataDesc duplicate() throws IOException;

  void release();

  default String updateOrFetchStr(boolean v) {
    return v ? "update" : "fetch";
  }

  abstract class Entry {
    protected String key;
    protected byte[] keyBytes;
    protected long offset;
    protected int dataSize;
    protected ByteBuf dataBuffer;
    protected boolean encoded;
    protected int actualSize;

    public String getKey() {
      return key;
    }

    public int getActualSize() {
      return actualSize;
    }

    protected abstract int getDescLen();

    protected abstract void encode();

    protected void setActualSize(int actualSize) {
      this.actualSize = actualSize;
    }

    public int getRequestSize() {
      return dataSize;
    }

    public long getOffset() {
      return offset;
    }

    public ByteBuf getDataBuffer() {
      return dataBuffer;
    }

    public abstract ByteBuf getFetchedData();

    public abstract boolean isFetchBufReleased();

    public void releaseDataBuffer() {
      if (dataBuffer != null) {
        dataBuffer.release();
        dataBuffer = null;
      }
    }
  }
}
