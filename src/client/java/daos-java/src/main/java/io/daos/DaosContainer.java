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

public class DaosContainer extends Shareable implements Closeable {

  private long contPtr;

  private String uuid;

  // keyed by container UUID
  private static final Map<String, DaosContainer> containerMap = new ConcurrentHashMap<>();

  private static final Logger log = LoggerFactory.getLogger(DaosContainer.class);

  private DaosContainer(String contUuid) {
    if (contUuid.length() != Constants.UUID_LENGTH) {
      throw new IllegalArgumentException("container UUID length should be " + Constants.UUID_LENGTH);
    }
    this.uuid = contUuid;
  }

  protected static DaosContainer getInstance(String contUuid, long poolPtr, int containerFlags) throws IOException {
    DaosContainer dc = containerMap.get(contUuid);
    if (dc == null) {
      dc = new DaosContainer(contUuid);
      containerMap.putIfAbsent(contUuid, dc);
      dc = containerMap.get(contUuid);
    }
    synchronized (dc) {
      dc.init(poolPtr, containerFlags);
      dc.incrementRef();
    }
    return dc;
  }

  private void init(long poolPtr, int containerFlags) throws IOException {
    if (isInited()) {
      return;
    }
    contPtr = DaosClient.daosOpenCont(poolPtr, uuid, containerFlags);
    setInited(true);
    if (log.isDebugEnabled()) {
      log.debug("opened container {} with ptr {}", uuid, contPtr);
    }
  }

  @Override
  public synchronized void close() throws IOException {
    decrementRef();
    if (isInited() && contPtr != 0 && getRefCnt() <= 0) {
      DaosClient.daosCloseContainer(contPtr);
      if (log.isDebugEnabled()) {
        log.debug("container {} with ptr {} closed", uuid, contPtr);
      }
      setInited(false);
      containerMap.remove(uuid);
    }
  }

  public String getUuid() {
    return uuid;
  }

  public long getContPtr() {
    return contPtr;
  }
}
