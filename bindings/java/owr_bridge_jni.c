#include <jni.h>
#include <owr_bridge.h>

void Java_com_ericsson_research_owr_OwrBridge_start
    (JNIEnv* env, jclass clazz)
{
    owr_bridge_start_in_thread();
}
