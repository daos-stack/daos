package com.intel.daos;

import java.io.IOException;

/**
 * This exception is thrown from java code. It probably indicates wrong use
 * of this package, and cause inconsistent status.
 *
 */
public class DaosJavaException extends IOException {
  protected DaosJavaException(String message) {
    super(message);
  }
}
