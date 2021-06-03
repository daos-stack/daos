/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.fs.hadoop;

import io.daos.DaosEventQueue;
import io.daos.DaosIOException;
import io.daos.dfs.DaosFile;
import io.daos.dfs.IODfsDesc;
import io.netty.buffer.ByteBuf;
import org.apache.hadoop.fs.FileSystem;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

public class DaosFileSourceAsync extends DaosFileSource {

  private IODfsDesc desc;

  private DaosEventQueue eq;

  private List<DaosEventQueue.Attachment> completed = new ArrayList<>(1);

  private final static int MIN_WRITE = 1 * 1024 * 1024; // per sec
  private final static int MIN_READ = 2 * MIN_WRITE; // per sec
  private final static int TIMEOUT_MS = 100; // MILLI SEC
  private final static int SPEED_DENOMINATOR_WRITE = MIN_WRITE/TIMEOUT_MS;
  private final static int SPEED_DENOMINATOR_READ = MIN_READ/TIMEOUT_MS;

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
  }

  @Override
  public void closeMore() {
    desc.release();
  }

  @Override
  protected int doWrite(long nextWritePos) throws IOException {
    DaosEventQueue.Event event = eq.acquireEvent();
    desc.reuse();
    desc.setEvent(event);
    int len = buffer.readableBytes();
    daosFile.writeAsync(desc, nextWritePos, len);
    waitForCompletion(len, SPEED_DENOMINATOR_WRITE);
    return len;
  }

  @Override
  protected int doRead(long nextReadPos, int length) throws IOException {
    DaosEventQueue.Event event = eq.acquireEvent();
    desc.reuse();
    desc.setEvent(event);
    daosFile.readAsync(desc, nextReadPos, length);
    waitForCompletion(length, SPEED_DENOMINATOR_READ);
    return desc.getActualLength();
  }

  private void waitForCompletion(int length, int speedDenom) throws IOException {
    int limit = length/speedDenom;
    limit = limit < 3 ? 3 : limit;
    int cnt = 0;
    int nbr;
    while((nbr = eq.pollCompleted(completed, 1, TIMEOUT_MS)) == 0) {
      cnt++;
      if (cnt > limit) {
        break;
      }
    }
    if (nbr != 1) {
      throw new DaosIOException("failed to complete DAOS async op after " + (limit * TIMEOUT_MS) + "ms, desc: " + desc);
    }
    if (completed.get(0) != desc) {
      throw new DaosIOException("unexpected attachment returned expect: " + desc +
          ", \nbut actual: " + completed.get(0));
    }
    completed.clear();
  }
}
