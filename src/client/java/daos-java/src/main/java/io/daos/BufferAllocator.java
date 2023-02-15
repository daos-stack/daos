/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos;

import io.netty.buffer.ByteBuf;
import io.netty.buffer.ByteBufAllocator;
import io.netty.buffer.PooledByteBufAllocator;
import io.netty.util.internal.PlatformDependent;

import java.nio.ByteBuffer;

/**
 * Entry point for getting buffer.
 *
 */
public class BufferAllocator {

  // DEFAULT allocator prefer direct buffer
  private static final PooledByteBufAllocator bufAllocatorObj = PooledByteBufAllocator.DEFAULT;

  static {
    if (!PlatformDependent.hasUnsafe()) {
      throw new RuntimeException("need unsafe support in buffer management");
    }
  }

  public static ByteBuffer directBuffer(int size) {
    return ByteBuffer.allocateDirect(size);
  }

  /**
   * get unsafe direct byte buffer with same endianness as native from netty.
   *
   * @param size
   * buffer size
   * @return pooled unsafe direct byte buffer with native endianness
   */
  public static ByteBuf objBufWithNativeOrder(int size) {
    return bufAllocatorObj.buffer(size).order(Constants.DEFAULT_ORDER);
  }

  /**
   * get unsafe direct byte buffer from netty.
   * @param size
   * buffer size
   * @return pooled unsafe direct byte buffer
   */
  public static ByteBuf directNettyBuf(int size) {
    return bufAllocatorObj.buffer(size);
  }

  /**
   * trim pooled buffer not being frequently allocated since last trim from cache in current thread
   *
   * @return true if any buffer trimmed. false otherwise
   */
  public static boolean trimThreadLocalCache() {
    return bufAllocatorObj.trimCurrentThreadCache();
  }
}
