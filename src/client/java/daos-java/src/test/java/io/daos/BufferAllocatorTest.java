package io.daos;

import java.lang.reflect.Field;
import java.nio.ByteBuffer;
import java.util.concurrent.atomic.AtomicLong;

public class BufferAllocatorTest {

  private static Long maxMemoryValue;
  private static Long reservedMemoryValue;

  private static Field maxMemory;
  private static Field reservedMemory;

  static {
    try {
      Class bitsClass = Class.forName("java.nio.Bits");
      maxMemory = bitsClass.getDeclaredField("maxMemory");
      maxMemory.setAccessible(true);
      reservedMemory = bitsClass.getDeclaredField("reservedMemory");
      reservedMemory.setAccessible(true);
      maxMemoryValue = (Long)maxMemory.get(null);
      reservedMemoryValue = ((AtomicLong)reservedMemory.get(null)).get();
    } catch (Exception e) {
      e.printStackTrace();
    }
  }

  public static void main(String args[]) throws ClassNotFoundException {
    System.out.println("max: " + maxMemoryValue);
    System.out.println("reserved: " + reservedMemoryValue);
    Runtime rt = Runtime.getRuntime();
    System.out.println("rt: " + rt.maxMemory());

    for (int i = 0; i < 100; i++) {
      ByteBuffer.allocateDirect(1024 * 1024);
    }

    try {
      maxMemoryValue = (Long) maxMemory.get(null);
      reservedMemoryValue = ((AtomicLong) reservedMemory.get(null)).get();
      System.out.println("max: " + maxMemoryValue);
      System.out.println("reserved: " + reservedMemoryValue);
      System.out.println("rt: " + rt.maxMemory());
    } catch (Exception e) {

    }
  }
}
