package io.daos.fs.hadoop;

import io.daos.BufferAllocator;
import io.daos.DaosEventQueue;
import io.daos.dfs.DaosFile;
import io.netty.buffer.ByteBuf;
import org.junit.Assert;
import org.junit.BeforeClass;
import org.junit.Test;

import java.io.IOException;

public class DaosFileSourceAsyncIT {

  @Test
  public void testReadWriteMultipleTimes() throws IOException {
    DaosFile file = DaosFSFactory.getFsClient().getFile("/DaosFileSourceAsyncIT_1");
    file.createNewFile();
    ByteBuf buffer = BufferAllocator.directNettyBuf(1000);
    DaosFileSourceAsync fs = new DaosFileSourceAsync(file, buffer, 0, false, null);
    buffer.writeLong(10000);
    fs.doWrite(0);
    buffer.clear();
    buffer.writeLong(20000);
    fs.doWrite(8);
    buffer.release();
    fs.close();

    file = DaosFSFactory.getFsClient().getFile("/DaosFileSourceAsyncIT_1");
    DaosFileSourceAsync fs2 = new DaosFileSourceAsync(file, 1000, 0, true, null);
    fs2.doRead(0, 8);
    long v1 = fs2.buffer.readLong();
    Assert.assertEquals(10000, v1);
    fs2.doRead(8, 8);
    long v2 = fs2.buffer.readLong();
    Assert.assertEquals(20000, v2);
    fs2.close();
  }
}
