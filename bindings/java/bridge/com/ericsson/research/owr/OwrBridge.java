package com.ericsson.research.owr;

public class OwrBridge {
    static {
        System.loadLibrary("openwebrtc_jni");
        System.loadLibrary("openwebrtc_bridge_jni");
    }
    public static native void start();
}
