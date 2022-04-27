/*
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos;

import io.daos.obj.IOSimpleDataDesc;
import io.netty.buffer.ByteBuf;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import javax.annotation.concurrent.NotThreadSafe;
import java.io.IOException;
import java.util.*;
import java.util.concurrent.ConcurrentHashMap;

/**
 * Java wrapper of underlying native DAOS event queue which has multiple reusable events. It manages event
 * acquiring and releasing, as well as polls completed events.
 *
 * The default number of events per EQ can be configured via {@linkplain Constants#CFG_NUMBER_OF_EVENTS_PER_EQ}
 * in system property or system environment. Or hard-coded value {@linkplain Constants#DEFAULT_NUMBER_OF_EVENTS_PER_EQ}
 * is used.
 *
 * One instance per thread.
 */
@NotThreadSafe
public class DaosEventQueue {

  private final long eqWrapperHdl;

  private final String threadName;

  private final long threadId;

  private final int nbrOfEvents;

  protected final Event[] events;

  private final ByteBuf completed;

  private int nextEventIdx;

  private int nbrOfAcquired;

  protected boolean released;

  private long lastProgressed;

  private int nbrOfTimedOut;

  private Map<Class<?>, List<Attachment>> attMap = new HashMap<>();

  private static final int DEFAULT_POLL_TIMEOUT_MS = 10;

  private static final int DEFAULT_NO_PROGRESS_DURATION_ERROR = Integer.valueOf(
      System.getProperty(Constants.CFG_DAOS_TIMEOUT, Constants.DEFAULT_DAOS_TIMEOUT_MS)); // MS

  private static final int DEFAULT_NBR_OF_TIMEDOUT_WARN = 5;

  private static final int DEFAULT_NBR_OF_TIMEDOUT_ERROR = 2 * DEFAULT_NBR_OF_TIMEDOUT_WARN;

  private static int DEFAULT_NBR_OF_EVENTS;

  private static final Map<Long, DaosEventQueue> EQ_MAP = new ConcurrentHashMap<>();

  private static final Logger log = LoggerFactory.getLogger(DaosEventQueue.class);

  static {
    String v = System.getProperty(Constants.CFG_NUMBER_OF_EVENTS_PER_EQ);
    if (v != null) {
      DEFAULT_NBR_OF_EVENTS = Integer.valueOf(v);
    } else {
      v = System.getenv(Constants.CFG_NUMBER_OF_EVENTS_PER_EQ);
      DEFAULT_NBR_OF_EVENTS = v == null ? Constants.DEFAULT_NUMBER_OF_EVENTS_PER_EQ : Integer.valueOf(v);
    }
    if (DEFAULT_NBR_OF_EVENTS <= 0) {
      log.error("got non-positive number of events per EQ, " + DEFAULT_NBR_OF_EVENTS +
          ", check your property or env config, " + Constants.CFG_NUMBER_OF_EVENTS_PER_EQ +
          ". set to default value, " + Constants.DEFAULT_NUMBER_OF_EVENTS_PER_EQ);
      DEFAULT_NBR_OF_EVENTS = Constants.DEFAULT_NUMBER_OF_EVENTS_PER_EQ;
    }
  }

  /**
   * constructor without {@link IOSimpleDataDesc} being bound.
   *
   * @param threadName
   * @param nbrOfEvents
   * @throws IOException
   */
  protected DaosEventQueue(String threadName, int nbrOfEvents) throws IOException {
    this.threadName = threadName == null ? Thread.currentThread().getName() : threadName;
    this.threadId = Thread.currentThread().getId();
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
   * Get EQ without any {@link Attachment} being bound. User should associate it to event by himself.
   * @see {@link IOSimpleDataDesc} {@link io.daos.dfs.IODfsDesc}
   *
   * If <code>nbrOfEvents</code> is <= 0, default value is used.
   *
   * @param nbrOfEvents
   * how many events created in EQ.
   * @return single {@link DaosEventQueue} instance per thread
   * @throws IOException
   */
  public static DaosEventQueue getInstance(int nbrOfEvents) throws IOException {
    long tid = Thread.currentThread().getId();
    DaosEventQueue queue = EQ_MAP.get(tid);
    if (queue == null) {
      if (nbrOfEvents <= 0) {
        nbrOfEvents = DEFAULT_NBR_OF_EVENTS;
      }
      queue = new DaosEventQueue(null, nbrOfEvents);
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
   * @return event
   */
  public Event acquireEvent() {
    int idx = nextEventIdx;
    if (nbrOfAcquired == nbrOfEvents) {
      return null;
    }
    while (events[idx].status != EventStatus.FREE) {
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
    ret.status = EventStatus.USING;
    nbrOfAcquired++;
    return ret;
  }

  /**
   * acquire event with timeout. If there is no event available, this method will try to poll completed
   * events. If there is no completed event for more than <code>maxWaitMs</code>, a timeout exception
   * will be thrown.
   *
   * @param maxWaitMs
   * max wait time in millisecond
   * @param completedList
   * a list to hold {@link Attachment}s associated with completed events.
   * @param klass
   * expected class instance of attachment
   * @return event
   * @throws IOException
   */
  public Event acquireEventBlocking(long maxWaitMs, List<Attachment> completedList, Class<? extends Attachment> klass,
                                    Set<? extends Attachment> candidates)
      throws IOException {
    Event e = acquireEvent();
    if (e != null) { // for most of cases
      return e;
    }
    // check progress
    checkProgress();
    // unfortunately to poll repeatedly and wait
    int cnt = 0;
    int wait;
    long start = System.currentTimeMillis();
    while (e == null) {
      if ((cnt % 10 == 0) & (System.currentTimeMillis() - start) > maxWaitMs) {
          nbrOfTimedOut++;
          throw new TimedOutException("failed to acquire event after waiting more than " + maxWaitMs + " ms");
      }
      wait = cnt < 100 ? cnt : 100;
      pollCompleted(completedList, klass, candidates, wait);
      e = acquireEvent();
      cnt++;
    }
    return e;
  }

  private void checkProgress() throws TimedOutException {
    if (nbrOfTimedOut > DEFAULT_NBR_OF_TIMEDOUT_ERROR) {
      long dur = System.currentTimeMillis() - lastProgressed;
      if (dur > DEFAULT_NO_PROGRESS_DURATION_ERROR) {
        throw new TimedOutException("too long duration without progress. number of timedout: " +
          nbrOfTimedOut + ", duration: " + dur +", nbrOfAcquired: " + nbrOfAcquired +
            ", nbrOfEvents: " + nbrOfEvents);
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
   * @param klass
   * expected class instance of attachment
   * @param completedList
   * a list to hold {@link Attachment}s associated with completed events.
   * @throws IOException
   */
  public void waitForCompletion(long maxWaitMs, Class<? extends Attachment> klass, List<Attachment> completedList)
      throws IOException {
    long start = System.currentTimeMillis();
    int timeout = 0;
    while (nbrOfAcquired > 0) {
      int lastNbr = nbrOfAcquired;
      pollCompleted(completedList, klass, null, timeout);
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

  /**
   * It's just for accessing event without acquiring it for DAOS API calls.
   * Use {@link #acquireEvent()} or {@link #acquireEventBlocking(long, List, Class, Set)} instead for DAOS API calls.
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
   * abort event.
   *
   * @param event
   * @return true if event being aborted. false if event is not in use.
   */
  public boolean abortEvent(Event event) {
    return DaosClient.abortEvent(eqWrapperHdl, event.getId());
  }

  /**
   * return event which has been used for any DAOS operation.
   *
   * @param event
   */
  public void returnEvent(Event event) {
    event.putBack();
    nbrOfAcquired--;
  }

  /**
   * poll completed events. The completed events are put back immediately.
   *
   * @param completedList
   * attachments of completed events are added to this list.
   * @param klass
   * expected class instance of attachment
   * @param candidates
   * expected attachments to get from poll, null means getting any attachment
   * @param timeOutMs
   * timeout in millisecond
   * @return number of events completed. Note that this number may not be equal to number of attachment added to
   * <code>completedList</code>.
   * @throws IOException
   */
  public int pollCompleted(List<Attachment> completedList, Class<? extends Attachment> klass,
                           Set<? extends Attachment> candidates, long timeOutMs) throws IOException {
    return pollCompleted(completedList, klass, candidates, candidates != null ? candidates.size() : nbrOfEvents,
        timeOutMs);
  }

  /**
   * poll expected number of completed events. The completed events are put back immediately.
   *
   * @param completedList
   * expected attachments of completed events are added to this list.
   * @param klass
   * expected class instance of attachment
   * @param candidates
   * expected attachments to get from poll. null means getting any attachment
   * @param expNbrOfRet
   * expected number of completed event
   * @param timeOutMs
   * timeout in millisecond
   * @return number of events completed. Note that this number may not be equal to number of attachment added to
   * <code>completedList</code>.
   * @throws IOException
   */
  public int pollCompleted(List<Attachment> completedList, Class<? extends Attachment> klass,
                           Set<? extends Attachment> candidates, int expNbrOfRet, long timeOutMs) throws IOException {
    assert Thread.currentThread().getId() == threadId : "current thread " + Thread.currentThread().getId() + "(" +
        Thread.currentThread().getName() + "), is not expected " + Thread.currentThread().getId() + "(" +
        threadName + ")";

    int aborted;
    int nbr;
    // check detained attachments first.
    int moved = moveAttachment(completedList, klass, candidates, expNbrOfRet);
    expNbrOfRet -= moved;
    if (expNbrOfRet == 0) {
      return moved;
    }
    while (nbrOfAcquired > 0) {
      aborted = 0;
      DaosClient.pollCompleted(eqWrapperHdl, completed.memoryAddress(),
          nbrOfAcquired, timeOutMs < 0 ? DEFAULT_POLL_TIMEOUT_MS : timeOutMs);
      completed.readerIndex(0);
      nbr = completed.readShort();
      Event event;
      for (int i = 0; i < nbr; i++) {
        event = events[completed.readShort()];
        if (event.status == EventStatus.ABORTED) {
          aborted++;
          event.putBack();
          continue;
        }
        Attachment attachment = event.complete();
        if ((completedList.size() < expNbrOfRet) & (candidates == null || candidates.contains(attachment))) {
          completedList.add(attachment);
        } else {
          detainAttachment(attachment);
        }
      }
      nbrOfAcquired -= nbr;
      nbr -= aborted;
      if (nbr > 0) {
        lastProgressed = System.currentTimeMillis();
        nbrOfTimedOut = 0;
        return nbr;
      }
      if (aborted == 0) {
        return nbr;
      }
    }
    return 0;
  }

  private int moveAttachment(List<Attachment> completedList, Class<? extends Attachment> klass,
                              Set<? extends Attachment> candidates, int expNbr) {
    int nbr = 0;
    List<Attachment> detainedList = attMap.get(klass);
    if (detainedList != null && !detainedList.isEmpty()) {
      Iterator<Attachment> it = detainedList.iterator();
      while ((nbr < expNbr) & it.hasNext()) {
        Attachment att = it.next();
        if (candidates == null || candidates.contains(att)) {
          completedList.add(att);
          it.remove();
          nbr++;
        }
      }
    }
    return nbr;
  }

  private void detainAttachment(Attachment attachment) {
    if (attachment.isDiscarded()) {
      attachment.release();
      return;
    }
    Class<?> klass = attachment.getClass();
    List<Attachment> list = attMap.get(klass);
    if (list == null) {
      list = new LinkedList<>();
      attMap.put(klass, list);
    }
    list.add(attachment);
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

  protected void releaseMore() {
    for (Event e : events) {
      Attachment attachment = e.getAttachment();
      if (attachment != null) {
        attachment.release();
      }
    }
    for (List<Attachment> list : attMap.values()) {
      list.forEach(a -> a.release());
    }
    attMap = null;
  }

  public static void destroy(long id, DaosEventQueue eq) throws IOException {
    long tid = Thread.currentThread().getId();
    if (id != tid) {
      throw new UnsupportedOperationException("Cannot destroy EQ belongs to other thread, id: " + id);
    }
    DaosEventQueue teq = EQ_MAP.get(id);
    if (teq != eq) {
      throw new IllegalArgumentException("given EQ is not same as EQ of current thread");
    }
    eq.release();
    EQ_MAP.remove(id);
  }

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

  public String getThreadName() {
    return threadName;
  }

  public long getThreadId() {
    return threadId;
  }

  /**
   * Java represent of DAOS event associated to a event queue identified by
   * <code>eqHandle</code>.
   * A {@link Attachment} can be associate to event as a outcome of asynchronous call.
   */
  public class Event {
    private final short id;
    private final long eqHandle;

    protected Attachment attachment;
    protected EventStatus status;

    protected Event(short id) {
      this.eqHandle = eqWrapperHdl;
      this.id = id;
      this.status = EventStatus.FREE;
    }

    public short getId() {
      return id;
    }

    public Attachment getAttachment() {
      return attachment;
    }

    public long getEqHandle() {
      return eqHandle;
    }

    public Attachment setAttachment(Attachment attachment) {
      Attachment pa = this.attachment;
      this.attachment = attachment;
      return pa;
    }

    private void putBack() {
      status = EventStatus.FREE;
      if (attachment != null && !attachment.alwaysBoundToEvt()) {
        attachment = null;
      }
    }

    public Attachment complete() {
      Attachment ret = attachment;
      if (attachment != null) {
        attachment.ready();
      }
      putBack();
      return ret;
    }

    public void abort() throws DaosIOException {
      if (status != EventStatus.USING) {
        return;
      }
      status = EventStatus.ABORTED;
      if (!abortEvent(this)) { // event is not actually using
        status = EventStatus.FREE;
      }
    }
  }

  public enum EventStatus {
    FREE, USING, ABORTED
  }

  public interface Attachment {
    /**
     * associate this attachment to event.
     *
     * @param e
     */
    void setEvent(Event e);

    /**
     * reuse attachment.
     */
    void reuse();

    /**
     * it's should be called before attachment being consumed by user.
     */
    void ready();

    /**
     * indicate if we need to disassociate this attachment from event when event is put back.
     * true for no, false for yes.
     *
     * @return true or false
     */
    boolean alwaysBoundToEvt();

    /**
     * discard attachment
     */
    void discard();

    /**
     * check if attachment is discarded. If true, it may be discarded when poll and detain attachment.
     */
    boolean isDiscarded();

    /**
     * release resources if any.
     */
    void release();
  }
}
