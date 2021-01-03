package io.daos;

import org.junit.Assert;
import org.junit.Test;

public class DaosIOExceptionTest {

  @Test
  public void testLoadErrorCode() {
    Assert.assertEquals(6, DaosIOException.errorMap.size());
    ErrorCode errorCode = Constants.CUSTOM_ERR_NO_POOL_SIZE;
    Assert.assertEquals(errorCode.getMsg(), DaosIOException.errorMap.get(errorCode.getCode()));

    errorCode = Constants.CUSTOM_ERR_TOO_LONG_VALUE;
    Assert.assertEquals(errorCode.getMsg(), DaosIOException.errorMap.get(errorCode.getCode()));
  }

  @Test
  public void testParseCustomError() {
    ErrorCode errorCode = Constants.CUSTOM_ERR_INCORRECT_SVC_REPLICS;
    String m = "test get message";
    DaosIOException exception = new DaosIOException(m, errorCode.getCode(), errorCode.getMsg());
    String msg = exception.getMessage();
    String lmsg = exception.getLocalizedMessage();
    String tmsg = exception.toString();

    Assert.assertEquals(msg, lmsg);
    Assert.assertEquals(tmsg, lmsg);

    Assert.assertTrue(msg.contains(m));
    Assert.assertTrue(msg.contains(errorCode.getMsg()));
    Assert.assertTrue(msg.contains(String.valueOf(errorCode.getCode())));
  }

  @Test
  public void testParseDaosError() {
    String m = "test get message";
    String dm = "daos error message";
    DaosIOException exception = new DaosIOException(m, 1001, dm);
    String msg = exception.getMessage();
    String lmsg = exception.getLocalizedMessage();
    String tmsg = exception.toString();

    Assert.assertEquals(msg, lmsg);
    Assert.assertEquals(tmsg, lmsg);

    Assert.assertTrue(msg.contains(m));
    Assert.assertTrue(msg.contains(dm));
    Assert.assertTrue(msg.contains("1001"));
  }
}
