package io.daos.obj;

import io.daos.BufferAllocator;
import io.netty.buffer.ByteBuf;
import org.junit.Assert;
import org.junit.Test;

import java.util.ArrayList;
import java.util.List;
import java.util.Random;

public class NettyAllocationTest {

  @Test
  public void run() {
    int size = 2 * 1024 * 1024;
    int limit = 100 * 1024 * 1024;
    long duration = 0;
    List<Integer> sizeList = new ArrayList<>();
    int total = 0;
    Random r = new Random();
    while (true) {
      int bsize = r.nextInt(size);
      total += bsize;
      sizeList.add(bsize);
      if (total > limit) {
        break;
      }
    }
    System.out.println(sizeList.size());
    int count = 5;
    for (int i = 0; i < count; i++) {
      duration += allocateBuffers(sizeList);
    }
    System.out.println(duration/count);
  }

  private long allocateBuffers(List<Integer> sizeList) {
    long start = System.currentTimeMillis();
    ByteBuf[] list = new ByteBuf[sizeList.size()];
    for (int i = 0; i < 100; i++) {
      int index = 0;
      for (Integer bsize : sizeList) {
        ByteBuf buf = BufferAllocator.objBufWithNativeOrder(bsize);
        Assert.assertTrue(buf.hasMemoryAddress());
        list[index++] = buf;
      }
      for (ByteBuf buf : list) {
        buf.release();
      }
      BufferAllocator.trimThreadLocalCache();
    }
    return System.currentTimeMillis() - start;
  }
}
