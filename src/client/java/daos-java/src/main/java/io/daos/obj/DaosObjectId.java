/*
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.obj;

import io.daos.BufferAllocator;
import io.daos.DaosObjClassHint;
import io.daos.DaosObjectClass;
import io.daos.DaosObjectType;
import io.netty.buffer.ByteBuf;

import javax.annotation.concurrent.NotThreadSafe;

/**
 * DAOS Object ID corresponding to a specific container.
 * It contains 64-bit high and 64-bit low. Both will be encoded with object type and object class to get
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
   * encode with object type and object class.
   *
   * @param objectType  object type for dkey/akey
   * @param objectClass object class
   * @param hint        object class hint
   * @param args        reserved
   */
  public void encode(long contPtr, DaosObjectType objectType, DaosObjectClass objectClass, DaosObjClassHint hint,
                     int args) {
    if (encoded) {
      throw new IllegalStateException("already encoded");
    }
    buffer = BufferAllocator.objBufWithNativeOrder(16);
    buffer.writeLong(high).writeLong(low);
    try {
      DaosObjClient.encodeObjectId(buffer.memoryAddress(), contPtr, objectType.getId(),
          objectClass.nameWithoutOc(), hint.getId(), args);
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
   * objectType: {@link DaosObjectType#DAOS_OT_MULTI_HASHED}
   * objectClass: {@linkplain DaosObjectClass#OC_SX}
   * args: 0
   *
   * @param contPtr
   * container handle
   * <p>
   * see {@link #encode(long, DaosObjectType, DaosObjectClass, DaosObjClassHint, int)}
   */
  public void encode(long contPtr) {
    encode(contPtr, DaosObjectType.DAOS_OT_MULTI_HASHED, DaosObjectClass.OC_SX,
        DaosObjClassHint.DAOS_OCH_RDD_DEF, 0);
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
