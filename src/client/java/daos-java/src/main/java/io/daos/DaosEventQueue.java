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

package io.daos;

import io.netty.buffer.ByteBuf;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

/**
 * DAOS Event Queue and Events per thread
 */
public class DaosEventQueue {

  private final long eqWrapperHdl;

  private final String threadName;

  private final int nbrOfEvents;

  private final boolean[] events;

  private final ByteBuf completed;

  private int nextEventIdx;

  private static final Map<Long, DaosEventQueue> EQ_MAP = new ConcurrentHashMap<>();

  private static final Logger log = LoggerFactory.getLogger(DaosEventQueue.class);

  public DaosEventQueue(String threadName, int nbrOfEvents) throws IOException {
    this.threadName = threadName;
    this.nbrOfEvents = nbrOfEvents;
    this.events = new boolean[nbrOfEvents];
    this.eqWrapperHdl = DaosClient.createEventQueue(nbrOfEvents);
    // additional 4 bytes to put number of completed events
    this.completed = BufferAllocator.objBufWithNativeOrder(nbrOfEvents * 4 + 4);
    completed.writerIndex(completed.capacity());
  }

  public static DaosEventQueue getInstance(int nbrOfEvents) throws IOException {
    long tid = Thread.currentThread().getId();
    DaosEventQueue queue = EQ_MAP.get(tid);
    if (queue == null) {
      queue = new DaosEventQueue(Thread.currentThread().getName(), nbrOfEvents);
      EQ_MAP.put(tid, queue);
    }
    return queue;
  }

  /**
   * no synchronization due to single thread access.
   *
   * @return event index starting from 0. -1 if there is no more events available.
   */
  public int acquireEvent() {
    int idx = nextEventIdx;
    while (events[idx]) {
      idx++;
      if (idx == nbrOfEvents) {
        idx = 0;
      }
      if (idx == nextEventIdx) {
        return -1;
      }
    }
    nextEventIdx = idx + 1;
    if (nextEventIdx == nbrOfEvents) {
      nextEventIdx = 0;
    }
    events[idx] = true;
    return idx;
  }

  public void releaseEvent(int idx) {
    if (idx >= nbrOfEvents) {
      throw new IllegalArgumentException("event index " + idx + " should not exceed number of total events, " +
          nbrOfEvents);
    }
    events[idx] = false;
  }

  public int pollCompleted() throws IOException {
    DaosClient.pollCompleted(eqWrapperHdl, completed.memoryAddress(), nbrOfEvents);
    completed.readerIndex(0);
    int nbr = completed.readInt();
    for (int i = 0; i < nbr; i++) {
      events[completed.readInt()] = false;
    }
    return nbr;
  }

  public long getEqWrapperHdl() {
    return eqWrapperHdl;
  }

  public static void destroyAll() {
    EQ_MAP.forEach((k, v) -> {
      try {
        DaosClient.destroyEventQueue(v.eqWrapperHdl);
      } catch (IOException e) {
        log.error("failed to destroy event queue in thread, " + v.threadName);
      }
    });
  }
}
