
package io.daos.obj;

public class PerfResult {
  private final long startTime;
  private final long endTime;
  private final float perf;
  private final long size;
  private final long duration;

  public PerfResult(long startTime, long endTime, long size) {
    this.startTime = startTime/1000000000;
    this.endTime = endTime/1000000000;
    this.size = size;
    duration = endTime - startTime;
    perf = ((float) (size))/duration;
  }

  public String toString() {
    StringBuilder sb = new StringBuilder();
    sb.append("start: " + startTime);
    sb.append("end: " + endTime);
    sb.append("perf (MB/s): " + (perf/1024/1024));
    sb.append("total read (MB): " + size/1024/1024);
    sb.append("seconds: " + duration);
    return sb.toString();
  }
}
