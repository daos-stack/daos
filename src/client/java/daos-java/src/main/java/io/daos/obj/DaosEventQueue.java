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

package io.daos.obj;

import io.daos.BufferAllocator;
import io.daos.DaosClient;
import io.daos.DaosIOException;
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
 * Java wrapper of underlying native DAOS event queue which has multiple reusable events. It manages event
 * acquiring and releasing, as well as polls completed events.
 * {@link IOSimpleDataDesc} is associated with each event so that it can be reused along with event.
 * One instance per thread.
 */
@NotThreadSafe
public class DaosEventQueue {

  private final long eqWrapperHdl;

  private final String threadName;

  private final int nbrOfEvents;

  protected final Event[] events;

  private final ByteBuf completed;

  private int nextEventIdx;

  private int nbrOfAcquired;

  protected boolean released;

  private long lastProgressed;

  private int nbrOfTimedOut;

  private static final int DEFAULT_POLL_TIMEOUT_MS = 10;

  private static final int DEFAULT_NO_PROGRESS_DURATION_ERROR = 5000; // MS

  private static final int DEFAULT_NBR_OF_TIMEDOUT_WARN = 5;

  private static final int DEFAULT_NBR_OF_TIMEDOUT_ERROR = 2 * DEFAULT_NBR_OF_TIMEDOUT_WARN;

  private static final Map<Long, DaosEventQueue> EQ_MAP = new ConcurrentHashMap<>();

  private static final Logger log = LoggerFactory.getLogger(DaosEventQueue.class);

  /**
   * constructor without {@link IOSimpleDataDesc} being bound.
   *
   * @param threadName
   * @param nbrOfEvents
   * @throws IOException
   */
  protected DaosEventQueue(String threadName, int nbrOfEvents) throws IOException {
    this.threadName = threadName;
    if (nbrOfEvents > Short.MAX_VALUE) {
      throw new IllegalArgumentException("number of events should be no larger than " + Short.MAX_VALUE);
    }
    this.nbrOfEvents = nbrOfEvents;
    this.eqWrapperHdl = DaosClient.createEventQueue(nbrOfEvents);
    this.events = new Event[nbrOfEvents];
    for (int i = 0; i < nbrOfEvents; i++) {
      events[i] = createEvent((short)i);
    }
    // additional 2 bytes to put number of completed events
    this.completed = BufferAllocator.objBufWithNativeOrder(nbrOfEvents * 2 + 2);
    completed.writerIndex(completed.capacity());
  }

  protected Event createEvent(short id) {
    return new Event(id);
  }

  /**
   * Get EQ bounded to current thread. If it's first time call, EQ will be created and bound it to current thread.
   *
   * @param nbrOfEvents
   * how many events created in EQ
   * @param maxKeyStrLen
   * max dkey/akey len
   * @param nbrOfEntries
   * number of akey entries for each desc
   * @param entryBufLen
   * entry buffer length
   * @return single {@link DaosEQWithDesc} instance per thread
   * @throws IOException
   */
  public static DaosEQWithDesc getInstance(int nbrOfEvents, int maxKeyStrLen, int nbrOfEntries,
                                           int entryBufLen) throws IOException {
    long tid = Thread.currentThread().getId();
    DaosEventQueue queue = EQ_MAP.get(tid);
    if (queue == null) {
      queue = new DaosEQWithDesc(Thread.currentThread().getName(), nbrOfEvents, maxKeyStrLen, nbrOfEntries,
          entryBufLen);
      EQ_MAP.put(tid, queue);
    }
    return (DaosEQWithDesc)queue;
  }

  /**
   * Get EQ without any {@link IOSimpleDataDesc} being bound.
   *
   * @param nbrOfEvents
   * how many events created in EQ
   * @return single {@link DaosEventQueue} instance per thread
   * @throws IOException
   */
  public static DaosEventQueue getInstance(int nbrOfEvents) throws IOException {
    long tid = Thread.currentThread().getId();
    DaosEventQueue queue = EQ_MAP.get(tid);
    if (queue == null) {
      queue = new DaosEventQueue(Thread.currentThread().getName(), nbrOfEvents);
      EQ_MAP.put(tid, queue);
    }
    return queue;
  }

  public int getNbrOfEvents() {
    return nbrOfEvents;
  }

  /**
   * no synchronization due to single thread access.
   *
   * @param updateOrFetch
   * event for update or fetch? true for update, false for fetch.
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
    ret.available = false;
    nbrOfAcquired++;
    return ret;
  }

  /**
   * acquire event with timeout. If there is no event available, this method will try to poll completed
   * events. If there is no completed event for more than <code>maxWaitMs</code>, a timeout exception
   * will be thrown.
   *
   * @param updateOrFetch
   * true for update, false for fetch.
   * @param maxWaitMs
   * max wait time in millisecond
   * @param completedList
   * a list to hold {@link IOSimpleDataDesc}s associated with completed events.
   * null means you want to ignore them.
   * @return event
   * @throws IOException
   */
  public Event acquireEventBlocking(boolean updateOrFetch, int maxWaitMs, List<IOSimpleDataDesc> completedList)
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

  /**
   * wait for completion of all events within specified time, <code>maxWaitMs</code>.
   *
   * @param maxWaitMs
   * max wait time in millisecond
   * @param completedList
   * a list to hold {@link IOSimpleDataDesc}s associated with completed events.
   * null means you want to ignore them.
   * @throws IOException
   */
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
   * Use {@link #acquireEvent(boolean)} or {@link #acquireEventBlocking(boolean, int, List)} instead for DAOS API calls.
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
      IOSimpleDataDesc desc = event.complete();
      if (completedDescList != null) {
        completedDescList.add(desc);
      }
    }
    nbrOfAcquired -= nbr;
    if (nbr > 0) {
      lastProgressed = System.currentTimeMillis();
      nbrOfTimedOut = 0;
    }
    return nbr;
  }

  public int getNbrOfAcquired() {
    return nbrOfAcquired;
  }

  public long getEqWrapperHdl() {
    return eqWrapperHdl;
  }

  public synchronized void release() throws IOException {
    if (released) {
      return;
    }
    DaosClient.destroyEventQueue(eqWrapperHdl);
    releaseMore();
    released = true;
  }

  protected void releaseMore() {}

  /**
   * destroy all event queues. It's should be called when JVM is shutting down.
   */
  public static void destroyAll() {
    EQ_MAP.forEach((k, v) -> {
      try {
        v.release();
      } catch (Throwable e) {
        log.error("failed to destroy event queue in thread, " + v.threadName, e);
      }
    });
    EQ_MAP.clear();
  }

  public class Event {
    private final short id;
    private final long eqHandle;

    protected boolean available;
    protected IOSimpleDataDesc desc;

    protected Event(short id) {
      this.eqHandle = eqWrapperHdl;
      this.id = id;
      this.available = true;
    }

    public int getId() {
      return id;
    }

    public IOSimpleDataDesc getDesc() {
      return desc;
    }

    public long getEqHandle() {
      return eqHandle;
    }

    public void setDesc(IOSimpleDataDesc desc) {
      this.desc = desc;
    }

    protected void putBack() {
      available = true;
      desc = null;
    }

    public IOSimpleDataDesc complete() {
      if (desc.isUpdateOrFetch()) {
        desc.succeed();
      } else {
        desc.parseResult();
      }
      IOSimpleDataDesc d = desc;
      putBack();
      return d;
    }
  }
}
