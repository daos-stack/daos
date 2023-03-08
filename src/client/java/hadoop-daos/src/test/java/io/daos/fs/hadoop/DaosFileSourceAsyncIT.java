package io.daos.fs.hadoop;

import io.daos.BufferAllocator;
import io.daos.DaosEventQueue;
import io.daos.dfs.DaosFile;
import io.netty.buffer.ByteBuf;
import org.apache.hadoop.fs.FileSystem;
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
    FileSystem.Statistics stat = FileSystem.getStatistics("daos", DaosFileSystem.class);
    DaosFileSourceAsync fs = new DaosFileSourceAsync(file, buffer, 0, false, false, stat);
    buffer.writeLong(10000);
    fs.doWrite(0);
    buffer.clear();
    buffer.writeLong(20000);
    fs.doWrite(8);
    buffer.clear();
    fs.close();
    // append
    file = DaosFSFactory.getFsClient().getFile("/DaosFileSourceAsyncIT_1");
    DaosFileSourceAsync fs11 = new DaosFileSourceAsync(file, buffer, file.length(), false, true, stat);
    buffer.writeLong(30000);
    fs11.flush();
    buffer.release();
    fs11.close();

    file = DaosFSFactory.getFsClient().getFile("/DaosFileSourceAsyncIT_1");
    DaosFileSourceAsync fs2 = new DaosFileSourceAsync(file, 1000, file.length(), true,
        false, stat);
    fs2.doRead(0, 8);
    long v1 = fs2.buffer.readLong();
    Assert.assertEquals(10000, v1);
    fs2.doRead(8, 8);
    long v2 = fs2.buffer.readLong();
    Assert.assertEquals(20000, v2);
    fs2.doRead(16, 8);
    long v3 = fs2.buffer.readLong();
    Assert.assertEquals(30000, v3);
    fs2.close();
  }
}
