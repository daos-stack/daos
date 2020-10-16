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
import io.daos.DaosClient;
import io.daos.TimedOutException;
import io.netty.buffer.ByteBuf;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import javax.annotation.concurrent.NotThreadSafe;
import java.io.IOException;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

/**
 * DAOS Event Queue and Events per thread
 */
@NotThreadSafe
public class DaosEventQueue {

  private final long eqWrapperHdl;

  private final long descGrpHdl;

  private final String threadName;

  private final int nbrOfEvents;

  private final Event[] events;

  private final ByteBuf completed;

  private final int maxKeyStrLen;
  private final int nbrOfEntries;
  private final int entryBufLen;

  private int nextEventIdx;

  private int nbrOfAcquired;

  private boolean released;

  private long lastProgressed;

  private int nbrOfTimedOut;

  private static final int DEFAULT_POLL_TIMEOUT_MS = 10;

  private static final int DEFAULT_NO_PROGRESS_DURATION_ERROR = 5000; // MS

  private static final int DEFAULT_NBR_OF_TIMEDOUT_WARN = 5;

  private static final int DEFAULT_NBR_OF_TIMEDOUT_ERROR = 2 * DEFAULT_NBR_OF_TIMEDOUT_WARN;

  private static final Map<Long, DaosEventQueue> EQ_MAP = new ConcurrentHashMap<>();

  private static final Logger log = LoggerFactory.getLogger(DaosEventQueue.class);

  public DaosEventQueue(String threadName, int nbrOfEvents, int maxKeyStrLen, int nbrOfEntries,
                        int entryBufLen) throws IOException {
    this.threadName = threadName;
    if (nbrOfEvents > Short.MAX_VALUE) {
      throw new IllegalArgumentException("number of events should be no larger than " + Short.MAX_VALUE);
    }
    this.nbrOfEvents = nbrOfEvents;
    this.maxKeyStrLen = maxKeyStrLen;
    this.nbrOfEntries = nbrOfEntries;
    this.entryBufLen = entryBufLen;
    this.eqWrapperHdl = DaosClient.createEventQueue(nbrOfEvents);
    this.events = new Event[nbrOfEvents];
    for (int i = 0; i < nbrOfEvents; i++) {
      events[i] = new Event(i, maxKeyStrLen, nbrOfEntries, entryBufLen);
    }
    this.descGrpHdl = allocateSimDescGroup();
    // additional 4 bytes to put number of completed events
    this.completed = BufferAllocator.objBufWithNativeOrder(nbrOfEvents * 2 + 2);
    completed.writerIndex(completed.capacity());
  }

  private long allocateSimDescGroup() {
    ByteBuf buf = BufferAllocator.objBufWithNativeOrder((nbrOfEvents) * 8);
    try {
      for (Event e : events) {
        buf.writeLong(e.desc.getDescBuffer().memoryAddress());
      }
      long grpHdl = DaosObjClient.allocateSimDescGroup(buf.memoryAddress(), nbrOfEvents);
      for (Event e : events) {
        e.desc.checkNativeDesc();
      }
      return grpHdl;
    } finally {
      buf.release();
    }
  }

  public static DaosEventQueue getInstance(int nbrOfEvents, int maxKeyStrLen, int nbrOfEntries,
                                           int entryBufLen) throws IOException {
    long tid = Thread.currentThread().getId();
    DaosEventQueue queue = EQ_MAP.get(tid);
    if (queue == null) {
      queue = new DaosEventQueue(Thread.currentThread().getName(), nbrOfEvents, maxKeyStrLen, nbrOfEntries,
          entryBufLen);
      EQ_MAP.put(tid, queue);
    }
    return queue;
  }

  public int getNbrOfEvents() {
    return nbrOfEvents;
  }

  public int getNbrOfEntries() {
    return nbrOfEntries;
  }

  public int getMaxKeyStrLen() {
    return maxKeyStrLen;
  }

  public int getEntryBufLen() {
    return entryBufLen;
  }

  /**
   * no synchronization due to single thread access.
   *
   * @param updateOrFetch
   * @return event
   */
  public Event acquireEvent(boolean updateOrFetch) {
    int idx = nextEventIdx;
    if (nbrOfAcquired == nbrOfEvents) {
      return null;
    }
    while (!events[idx].available) {
      idx++;
      if (idx == nbrOfEvents) {
        idx = 0;
      }
      if (idx == nextEventIdx) {
        return null;
      }
    }
    nextEventIdx = idx + 1;
    if (nextEventIdx == nbrOfEvents) {
      nextEventIdx = 0;
    }
    Event ret = events[idx];
    ret.desc.setUpdateOrFetch(updateOrFetch);
    ret.available = false;
    nbrOfAcquired++;
    return ret;
  }

  public Event acquireEventBlock(boolean updateOrFetch, int maxWaitMs, List<IOSimpleDataDesc> completedList)
      throws IOException {
    DaosEventQueue.Event e = acquireEvent(updateOrFetch);
    if (e != null) { // for most of cases
      return e;
    }
    // check progress
    checkProgress();
    // unfortunately to poll repeatedly and wait
    int cnt = 0;
    int totalWait = 0;
    int wait = 0;
    while (e == null) {
      if (cnt % 10 == 0) {
        if (totalWait > maxWaitMs) {
          nbrOfTimedOut++;
          throw new TimedOutException("failed to acquire event after waiting " + totalWait + " ms");
        }
        wait = cnt < 100 ? cnt : 100;
        totalWait += wait;
      }
      pollCompleted(completedList, wait);
      e = acquireEvent(updateOrFetch);
      cnt++;
    }
    return e;
  }

  private void checkProgress() throws TimedOutException {
    if (nbrOfTimedOut > DEFAULT_NBR_OF_TIMEDOUT_ERROR) {
      long dur = System.currentTimeMillis() - lastProgressed;
      if (dur > DEFAULT_NO_PROGRESS_DURATION_ERROR) {
        throw new TimedOutException("too long duration without progress. number of timedout: " +
          nbrOfTimedOut + ", duration: " + dur);
      }
    }
    if (nbrOfTimedOut > DEFAULT_NBR_OF_TIMEDOUT_WARN) {
      log.warn("number of timedout: " + nbrOfTimedOut);
    }
  }

  public boolean hasPendingEvent() {
    return nbrOfAcquired > 0;
  }

  public void waitForCompletion(int maxWaitMs, List<IOSimpleDataDesc> completedList)
      throws IOException {
    long start = System.currentTimeMillis();
    int timeout = 0;
    while (nbrOfAcquired > 0) {
      int lastNbr = nbrOfAcquired;
      pollCompleted(completedList, timeout);
      if (lastNbr == nbrOfAcquired) {
        timeout = DEFAULT_POLL_TIMEOUT_MS;
      } else {
        timeout = 0;
      }
      if (System.currentTimeMillis() - start > maxWaitMs) {
        nbrOfTimedOut++;
        throw new TimedOutException("no completion after waiting more than " + maxWaitMs +
          ", nbrOfAcquired: " + nbrOfAcquired +", total: " + nbrOfEvents);
      }
    }
  }

//  public void releaseEvent(int idx) {
//    if (idx >= nbrOfEvents) {
//      throw new IllegalArgumentException("event index " + idx + " should not exceed number of total events, " +
//          nbrOfEvents);
//    }
//    events[idx].putBack();
//  }

  /**
   * It's just for accessing event without acquiring it for DAOS API calls.
   * Use {@link #acquireEvent(boolean)} or {@link #acquireEventBlock(boolean, int, List)} instead for DAOS API calls.
   *
   * @param idx
   * @return
   */
  public Event getEvent(int idx) {
    if (idx >= nbrOfEvents) {
      throw new IllegalArgumentException("event index " + idx + " should not exceed number of total events, " +
          nbrOfEvents);
    }
    return events[idx];
  }

  /**
   * poll completed event. The completed events are put back immediately.
   *
   * @param completedDescList
   * if it's not null, descs of completed events are added to this list.
   * @param timeOutMs
   * @return number of events completed
   * @throws IOException
   */
  public int pollCompleted(List<IOSimpleDataDesc> completedDescList, int timeOutMs) throws IOException {
    DaosClient.pollCompleted(eqWrapperHdl, completed.memoryAddress(),
      nbrOfEvents, timeOutMs < 0 ? DEFAULT_POLL_TIMEOUT_MS : timeOutMs);
    completed.readerIndex(0);
    int nbr = completed.readShort();
    Event event;
    for (int i = 0; i < nbr; i++) {
      event = events[completed.readShort()];
      completeDesc(event.desc);
      event.putBack();
      if (completedDescList != null) {
        completedDescList.add(event.desc);
      }
    }
    nbrOfAcquired -= nbr;
    if (nbr > 0) {
      lastProgressed = System.currentTimeMillis();
      nbrOfTimedOut = 0;
    }
    return nbr;
  }

  private void completeDesc(IOSimpleDataDesc desc) {
    if (desc.isUpdateOrFetch()) {
      desc.succeed();
    } else {
      desc.parseResult();
    }
  }

  public int getNbrOfAcquired() {
    return nbrOfAcquired;
  }

  public long getEqWrapperHdl() {
    return eqWrapperHdl;
  }

  public static void destroyAll() {
    EQ_MAP.forEach((k, v) -> {
      try {
        if (!v.released) {
          DaosClient.destroyEventQueue(v.eqWrapperHdl);
          DaosObjClient.releaseSimDescGroup(v.descGrpHdl);
          for (int i = 0; i < v.events.length; i++) {
            v.events[i].desc.release();
          }
          v.released = true;
        }
      } catch (Throwable e) {
        log.error("failed to destroy event queue in thread, " + v.threadName, e);
      }
    });
    EQ_MAP.clear();
  }

  public class Event {
    public final int id;
    public final long eqHandle;

    private final IOSimpleDataDesc desc;
    private boolean available;

    private Event(int id, int maxKeyStrLen, int nbrOfEntries, int entryBufLen) {
      this.eqHandle = eqWrapperHdl;
      this.id = id;
      this.available = true;
      this.desc = new IOSimpleDataDesc(maxKeyStrLen, nbrOfEntries, entryBufLen, this);
    }

    public IOSimpleDataDesc getDesc() {
      return desc;
    }

    public IOSimpleDataDesc reuseDesc() {
      desc.reuse();
      return desc;
    }

    private void putBack() {
      available = true;
    }
  }
}
