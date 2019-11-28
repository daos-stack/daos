package com.intel.daos;

import java.io.IOException;

/**
 * This exception is thrown form JNI code. It probably occurs in daos native
 * functions, and has a return code. Please refer to cart/errno.h for meaning
 * of each return code.
 */
public class DaosNativeException extends IOException {
  protected DaosNativeException(String message) {
    super(message);
  }
}
