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

public class DaosContainer extends Shareable implements Closeable {

  private long contPtr;

  private String uuid;

  // keyed by container UUID
  private static final Map<String, DaosContainer> containerMap = new ConcurrentHashMap<>();

  private static final Logger log = LoggerFactory.getLogger(DaosContainer.class);

  private DaosContainer(String contUuid) {
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
