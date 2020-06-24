package io.daos.obj;

import io.daos.DaosClient;
import org.junit.Assert;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mockito;
import org.powermock.api.mockito.PowerMockito;
import org.powermock.core.classloader.annotations.PowerMockIgnore;
import org.powermock.core.classloader.annotations.PrepareForTest;
import org.powermock.core.classloader.annotations.SuppressStaticInitializationFor;
import org.powermock.modules.junit4.PowerMockRunner;
import sun.nio.ch.DirectBuffer;

import java.util.Collections;

@RunWith(PowerMockRunner.class)
@PowerMockIgnore("javax.management.*")
@PrepareForTest({DaosObject.class, DaosObjClient.class})
@SuppressStaticInitializationFor("io.daos.DaosClient")
public class DaosObjectTest {

  @Test
  public void testObjectIdEncode() throws Exception {
    DaosObjectId oid = new DaosObjectId();

    Exception ee = null;
    try {
      new DaosObject(Mockito.mock(DaosObjClient.class), oid);
    } catch (Exception e) {
      ee = e;
    }
    Assert.assertNotNull(ee);
    Assert.assertTrue(ee instanceof IllegalArgumentException);
    Assert.assertTrue(ee.getMessage().contains("DAOS object ID should be encoded"));
  }

  @Test
  public void testPunchEmptyDkeys() throws Exception {
    PowerMockito.mockStatic(DaosObjClient.class);
    DaosObjectId oid = new DaosObjectId();
    oid.encode();
    DaosObjClient client = Mockito.mock(DaosObjClient.class);
    long contPtr = 12345;
    long address = ((DirectBuffer) oid.getBuffer()).address();
    long objPtr = 6789;
    Mockito.when(client.getContPtr()).thenReturn(contPtr);
    Mockito.when(client.openObject(contPtr, address, OpenMode.UNKNOWN.getValue())).thenReturn(objPtr);
    DaosObject object = new DaosObject(client, oid);
    Exception ee = null;
    try {
      object.open();
      object.punchDkeys(Collections.EMPTY_LIST);
    } catch (Exception e) {
      ee = e;
    }
    Assert.assertNotNull(ee);
    Assert.assertTrue(ee instanceof DaosObjectException);
    Assert.assertTrue(ee.getMessage().contains("no dkeys specified when punch dkeys"));
  }

  @Test
  public void testPunchEmptyAkeys() throws Exception {
    PowerMockito.mockStatic(DaosObjClient.class);
    DaosObjectId oid = new DaosObjectId();
    oid.encode();
    DaosObjClient client = Mockito.mock(DaosObjClient.class);
    long contPtr = 12345;
    long address = ((DirectBuffer) oid.getBuffer()).address();
    long objPtr = 6789;
    Mockito.when(client.getContPtr()).thenReturn(contPtr);
    Mockito.when(client.openObject(contPtr, address, OpenMode.UNKNOWN.getValue())).thenReturn(objPtr);
    DaosObject object = new DaosObject(client, oid);
    Exception ee = null;
    try {
      object.open();
      object.punchAkeys("dkey1", Collections.EMPTY_LIST);
    } catch (Exception e) {
      ee = e;
    }
    Assert.assertNotNull(ee);
    Assert.assertTrue(ee instanceof DaosObjectException);
    Assert.assertTrue(ee.getMessage().contains("no akeys specified when punch akeys"));
  }
}
