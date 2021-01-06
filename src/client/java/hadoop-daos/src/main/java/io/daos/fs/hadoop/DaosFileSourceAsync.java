package io.daos.fs.hadoop;

import io.daos.dfs.DaosFile;
import io.netty.buffer.ByteBuf;
import org.apache.hadoop.fs.FileSystem;

import java.io.IOException;

public class DaosFileSourceAsync extends DaosFileSource {

  public DaosFileSourceAsync(DaosFile daosFile, int bufCapacity, long fileLen,
                             FileSystem.Statistics stats) {
    super(daosFile, bufCapacity, fileLen, stats);
  }

  public DaosFileSourceAsync(DaosFile daosFile, ByteBuf buffer, long fileLen,
                             FileSystem.Statistics stats) {
    super(daosFile, buffer, fileLen, stats);
  }

  @Override
  public void closeMore() {

  }

  @Override
  protected long doWrite(long nextWritePos) throws IOException {
    return 0;
  }

  @Override
  protected int doRead(long nextReadPos, int length) throws IOException {
    return 0;
  }


}
