/**
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.fs.hadoop;

import io.daos.BufferAllocator;
import io.daos.dfs.DaosFile;
import io.netty.buffer.ByteBuf;
import io.netty.buffer.ByteBufAllocator;
import org.apache.hadoop.fs.FileSystem;
import org.junit.Assert;
import org.junit.Test;
import org.mockito.ArgumentCaptor;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.Arrays;

import static org.mockito.Mockito.*;

public class DaosOutputStreamTest {

  @Test(expected = IllegalArgumentException.class)
  public void testNonDirectBuffer() throws Exception {
    FileSystem.Statistics stats = mock(FileSystem.Statistics.class);
    DaosOutputStream dos = new DaosOutputStream(null, ByteBufAllocator.DEFAULT.heapBuffer(), stats,
        false);
    dos.close();
  }

  @Test(expected = IllegalArgumentException.class)
  public void testWriteLenIllegal() throws Exception {
    DaosFile file = mock(DaosFile.class);
    FileSystem.Statistics stats = mock(FileSystem.Statistics.class);
    DaosOutputStream os = new DaosOutputStream(file, 100, stats, false);
    byte[] buf = new byte[10];
    os.write(buf, 0, -10);
  }

  @Test(expected = IndexOutOfBoundsException.class)
  public void testWriteOverflowException() throws Exception {
    DaosFile file = mock(DaosFile.class);
    FileSystem.Statistics stats = mock(FileSystem.Statistics.class);
    DaosOutputStream os = new DaosOutputStream(file, 100, stats, false);
    byte[] buf = new byte[10];
    os.write(buf, 0, 200);
  }

  private void writeNoMoreThanBuffer(int writeLen, int writeTimes) throws Exception {
    int bufCap = 100;
    DaosFile file = mock(DaosFile.class);
    FileSystem.Statistics stats = mock(FileSystem.Statistics.class);
    ByteBuf byteBuffer = BufferAllocator.directNettyBuf(bufCap);
    ByteBuf sbuffer = spy(byteBuffer);
    doReturn(true).when(sbuffer).release();
    DaosOutputStream daos = new DaosOutputStream(file, sbuffer, stats, false);
    byte buf[] = new byte[writeLen];
    for (int i = 0; i < buf.length; i++) {
      buf[i] = (byte) i;
    }

    daos.write(buf, 0, buf.length);

    boolean less = writeLen < bufCap;
    boolean mod = writeLen % bufCap == 0;
    int fullBufferTimes = less ? 0 : (mod ? writeTimes : writeTimes - 1);
    verify(file, times(fullBufferTimes)).write(sbuffer, 0L, 0L,
        bufCap);
    daos.close();
    verify(sbuffer, times(fullBufferTimes)).writeBytes(buf, 0, bufCap);
    verify(sbuffer, times(writeTimes - fullBufferTimes)).writeBytes(buf, 0,
        writeLen % bufCap);
    verify(file, times(writeTimes - fullBufferTimes)).write(sbuffer, 0L, 0L,
        writeLen % bufCap);

    byteBuffer.release();
  }

  @Test
  public void testWriteLessThanBuffer() throws Exception {
    writeNoMoreThanBuffer(80, 1);
  }

  @Test
  public void testWriteEqualToBuffer() throws Exception {
    writeNoMoreThanBuffer(100, 1);
  }

  public void writeOne(boolean flush) throws Exception {
    int bufCap = 100;
    DaosFile file = mock(DaosFile.class);
    FileSystem.Statistics stats = mock(FileSystem.Statistics.class);
    ByteBuf byteBuffer = BufferAllocator.directNettyBuf(bufCap);
    ByteBuf sbuffer = spy(byteBuffer);
    doReturn(true).when(sbuffer).release();
    DaosOutputStream daos = new DaosOutputStream(file, sbuffer, stats, false);

    daos.write('e');

    verify(file, times(0)).write(sbuffer, 0L, 0L,
        bufCap);
//    verify(sbuffer, times(1)).writeByte((byte) 'e');

    if (flush) {
      daos.flush();
    } else {
      daos.close();
    }
    verify(file, times(1)).write(sbuffer, 0L, 0L, 1);
    byteBuffer.release();
  }

  @Test
  public void testWriteOneByte() throws Exception {
    writeOne(false);
  }

  @Test
  public void testFlush() throws Exception {
    writeOne(true);
  }

  private void writeMoreThanBuffer(int writeLen, int writeTimes) throws Exception {
    int bufCap = 100;
    DaosFile file = mock(DaosFile.class);
    FileSystem.Statistics stats = mock(FileSystem.Statistics.class);
    ByteBuf byteBuffer = BufferAllocator.directNettyBuf(bufCap);
    ByteBuf sbuffer = spy(byteBuffer);
    doReturn(true).when(sbuffer).release();
    DaosOutputStream daos = new DaosOutputStream(file, sbuffer, stats, false);
    byte buf[] = new byte[writeLen];
    for (int i = 0; i < buf.length; i++) {
      buf[i] = (byte) i;
    }

    when(file.write(any(ByteBuf.class), anyLong(), anyLong(), eq((long) bufCap))).thenReturn((long) bufCap);
    when(file.write(any(ByteBuf.class), anyLong(), anyLong(),
        eq((long) writeLen % bufCap))).thenReturn((long) writeLen % bufCap);
    daos.write(buf, 0, buf.length);

    boolean less = writeLen < bufCap;
    boolean mod = writeLen % bufCap == 0;
    int fullBufferTimes = less ? 0 : (mod ? writeTimes : writeTimes - 1);

    ArgumentCaptor<Long> writeCaptor1 = ArgumentCaptor.forClass(Long.class);
    verify(file, times(fullBufferTimes)).write(eq(sbuffer), eq(0L), writeCaptor1.capture(),
        eq((long) bufCap));

    ArgumentCaptor<Integer> bufCaptor1 = ArgumentCaptor.forClass(Integer.class);
    verify(sbuffer, times(fullBufferTimes)).writeBytes(eq(buf), bufCaptor1.capture(), eq(bufCap));
    ArgumentCaptor<Integer> bufCaptor2 = ArgumentCaptor.forClass(Integer.class);
    verify(sbuffer, times(writeTimes - fullBufferTimes)).writeBytes(eq(buf),
        bufCaptor2.capture(),
        eq(writeLen % bufCap));

    daos.close();

    ArgumentCaptor<Long> writeCaptor2 = ArgumentCaptor.forClass(Long.class);
    verify(file, times(writeTimes - fullBufferTimes)).write(eq(sbuffer),
        eq(0L), writeCaptor2.capture(), eq((long) writeLen % bufCap));

    Long write1Exp[] = new Long[fullBufferTimes];
    for (int i = 0; i < write1Exp.length; i++) {
      write1Exp[i] = (long) i * bufCap;
    }
    Assert.assertTrue(Arrays.equals(write1Exp, writeCaptor1.getAllValues().toArray(new Long[write1Exp.length])));

    Integer buf1Exp[] = new Integer[fullBufferTimes];
    for (int i = 0; i < write1Exp.length; i++) {
      buf1Exp[i] = i * bufCap;
    }
    Assert.assertTrue(Arrays.equals(buf1Exp, bufCaptor1.getAllValues().toArray(new Integer[buf1Exp.length])));

    if (!mod) {
      Assert.assertEquals(fullBufferTimes * bufCap, bufCaptor2.getValue().intValue());
      Assert.assertEquals(fullBufferTimes * bufCap, writeCaptor2.getValue().intValue());
    }
    byteBuffer.release();
  }

  @Test
  public void testWriteLargerThanBuffer() throws Exception {
    writeMoreThanBuffer(200, 2);
  }

  @Test
  public void testWriteLargerThanBufferWithRemaining() throws Exception {
    writeMoreThanBuffer(535, 6);
  }

  private DaosOutputStream close() throws Exception {
    int bufCap = 100;
    DaosFile file = mock(DaosFile.class);
    FileSystem.Statistics stats = mock(FileSystem.Statistics.class);
    ByteBuf byteBuffer = BufferAllocator.directNettyBuf(bufCap);
    ByteBuf sbuffer = spy(byteBuffer);
    doReturn(true).when(sbuffer).release();
    DaosOutputStream daos = new DaosOutputStream(file, sbuffer, stats, false);

    daos.close();
    daos.close();
    verify(file, times(1)).release();
    byteBuffer.release();
    return daos;
  }

  @Test
  public void testClose() throws Exception {
    close();
  }

  @Test(expected = IOException.class)
  public void testOperationAfterClose() throws Exception {
    DaosOutputStream dos = close();
    dos.write('e');
  }
}
