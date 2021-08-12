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

  private String id;

  // keyed by container UUID
  private static final Map<String, DaosContainer> containerMap = new ConcurrentHashMap<>();

  private static final Logger log = LoggerFactory.getLogger(DaosContainer.class);

  private DaosContainer(String contUuid) {
    if (contUuid.length() > Constants.ID_LENGTH) {
      throw new IllegalArgumentException("container UUID length should be " + Constants.ID_LENGTH);
    }
    this.id = contUuid;
  }

  protected static DaosContainer getInstance(String contId, long poolPtr, int containerFlags) throws IOException {
    DaosContainer dc = containerMap.get(contId);
    if (dc == null) {
      dc = new DaosContainer(contId);
      containerMap.putIfAbsent(contId, dc);
      dc = containerMap.get(contId);
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
    contPtr = DaosClient.daosOpenCont(poolPtr, id, containerFlags);
    setInited(true);
    if (log.isDebugEnabled()) {
      log.debug("opened container {} with ptr {}", id, contPtr);
    }
  }

  @Override
  public synchronized void close() throws IOException {
    decrementRef();
    if (isInited() && contPtr != 0 && getRefCnt() <= 0) {
      DaosClient.daosCloseContainer(contPtr);
      if (log.isDebugEnabled()) {
        log.debug("container {} with ptr {} closed", id, contPtr);
      }
      setInited(false);
      containerMap.remove(id);
    }
  }

  public String getContainerId() {
    return id;
  }

  public long getContPtr() {
    return contPtr;
  }
}
