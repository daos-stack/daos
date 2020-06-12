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
import io.daos.Constants;
import io.daos.DaosClient;
import io.daos.DaosObjectType;
import sun.nio.ch.DirectBuffer;

import javax.annotation.concurrent.NotThreadSafe;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/**
 * DAOS Object ID corresponding to a specific container.
 * It contains 64-bit high and 64-bit low. Both will be encoded with feature bits and object type to get
 * final object ID.
 */
@NotThreadSafe
public class DaosObjectId {

  private long high;

  private long low;

  private boolean encoded;

  private ByteBuffer buffer;

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
    // TODO: memory management for small buffer
    buffer = BufferAllocator.directBuffer(16);
    buffer.order(Constants.DEFAULT_ORDER);
    buffer.putLong(high).putLong(low);
    DaosObjClient.encodeObjectId(((DirectBuffer) buffer).address(), feats, objectType.nameWithoutOc(), args);
    buffer.flip();
    high = buffer.getLong();
    low = buffer.getLong();
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

  public ByteBuffer getBuffer() {
    if (buffer == null) {
      throw new IllegalStateException("DAOS object ID not encoded yet");
    }
    return buffer;
  }
}
