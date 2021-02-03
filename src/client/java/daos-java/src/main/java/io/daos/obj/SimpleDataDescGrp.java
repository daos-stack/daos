/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.obj;

import io.daos.BufferAllocator;
import io.daos.DaosEventQueue;
import io.netty.buffer.ByteBuf;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * Group of {@link IOSimpleDataDesc} to create and release multiple descs efficiently.
 *
 * {@link #release()} should be called after usage to avoid memory leak.
 */
public class SimpleDataDescGrp {

  private final int nbrOfDescs;
  private final int maxKeyStrLen;
  private final int nbrOfEntries;
  private final int entryBufLen;
  private final DaosEventQueue eq;
  private final List<IOSimpleDataDesc> list = new ArrayList<>();

  private final long descGrpHdl;

  private List<IOSimpleDataDesc> unmodifiableList;

  protected SimpleDataDescGrp(int nbrOfDescs, int maxKeyStrLen, int nbrOfEntries, int entryBufLen, DaosEventQueue eq) {
    if (eq == null) {
      throw new IllegalArgumentException("DaosEventQueue is null");
    }
    this.nbrOfDescs = nbrOfDescs;
    this.maxKeyStrLen = maxKeyStrLen;
    this.nbrOfEntries = nbrOfEntries;
    this.entryBufLen = entryBufLen;
    this.eq = eq;
    descGrpHdl = createDescs();
  }

  private long createDescs() {
    ByteBuf buf = BufferAllocator.objBufWithNativeOrder((nbrOfDescs) * 8);
    for (int i = 0; i < nbrOfDescs; i++) {
      IOSimpleDataDesc desc = DaosObject.createSimpleDesc(maxKeyStrLen, nbrOfEntries, entryBufLen, eq);
      buf.writeLong(desc.getDescBuffer().memoryAddress());
      list.add(desc);
    }
    unmodifiableList = Collections.unmodifiableList(list);
    try {
      long grpHdl = DaosObjClient.allocateSimDescGroup(buf.memoryAddress(), nbrOfDescs);
      return grpHdl;
    } finally {
      buf.release();
    }
  }

  public List<IOSimpleDataDesc> getDescList() {
    return unmodifiableList;
  }

  public void release() {
    list.forEach(d -> d.release());
    list.clear();
    unmodifiableList = null;
    DaosObjClient.releaseSimDescGroup(descGrpHdl);
  }
}
