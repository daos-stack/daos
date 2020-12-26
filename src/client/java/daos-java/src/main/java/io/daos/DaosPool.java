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

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.Closeable;
import java.io.IOException;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

public class DaosPool extends Shareable implements Closeable {

  private long poolPtr;

  private String uuid;

  // keyed by pool UUID
  private static final Map<String, DaosPool> poolMap = new ConcurrentHashMap<>();

  private static final Logger log = LoggerFactory.getLogger(DaosPool.class);

  private DaosPool(String poolUuid) {
    this.uuid = poolUuid;
  }

  protected static DaosPool getInstance(String poolUuid, String serverGroup, String ranks, int flags)
      throws IOException {
    DaosPool dp = poolMap.get(poolUuid);
    if (dp == null) {
      dp = new DaosPool(poolUuid);
      poolMap.putIfAbsent(poolUuid, dp);
      dp = poolMap.get(poolUuid);
    }
    synchronized (dp) {
      dp.init(serverGroup, ranks, flags);
      dp.incrementRef();
    }
    return dp;
  }

  private void init(String serverGroup, String ranks, int flags) throws IOException {
    if (isInited()) {
      return;
    }
    poolPtr = DaosClient.daosOpenPool(uuid, serverGroup, ranks, flags);
    setInited(true);
    if (log.isDebugEnabled()) {
      log.debug("opened pool {} with ptr {}", uuid, poolPtr);
    }
  }

  @Override
  public synchronized void close() throws IOException {
    decrementRef();
    if (isInited() && poolPtr != 0 && getRefCnt() <= 0) {
      DaosClient.daosClosePool(poolPtr);
      if (log.isDebugEnabled()) {
        log.debug("pool {} with ptr {} closed", uuid, poolPtr);
      }
      setInited(false);
      poolMap.remove(uuid);
    }
  }

  public String getUuid() {
    return uuid;
  }

  public long getPoolPtr() {
    return poolPtr;
  }
}
