/*
 * (C) Copyright 2018-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
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

  private static final ByteBufAllocator bufAllocatorObj = new PooledByteBufAllocator(true);

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
}
