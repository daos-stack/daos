/*
 * (C) Copyright 2018-2019 Intel Corporation.
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

package io.daos.obj;

import io.daos.BufferAllocator;
import io.daos.DaosObjectType;
import io.netty.buffer.ByteBuf;

import javax.annotation.concurrent.NotThreadSafe;

/**
 * DAOS Object ID corresponding to a specific container.
 * It contains 64-bit high and 64-bit low. Both will be encoded with feature bits and object type to get
 * final object ID.
 *
 * <p>
 *   You should call {@link #release()} after using it.
 * </p>
 */
@NotThreadSafe
public class DaosObjectId {

  private long high;

  private long low;

  private boolean encoded;

  private ByteBuf buffer;

  public DaosObjectId() {
  }

  public DaosObjectId(long high, long low) {
    this.high = high;
    this.low = low;
  }

  /**
   * encode with object feature bits and object type.
   *
   * @param feats      feature bits
   * @param objectType object type
   * @param args       reserved
   */
  public void encode(int feats, DaosObjectType objectType, int args) {
    if (encoded) {
      throw new IllegalStateException("already encoded");
    }
    buffer = BufferAllocator.objBufWithNativeOrder(16);
    buffer.writeLong(high).writeLong(low);
    try {
      DaosObjClient.encodeObjectId(buffer.memoryAddress(), feats, objectType.nameWithoutOc(), args);
    } catch (RuntimeException e) {
      buffer.release();
      buffer = null;
      throw e;
    }
    high = buffer.readLong();
    low = buffer.readLong();
    encoded = true;
  }

  /**
   * encode with default values.
   * feats: 0
   * objectType: {@linkplain DaosObjectType#OC_SX}
   * args: 0
   *
   * <p>
   * see {@link #encode(int, DaosObjectType, int)}
   */
  public void encode() {
    encode(0, DaosObjectType.OC_SX, 0);
  }

  public long getHigh() {
    return high;
  }

  public long getLow() {
    return low;
  }

  public boolean isEncoded() {
    return encoded;
  }

  public ByteBuf getBuffer() {
    if (buffer == null) {
      throw new IllegalStateException("DAOS object ID not encoded yet");
    }
    return buffer;
  }

  @Override
  public String toString() {
    return "object ID, high: " + high + ", low: " + low + ", encoded: " + encoded;
  }

  /**
   * release buffer
   */
  public void release() {
    if (buffer != null) {
      buffer.release();
    }
  }
}
