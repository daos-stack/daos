/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
    if (poolUuid.length() != Constants.UUID_LENGTH) {
      throw new IllegalArgumentException("pool UUID length should be " + Constants.UUID_LENGTH);
    }
    this.uuid = poolUuid;
  }

  protected static DaosPool getInstance(String poolUuid, String serverGroup, int flags)
      throws IOException {
    DaosPool dp = poolMap.get(poolUuid);
    if (dp == null) {
      dp = new DaosPool(poolUuid);
      poolMap.putIfAbsent(poolUuid, dp);
      dp = poolMap.get(poolUuid);
    }
    synchronized (dp) {
      dp.init(serverGroup, flags);
      dp.incrementRef();
    }
    return dp;
  }

  private void init(String serverGroup, int flags) throws IOException {
    if (isInited()) {
      return;
    }
    if (serverGroup.length() > Constants.SERVER_GROUP_NAME_MAX_LEN) {
      throw new IllegalArgumentException("server group length should be no more than " +
          Constants.SERVER_GROUP_NAME_MAX_LEN);
    }
    poolPtr = DaosClient.daosOpenPool(uuid, serverGroup, flags);
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
