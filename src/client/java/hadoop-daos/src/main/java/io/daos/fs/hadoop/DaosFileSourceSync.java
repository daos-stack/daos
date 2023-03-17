/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.fs.hadoop;

import io.daos.dfs.DaosFile;
import io.netty.buffer.ByteBuf;
import org.apache.hadoop.fs.FileSystem;

import java.io.IOException;

public class DaosFileSourceSync extends DaosFileSource {

  public DaosFileSourceSync(DaosFile daosFile, int bufCapacity, long fileLen, boolean append,
                            FileSystem.Statistics stats) {
    super(daosFile, bufCapacity, fileLen, append, stats);
  }

  public DaosFileSourceSync(DaosFile daosFile, ByteBuf buffer, long fileLen, boolean append,
                            FileSystem.Statistics stats) {
    super(daosFile, buffer, fileLen, append, stats);
  }

  @Override
  protected int doWrite(long nextWritePos) throws IOException {
    return (int)daosFile.write(buffer, 0, nextWritePos, buffer.readableBytes());
  }

  @Override
  public int doRead(long nextReadPos, int length) throws IOException {
    return (int)this.daosFile.read(this.buffer, 0, nextReadPos, length);
  }
}
