/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos;

public abstract class Shareable {

  private volatile boolean inited;

  private int refCnt;

  protected boolean isInited() {
    return inited;
  }

  protected void setInited(boolean inited) {
    this.inited = inited;
  }

  /**
   * increase reference count by one.
   *
   * @throws IllegalStateException if this client is disconnected.
   */
  protected synchronized void incrementRef() {
    if (!inited) {
      throw new IllegalStateException("DaosFsClient is not initialized or disconnected.");
    }
    refCnt++;
  }

  /**
   * decrease reference count by one.
   */
  protected synchronized void decrementRef() {
    refCnt--;
  }

  /**
   * get reference count.
   *
   * @return reference count
   */
  public synchronized int getRefCnt() {
    return refCnt;
  }

}
