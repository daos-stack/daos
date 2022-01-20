/**
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.fs.hadoop.perf;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.net.InetAddress;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;
import java.util.concurrent.*;

import io.daos.BufferAllocator;
import io.daos.dfs.DaosFile;
import io.daos.dfs.DaosFsClient;
import io.daos.fs.hadoop.Constants;
import io.daos.fs.hadoop.DaosFsConfig;
import io.netty.buffer.ByteBuf;
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.FSDataInputStream;
import org.apache.hadoop.fs.FSDataOutputStream;
import org.apache.hadoop.fs.FileSystem;
import org.apache.hadoop.fs.Path;

public class Main {

  static final String WRITE_PERF_PREFIX = "write ";
  static final String READ_PERF_PREFIX = "read ";
  static final String FINAL_WRITE_PERF_PREFIX = "write perf: ";
  static final String FINAL_READ_PERF_PREFIX = "read perf: ";

  private static void setDFSArgs(Configuration conf) {
    String pid = System.getProperty("pid");
    if (pid != null) {
      conf.set(Constants.DAOS_POOL_ID, pid);
    }
    String uid = System.getProperty("uid");
    if (uid != null) {
      conf.set(Constants.DAOS_CONTAINER_ID, uid);
    }
    String async = System.getProperty(Constants.DAOS_IO_ASYNC);
    if (async != null) {
      conf.set(Constants.DAOS_IO_ASYNC, async);
    }
    String defaultURI = System.getProperty("uri", "daos:///");
    if (defaultURI != null) {
      conf.set("fs.defaultFS", defaultURI);
    }
  }

  public static void main(String args[]) throws Exception {
    if (args.length < 1) {
      throw new Exception("need arguments of write, read or both");
    }

    String threads = System.getProperty("threads");
    String jvms = System.getProperty("jvms");

    if (threads != null && jvms != null) {
      throw new Exception("only one of threads and jvms at same time.");
    }

    if (threads != null) {
      System.out.println("in threads mode");
      FileSystem fs = null;
      if ("hadoop-api".equalsIgnoreCase(System.getProperty("api", "hadoop-api"))) {
        Configuration conf = new Configuration(false);
        setDFSArgs(conf);
        fs = FileSystem.get(conf);
      }

      if ("write".equalsIgnoreCase(args[0]) || "both".equalsIgnoreCase(args[0])) {
        Runner runner = new ThreadsWriteRunner(fs);
        runner.run();
      }

      if ("read".equalsIgnoreCase(args[0]) || "both".equalsIgnoreCase(args[0])) {
        Runner runner = new ThreadsReadRunner(fs);
        runner.run();
      }
    }

    if (jvms != null) {
      System.out.println("in jvms mode");
      String async = System.getProperty(Constants.DAOS_IO_ASYNC, "true");
      if ("write".equalsIgnoreCase(args[0]) || "both".equalsIgnoreCase(args[0])) {
        Runner runner = new JvmsWriteRunner(async);
        runner.run();
      }

      if ("read".equalsIgnoreCase(args[0]) || "both".equalsIgnoreCase(args[0])) {
        Runner runner = new JvmsReadRunner(async);
        runner.run();
      }
    }
  }

  static abstract class Runner {
    protected FileSystem fs;
    protected String path;
    protected int parallel;
    protected String seq;
    protected int jvmThreads;
    protected String api;
    protected String scriptPath;

    protected Runner(FileSystem fs) {
      this.fs = fs;
    }

    protected void prepare() throws Exception {
      path = System.getProperty("path", "/direct-test/write");
      seq = System.getProperty("seq");
      System.out.println("seq: " + seq);
      String hostname = InetAddress.getLocalHost().getHostName();
      path = path + "/" + hostname + "/" + (seq == null ? "" : seq);
      System.out.println("write path: " + path);
      jvmThreads = Integer.valueOf(System.getProperty("jvmThreads", "1"));
      System.out.println("JVM threads: " + jvmThreads);
      api = System.getProperty("api", "hadoop-api");
      System.out.println("api: " + api);
      scriptPath = System.getProperty("scriptPath", "/root/daos");
      System.out.println("script path: " + scriptPath);
    }

    public abstract void run() throws Exception;
  }

  static abstract class WriteRunner extends Runner {
    protected int writeSize;
    protected long fileSize;

    protected WriteRunner(FileSystem fs) {
      super(fs);
    }

    protected void prepare() throws Exception {
      super.prepare();
      writeSize = Constants.DEFAULT_DAOS_WRITE_BUFFER_SIZE;
      System.out.println("write buffer size: " + writeSize);
      fileSize = Long.valueOf(System.getProperty("fileSize", String.valueOf(1L * 1024L * 1024L * 1024L)));
      System.out.println("file size: " + fileSize);
    }
  }

  static class ThreadsWriteRunner extends WriteRunner {

    protected ThreadsWriteRunner(FileSystem fs) {
      super(fs);
    }

    @Override
    protected void prepare() throws Exception {
      super.prepare();
      parallel = Integer.valueOf(System.getProperty("threads", "1"));
      System.out.println("parallel: " + parallel);
    }

    @Override
    public void run() throws Exception {
      prepare();

      ExecutorService executor = Executors.newFixedThreadPool(parallel);
      List<Future<Double>> futures = new ArrayList<>();
      for (int i = 0; i < parallel; i++) {
        Future<Double> future = executor.submit(new WriteTask(i, fs, path + "/thread" + i, writeSize, fileSize));
        futures.add(future);
      }

      double total = 0.0;
      for (Future<Double> future : futures) {
        total += future.get();
      }
      System.out.println(FINAL_WRITE_PERF_PREFIX + total);
      executor.shutdownNow();
    }
  }

  static class JvmsWriteRunner extends WriteRunner {
    private String async;

    protected JvmsWriteRunner(String async) {
      super(null);
      this.async = async;
    }

    @Override
    protected void prepare() throws Exception {
      super.prepare();
      parallel = Integer.valueOf(System.getProperty("jvms", "1"));
      System.out.println("parallel: " + parallel);
    }

    @Override
    public void run() throws Exception {
      prepare();

      List<ShellExecutor> executors = new ArrayList<>();
      for (int i = 0; i < parallel; i++) {
        File dir = new File(".", i + "");
        if (!dir.exists()) {
          dir.mkdir();
        }
        File out = new File(dir, "out");
        File err = new File(dir, "err");

        List<String> list = new ArrayList<>();
        list.add(scriptPath + "/java-test.sh");
        list.add("write");
        list.add("jvm");
        list.add(out.getAbsolutePath());
        list.add(err.getAbsolutePath());
        list.add("-DwriteSize=" + writeSize);
        list.add("-Dpid=" + System.getProperty("pid"));
        list.add("-Duid=" + System.getProperty("uid"));
        list.add("-Dthreads=" + jvmThreads);
        list.add("-DfileSize=" + fileSize);
        list.add("-Dseq=" + i);
        list.add("-Dapi=" + api);
	list.add("-DscriptPath=" + scriptPath);
        list.add("-D" + Constants.DAOS_IO_ASYNC + "=" + async);
        executors.add(new ShellExecutor(i, list, out, err, WRITE_PERF_PREFIX, FINAL_WRITE_PERF_PREFIX));
      }
      for (ShellExecutor executor : executors) {
        executor.execute();
      }
      Double total = 0.0;
      for (ShellExecutor executor : executors) {
        int rc = executor.process.waitFor();
        if (rc != 0) {
          System.out.println(executor.cmdList + " failed. ignoring it");
          System.out.println(executor.getError());
          continue;
        }
        total += executor.getPerf();
      }
      System.out.println(FINAL_WRITE_PERF_PREFIX + total);
    }
  }

  static class ShellExecutor {
    int id;
    ProcessBuilder pb = new ProcessBuilder();
    File out;
    File err;
    Process process;
    List<String> cmdList;
    String resultPrefix;
    String finalRstPrefix;

    ShellExecutor(int id, List<String> cmdList, File out, File err, String resultPrefix, String finalRstPrefix) {
      this.id = id;
      this.cmdList = cmdList;
      pb.command(cmdList);
      this.out = out;
      this.err = err;
      this.resultPrefix = resultPrefix;
      this.finalRstPrefix = finalRstPrefix;
    }

    public void execute() throws Exception {
      process = pb.start();
    }

    public Double getPerf() throws Exception {
      try (BufferedReader br = new BufferedReader(new FileReader(out))) {
        String line;
        while ((line = br.readLine()) != null) {
          int index = line.indexOf(finalRstPrefix);
          if (index >= 0) {
            System.out.println(line);
            return Double.valueOf(line.substring(index + finalRstPrefix.length()));
          }
          if (line.startsWith(resultPrefix)) {
            System.out.println(line);
          }
        }
      }
      throw new Exception("failed to get perf of " + cmdList + "\n" + getError());
    }

    public String getError() throws Exception {
      StringBuilder sb = new StringBuilder();
      try (BufferedReader br = new BufferedReader(new FileReader(err))) {
        String line;
        while ((line = br.readLine()) != null) {
          sb.append(line);
          sb.append('\n');
        }
      }
      return sb.toString();
    }
  }

  static abstract class ReadRunner extends Runner {
    protected int readSize;
    protected boolean random;
    protected long fileSize;

    protected ReadRunner(FileSystem fs) {
      super(fs);
    }

    @Override
    protected void prepare() throws Exception {
      super.prepare();
      readSize = Constants.DEFAULT_DAOS_READ_BUFFER_SIZE;
      System.out.println("read buffer size: " + readSize);
      random = "true".equalsIgnoreCase(System.getProperty("random"));
      System.out.println("random: " + random);
      fileSize = Long.valueOf(System.getProperty("fileSize", String.valueOf(1L * 1024L * 1024L * 1024L)));
      System.out.println("file size: " + fileSize);
    }
  }

  static class ThreadsReadRunner extends ReadRunner {

    protected ThreadsReadRunner(FileSystem fs) {
      super(fs);
    }

    @Override
    protected void prepare() throws Exception {
      super.prepare();
      parallel = Integer.valueOf(System.getProperty("threads", "1"));
      System.out.println("parallel: " + parallel);
    }

    @Override
    public void run() throws Exception {
      prepare();

      ExecutorService executor = Executors.newFixedThreadPool(parallel);
      List<Future<Double>> futures = new ArrayList<>();
      for (int i = 0; i < parallel; i++) {
        Future<Double> future = executor.submit(
            new ReadTask(i, fs, path + "/thread" + i, readSize, random, fileSize));
        futures.add(future);
      }

      double total = 0.0;
      for (Future<Double> future : futures) {
        total += future.get();
      }
      System.out.println(FINAL_READ_PERF_PREFIX + total);
      executor.shutdownNow();
    }
  }

  static class JvmsReadRunner extends ReadRunner {
    private String async;
    protected JvmsReadRunner(String async) {
      super(null);
      this.async = async;
    }

    @Override
    protected void prepare() throws Exception {
      super.prepare();
      parallel = Integer.valueOf(System.getProperty("jvms", "1"));
      System.out.println("parallel: " + parallel);
    }

    @Override
    public void run() throws Exception {
      prepare();

      List<ShellExecutor> executors = new ArrayList<>();
      for (int i = 0; i < parallel; i++) {
        File dir = new File(".", i + "");
        if (!dir.exists()) {
          dir.mkdir();
        }
        File out = new File(dir, "out");
        File err = new File(dir, "err");
        List<String> list = new ArrayList<>();
        list.add(scriptPath + "/java-test.sh");
        list.add("read");
        list.add("jvm");
        list.add(out.getAbsolutePath());
        list.add(err.getAbsolutePath());
        list.add("-DreadSize=" + readSize);
        list.add("-Dpid=" + System.getProperty("pid"));
        list.add("-Duid=" + System.getProperty("uid"));
        list.add("-Dthreads=" + jvmThreads);
        list.add("-Dseq=" + i);
        list.add("-Drandom=" + (random ? "true" : "false"));
        list.add("-Dapi=" + api);
	list.add("-DscriptPath=" + scriptPath);
        list.add("-D" + Constants.DAOS_IO_ASYNC + "=" + async);
        executors.add(new ShellExecutor(i, list, out, err, READ_PERF_PREFIX, FINAL_READ_PERF_PREFIX));
      }
      for (ShellExecutor executor : executors) {
        executor.execute();
      }
      Double total = 0.0;
      for (ShellExecutor executor : executors) {
        int rc = executor.process.waitFor();
        if (rc != 0) {
          System.out.println(executor.cmdList + " failed. ignoring it");
          System.out.println(executor.getError());
          continue;
        }
        total += executor.getPerf();
      }
      System.out.println(FINAL_READ_PERF_PREFIX + total);
    }
  }


  static class WriteTask implements Callable<Double> {
    int id;
    FileSystem fs;
    String writePath;
    int writeSize;
    long fileSize;

    WriteTask(int id, FileSystem fs, String writePath, int writeSize, long fileSize) {
      this.id = id;
      this.fs = fs;
      this.writePath = writePath;
      this.writeSize = writeSize;
      this.fileSize = fileSize;
    }

    @Override
    public Double call() throws Exception {
      //write
      byte[] bytes = new byte[writeSize];

      int v = 0;
      for (int i = 0; i < bytes.length; i++) {
        bytes[i] = (byte) ((v++) % 255);
      }

      long count = 0;
      long start = System.currentTimeMillis();
      try (FSDataOutputStream fos = fs.create(new Path(writePath), true)) {
        while (count < fileSize) {
          fos.write(bytes);
          count += bytes.length;
        }
      } catch (Exception e) {
        e.printStackTrace();
      }
      long end = System.currentTimeMillis();

      double rst = count * 1.0 / ((end - start) * 1.0 / 1000);
      System.out.println("write path: " + writePath);
      System.out.println(WRITE_PERF_PREFIX + "file size(" + id + "): " + count);
      System.out.println(WRITE_PERF_PREFIX + "time(" + id + "): " + ((end - start) * 1.0 / 1000));
      System.out.println(WRITE_PERF_PREFIX + "perf(" + id + "): " + rst);
      return rst;
    }
  }

  static class ReadTask implements Callable<Double> {
    int id;
    FileSystem fs;
    String readPath;
    int readSize;
    boolean random;
    long fileSize;

    ReadTask(int id, FileSystem fs, String readPath, int readSize, boolean random, long fileSize) {
      this.id = id;
      this.fs = fs;
      this.readPath = readPath;
      this.readSize = readSize;
      this.random = random;
      this.fileSize = fileSize;
    }

    @Override
    public Double call() throws Exception {
      //read
      long count = 0;
      int times = 0;
      byte[] bytes = new byte[readSize];

      long start = System.currentTimeMillis();
      long end;

      String api = System.getProperty("api", "hadoop-api");

      if ("java-api".equalsIgnoreCase(api)) {
        String poolId = System.getProperty("pid");
        String contId = System.getProperty("uid");
        DaosFsClient client = new DaosFsClient.DaosFsClientBuilder().poolId(poolId).containerId(contId)
            .build();

        start = System.currentTimeMillis();

        DaosFile file = client.getFile(readPath);
        long size;
        ByteBuf byteBuffer = BufferAllocator.directNettyBuf(readSize);
        if (random) {
          Random rd = new Random();

          while (count < fileSize) {
            long offset = (long) (fileSize * rd.nextFloat());
            size = file.read(byteBuffer, 0, offset,
                offset + readSize > fileSize ? (fileSize - offset) : readSize);
//            size = fos.read(bytes);
            count += size;
            times++;
          }
        } else {
          while (count < fileSize) {
            size = file.read(byteBuffer, 0, count, readSize);
            if (size < 0) {
              break;
            }
            count += size;
            times++;
          }
        }
        end = System.currentTimeMillis();
      } else {
        int size;
        try (FSDataInputStream fos = fs.open(new Path(readPath))) {
          if (random) {
            Random rd = new Random();
            while (count < fileSize) {
              long offset = (long) (fileSize * rd.nextFloat());
              fos.seek(offset);
              size = fos.read(bytes, 0, (int) (offset + readSize > fileSize ? (fileSize - offset) : readSize));
//            size = fos.read(bytes);
              count += size;
              times++;
            }
          } else {
            while (count < fileSize) {
              size = fos.read(bytes);
              if (size < 0) {
                break;
              }
              count += size;
              times++;
            }
          }
          end = System.currentTimeMillis();
        } catch (Exception e) {
          e.printStackTrace();
          end = System.currentTimeMillis();
        }
      }


      double rst = count * 1.0 / ((end - start) * 1.0 / 1000);
      System.out.println("api: " + api);
      System.out.println("read path: " + readPath);
      System.out.println("number of reads: " + times);
      System.out.println(READ_PERF_PREFIX + "file size(" + id + "): " + count);
      System.out.println(READ_PERF_PREFIX + "time(" + id + "): " + ((end - start) * 1.0 / 1000));
      System.out.println(READ_PERF_PREFIX + "perf(" + id + "): " + rst);
      return rst;
    }
  }
}
