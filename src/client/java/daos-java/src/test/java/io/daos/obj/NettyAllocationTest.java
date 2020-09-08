package io.daos.obj;

import io.daos.BufferAllocator;
import io.netty.buffer.ByteBuf;

import java.util.ArrayList;
import java.util.List;

public class NettyAllocationTest {

  public static void main(String[] args) {
    int size = 1803886;
    int limit = 8 * 1024 * 1024;
    long duration = 0;
    for (int i = 0; i < 5; i++) {
      duration += allocateBuffers(size, limit);
    }
    System.out.println(duration/5);
  }

  private static long allocateBuffers(int size, int limit) {
    long start = System.nanoTime();
    int total = 0;
    List<ByteBuf> list = new ArrayList<>();
    for (int i = 0; i < 10; i++) {
      total = 0;
      for (int j = 0; j < 1000; j++) {
        ByteBuf buf = BufferAllocator.objBufWithNativeOrder(size);
        list.add(buf);
        total += size;
        if (total > limit) {
          for (ByteBuf b : list) {
            b.release();
          }
          total = 0;
          list.clear();
        }
      }
      if (total > 0) {
        for (ByteBuf b : list) {
          b.release();
        }
        list.clear();
      }
    }
    return System.nanoTime() - start;
  }
}
