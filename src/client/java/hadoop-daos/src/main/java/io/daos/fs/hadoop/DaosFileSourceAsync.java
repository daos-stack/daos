/*
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.fs.hadoop;

import io.daos.Constants;
import io.daos.DaosEventQueue;
import io.daos.DaosIOException;
import io.daos.dfs.DaosFile;
import io.daos.dfs.IODfsDesc;
import io.netty.buffer.ByteBuf;
import org.apache.hadoop.fs.FileSystem;

import java.io.IOException;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

public class DaosFileSourceAsync extends DaosFileSource {

  private IODfsDesc desc;

  private DaosEventQueue eq;

  private Set<IODfsDesc> candidates = new HashSet<>();

  private List<DaosEventQueue.Attachment> completed = new ArrayList<>(1);

  private final static int TIMEOUT_MS = Integer.valueOf(System.getProperty(Constants.CFG_DAOS_TIMEOUT,
      Constants.DEFAULT_DAOS_TIMEOUT_MS)); // MILLI SEC

  public DaosFileSourceAsync(DaosFile daosFile, int bufCapacity, long fileLen, boolean readOrWrite,
                             FileSystem.Statistics stats) {
    super(daosFile, bufCapacity, fileLen, stats);
    createDesc(readOrWrite);
  }

  public DaosFileSourceAsync(DaosFile daosFile, ByteBuf buffer, long fileLen,
                             boolean readOrWrite, FileSystem.Statistics stats) {
    super(daosFile, buffer, fileLen, stats);
    createDesc(readOrWrite);
  }

  private void createDesc(boolean readOrWrite) {
    try {
      eq = DaosEventQueue.getInstance(0);
    } catch (IOException e) {
      buffer.release();
      throw new IllegalStateException("failed to get EQ", e);
    }
    desc = daosFile.createDfsDesc(buffer, eq);
    desc.setReadOrWrite(readOrWrite);
    candidates.add(desc);
  }

  @Override
  public void closeMore() {
    desc.release();
  }

  @Override
  protected int doWrite(long nextWritePos) throws IOException {
    assert Thread.currentThread().getId() == eq.getThreadId() : "current thread " + Thread.currentThread().getId() +
        "(" + Thread.currentThread().getName() + "), is not expected " + eq.getThreadId() + "(" +
        eq.getThreadName() + ")";

    DaosEventQueue.Event event = eq.acquireEventBlocking(TIMEOUT_MS, completed, IODfsDesc.class, candidates);
    desc.reuse();
    desc.setEvent(event);
    int len = buffer.readableBytes();
    daosFile.writeAsync(desc, nextWritePos, len);
    waitForCompletion();
    return len;
  }

  @Override
  protected int doRead(long nextReadPos, int length) throws IOException {
    assert Thread.currentThread().getId() == eq.getThreadId() : "current thread " + Thread.currentThread().getId() +
        "(" + Thread.currentThread().getName() + "), is not expected " + eq.getThreadId() + "(" +
        eq.getThreadName() + ")";

    DaosEventQueue.Event event = eq.acquireEventBlocking(TIMEOUT_MS, completed, IODfsDesc.class, candidates);
    desc.reuse();
    desc.setEvent(event);
    daosFile.readAsync(desc, nextReadPos, length);
    waitForCompletion();
    return desc.getActualLength();
  }

  private void waitForCompletion() throws IOException {
    completed.clear();
    long start = System.currentTimeMillis();
    long dur;
    while (completed.isEmpty() & ((dur = (System.currentTimeMillis() - start)) < TIMEOUT_MS)) {
      eq.pollCompleted(completed, IODfsDesc.class, candidates, 1, TIMEOUT_MS - dur);
    }
    if (completed.isEmpty()) {
      desc.discard();
      throw new DaosIOException("failed to get expected return after waiting " + TIMEOUT_MS + " ms. desc: " + desc +
          ", candidates size: " + candidates.size() + ", dur: " + (System.currentTimeMillis() - start));
    }
  }
}
