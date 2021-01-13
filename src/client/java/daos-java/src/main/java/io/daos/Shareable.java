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
