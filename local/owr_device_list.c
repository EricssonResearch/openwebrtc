/*
 * Copyright (c) 2014, Ericsson AB. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */

/*/
\*\ OwrDeviceList
/*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gstinfo.h>

#include "owr_device_list_private.h"
#include "owr_local_media_source_private.h"
#include "owr_media_source.h"
#include "owr_types.h"
#include "owr_utils.h"
#include "owr_private.h"

#ifdef __APPLE__
#include <TargetConditionals.h>
#include "owr_device_list_avf_private.h"

#elif defined(__ANDROID__)
#include <assert.h>
#include <jni.h>
#include <stdlib.h>
#include <dlfcn.h>

#elif defined(__linux__)
#include <fcntl.h>
#include <linux/videodev2.h>
#include <libv4l2.h>

#endif /* defined(__linux__) */

static gboolean enumerate_video_source_devices(GClosure *);
static gboolean enumerate_audio_source_devices(GClosure *);

typedef struct {
    GClosure *callback;
    GList *list;
} CallbackAndList;

static gboolean cb_call_closure_with_list_later(CallbackAndList *cal)
{
    _owr_utils_call_closure_with_list(cal->callback, cal->list);

    g_slice_free(CallbackAndList, cal);

    return FALSE;
}

static void call_closure_with_list_later(GClosure *callback, GList *list)
{
    CallbackAndList *cal;

    cal = g_slice_new0(CallbackAndList);
    cal->callback = callback;
    cal->list = list;

    _owr_schedule_with_user_data((GSourceFunc) cb_call_closure_with_list_later, cal);
}

void _owr_get_capture_devices(OwrMediaType types, GClosure *callback)
{
    GClosure *merger;

    g_return_if_fail(callback);

    if (G_CLOSURE_NEEDS_MARSHAL(callback)) {
        g_closure_set_marshal(callback, g_cclosure_marshal_generic);
    }

    merger = _owr_utils_list_closure_merger_new(callback);

    if (types & OWR_MEDIA_TYPE_VIDEO) {
        g_closure_ref(merger);
        _owr_schedule_with_user_data((GSourceFunc) enumerate_video_source_devices, merger);
    }

    if (types & OWR_MEDIA_TYPE_AUDIO) {
        g_closure_ref(merger);
        _owr_schedule_with_user_data((GSourceFunc) enumerate_audio_source_devices, merger);
    }

    g_closure_unref(merger);
}



#if defined(__APPLE__)

static gboolean enumerate_video_source_devices(GClosure *callback)
{
    _owr_utils_call_closure_with_list(callback, _get_avf_video_sources());
    return FALSE;
}

static gboolean enumerate_audio_source_devices(GClosure *callback)
{
    _owr_utils_call_closure_with_list(callback, _get_avf_audio_sources());
    return FALSE;
}

#endif /*defined(__APPLE__)*/



#if defined(__linux__) && !defined(__ANDROID__)

#include <pulse/glib-mainloop.h>
#include <pulse/pulseaudio.h>

typedef struct {
    GClosure *callback;
    GList *list;
    pa_glib_mainloop *mainloop;
    pa_context *pa_context;
    gboolean it_sources;
} AudioListContext;

static void on_pulse_context_state_change(pa_context *, AudioListContext *);
static void source_info_iterator(pa_context *, const pa_source_info *, int eol, AudioListContext *);

static void finish_pa_list(AudioListContext *context)
{
    /* Schedule the callback in Owr mainloop context */
    call_closure_with_list_later(context->callback, context->list);

    g_slice_free(AudioListContext, context);
}

static gboolean enumerate_audio_source_devices(GClosure *callback)
{
    AudioListContext *context;

    context = g_slice_new0(AudioListContext);

    context->callback = callback;

    context->mainloop = pa_glib_mainloop_new(NULL);

    if (!context->mainloop) {
        g_error("PulseAudio: failed to create glib mainloop");
        goto cleanup;
    }

    context->pa_context = pa_context_new(pa_glib_mainloop_get_api(context->mainloop), "Owr");

    if (!context->pa_context) {
        g_error("PulseAudio: failed to create context");
        goto cleanup_mainloop;
    }

    pa_context_set_state_callback(context->pa_context,
        (pa_context_notify_cb_t) on_pulse_context_state_change, context);
    pa_context_connect(context->pa_context, NULL, 0, NULL);

done:
    return FALSE;

cleanup_mainloop:
    pa_glib_mainloop_free(context->mainloop);

cleanup:
    finish_pa_list(context);

    goto done;
}

static void on_pulse_context_state_change(pa_context *pa_context, AudioListContext *context)
{
    gint error;
    error = pa_context_errno(pa_context);
    if (error) {
        g_print("PulseAudio: error: %s", pa_strerror(error));
    }
    switch (pa_context_get_state(pa_context)) {
    case PA_CONTEXT_READY:
        pa_context_get_source_info_list(pa_context, (pa_source_info_cb_t) source_info_iterator, context);
        break;
    case PA_CONTEXT_FAILED:
        g_print("PulseAudio: failed to connect to daemon");
        finish_pa_list(context);
        break;
    case PA_CONTEXT_TERMINATED:
        break;
    case PA_CONTEXT_UNCONNECTED:
        break;
    case PA_CONTEXT_CONNECTING:
        break;
    case PA_CONTEXT_AUTHORIZING:
        break;
    case PA_CONTEXT_SETTING_NAME:
        break;
    default:
        break;
    }
}

static void source_info_iterator(pa_context *pa_context, const pa_source_info *info, int eol, AudioListContext *context)
{
    OWR_UNUSED(pa_context);

    if (!eol) {
        OwrLocalMediaSource *source;

        if (info->monitor_of_sink_name != NULL) {
            /* We don't want to list monitor sources */
            return;
        }

        source = _owr_local_media_source_new(info->index, info->description,
            OWR_MEDIA_TYPE_AUDIO, OWR_SOURCE_TYPE_CAPTURE);

        context->list = g_list_append(context->list, source);
    } else {
        finish_pa_list(context);
    }
}

#endif /* defined(__linux__) && !defined(__ANDROID__) */



#if defined(__ANDROID__)

static gboolean enumerate_audio_source_devices(GClosure *callback)
{
    OwrLocalMediaSource *source;

    source = _owr_local_media_source_new(-1,
        "Default audio input", OWR_MEDIA_TYPE_AUDIO, OWR_SOURCE_TYPE_CAPTURE);
    _owr_utils_call_closure_with_list(callback, g_list_append(NULL, source));

    return FALSE;
}

#endif /*defined(__ANDROID__)*/



#if (defined(__linux__) && !defined(__ANDROID__))

static gchar *get_v4l2_device_name(gchar *filename)
{
    gchar *device_name;
    int fd = 0;

    fd = open(filename, O_RDWR);

    if (fd <= 0) {
        g_print("v4l: failed to open %s", filename);

        device_name = g_strdup(filename);
    } else {
        struct v4l2_capability vcap;

        if (v4l2_ioctl(fd, VIDIOC_QUERYCAP, &vcap) < 0) {
            g_print("v4l: failed to query %s", filename);

            device_name = g_strdup(filename);
        } else {
            device_name = g_strdup((const gchar *)vcap.card);
        }
    }

    g_print("v4l: found device: %s\n", device_name);

    close(fd);

    return device_name;
}

static OwrLocalMediaSource *maybe_create_source_from_filename(const gchar *name)
{
    static GRegex *regex;
    GMatchInfo *match_info = NULL;
    OwrLocalMediaSource *source = NULL;
    gchar *index_str;
    gint index;
    gchar *filename;
    gchar *device_name;

    if (g_once_init_enter(&regex)) {
        GRegex *r;
        r = g_regex_new("^video(0|[1-9][0-9]*)$", G_REGEX_OPTIMIZE, 0, NULL);
        g_assert(r);
        g_once_init_leave(&regex, r);
    }

    if (g_regex_match(regex, name, 0, &match_info)) {
        index_str = g_match_info_fetch(match_info, 1);
        index = g_ascii_strtoll(index_str, NULL, 10);

        filename = g_strdup_printf("/dev/%s", name);
        device_name = get_v4l2_device_name(filename);
        g_free(filename);
        filename = NULL;

        source = _owr_local_media_source_new(index, device_name,
            OWR_MEDIA_TYPE_VIDEO, OWR_SOURCE_TYPE_CAPTURE);

        g_print("v4l: filename match: %s\n", device_name);

        g_free(device_name);
    }

    g_match_info_free(match_info);

    return source;
}

static gboolean enumerate_video_source_devices(GClosure *callback)
{
    OwrLocalMediaSource *source;
    GList *sources = NULL;
    GError *error = NULL;
    GDir *dev_dir;
    const gchar *filename;

    dev_dir = g_dir_open("/dev", 0, &error);

    while ((filename = g_dir_read_name(dev_dir))) {
        source = maybe_create_source_from_filename(filename);

        if (source) {
            sources = g_list_append(sources, source);
        }
    }

    g_dir_close(dev_dir);

    _owr_utils_call_closure_with_list(callback, sources);

    return FALSE;
}

#endif /*(defined(__linux__) && !defined(__ANDROID__))*/



#if defined(__ANDROID__)

#define OWR_DEVICE_LIST_JNI_VERSION JNI_VERSION_1_6
#define OWR_DEVICE_LIST_MIN_SDK_VERSION 9

#define ANDROID_RUNTIME_DALVIK_LIB "libdvm.so"
#define ANDROID_RUNTIME_ART_LIB "libart.so"

typedef jint (*JNI_GetCreatedJavaVMs)(JavaVM **vmBuf, jsize bufLen, jsize *nVMs);

static void cache_java_classes(JNIEnv *);

static const char *const android_runtime_libs[] = {
    ANDROID_RUNTIME_ART_LIB,
    ANDROID_RUNTIME_DALVIK_LIB,
    NULL
};

static pthread_key_t detach_key = 0;

static void on_java_detach(JavaVM *jvm)
{
    g_return_if_fail(jvm);

    g_print("%s detached thread(%ld) from Java VM", __FUNCTION__, pthread_self());
    (*jvm)->DetachCurrentThread(jvm);
    pthread_setspecific(detach_key, NULL);
}

static int get_android_sdk_version(JNIEnv *env)
{
    jfieldID field_id = NULL;
    jint version;
    static jclass ref;

    assert(env);

    if (g_once_init_enter(&ref)) {
        jclass c = (*env)->FindClass(env, "android/os/Build$VERSION");
        g_assert(c);

        field_id = (*env)->GetStaticFieldID(env, c, "SDK_INT", "I");
        g_assert(field_id);

        g_once_init_leave(&ref, c);
    }

    version = (*env)->GetStaticIntField(env, ref, field_id);
    g_print("android device list: android sdk version is: %d\n", version);

    (*env)->DeleteLocalRef(env, ref);

    return version;
}

static JNIEnv* get_jni_env_from_jvm(JavaVM *jvm)
{
    JNIEnv *env = NULL;
    int err;

    g_return_val_if_fail(jvm, NULL);

    err = (*jvm)->GetEnv(jvm, (void**)&env, OWR_DEVICE_LIST_JNI_VERSION);

    if (JNI_EDETACHED == err) {
        err = (*jvm)->AttachCurrentThread(jvm, &env, NULL);

        if (err) {
            g_error("android device list: failed to attach current thread");
        }

        g_print("attached thread (%ld) to jvm\n", pthread_self());

        if (pthread_key_create(&detach_key, (void (*)(void *)) on_java_detach)) {
            g_warning("android device list: failed to set on_java_detach");
        }

        pthread_setspecific(detach_key, env);
    } else if (JNI_OK != err) {
        g_error("jvm->GetEnv() failed");
    }

    return env;
}

static void init_jni(JavaVM *jvm)
{
    JNIEnv *env;
    int sdk_version;

    env = get_jni_env_from_jvm(jvm);

    sdk_version = get_android_sdk_version(env);

    if (sdk_version < OWR_DEVICE_LIST_MIN_SDK_VERSION) {
        g_error("android version is %d, owr_device_list needs > %d",
            sdk_version, OWR_DEVICE_LIST_MIN_SDK_VERSION);
    }

    cache_java_classes(env);
}

static JavaVM *get_java_vm()
{
    JNI_GetCreatedJavaVMs get_created_java_vms;
    gpointer handle = NULL;
    static JavaVM *jvm = NULL;
    const gchar *error_string;
    jsize num_jvms = NULL;
    gint lib_index = 0;
    gint err;

    while (android_runtime_libs[lib_index] && !handle) {
        dlerror();
        handle = dlopen(android_runtime_libs[lib_index], RTLD_LOCAL | RTLD_LAZY);
        error_string = dlerror();

        if (error_string) {
            g_print("failed to load %s: %s\n", android_runtime_libs[lib_index], error_string);
        }

        if (handle) {
            g_print("Android runtime loaded from %s\n", android_runtime_libs[lib_index]);
        } else {
            ++lib_index;
        }
    }

    if (handle) {
        dlerror();
        *(void **) (&get_created_java_vms) = dlsym(handle, "JNI_GetCreatedJavaVMs");
        error_string = dlerror();

        if (error_string) {
            g_error("dlsym(\"JNI_GetCreatedJavaVMs\") failed: %s", error_string);
        }

        get_created_java_vms(&jvm, 1, &num_jvms);

        if (num_jvms < 1) {
            g_print("get_created_java_vms returned %d jvms", num_jvms);
        } else {
            g_print("found existing jvm");
        }

        err = dlclose(handle);
        if (err) {
            g_warning("dlclose() of android runtime handle failed");
        }
    } else {
        g_error("Failed to get jvm");
    }

    return jvm;
}

/* jvm needs to be fetched once, jni env needs to be fetched once per thread */
static JNIEnv* get_jni_env()
{
    static JavaVM *jvm = NULL;

    if (g_once_init_enter(&jvm)) {
        JavaVM *vm;
        vm = get_java_vm();
        init_jni(vm);
        g_once_init_leave(&jvm, vm);
    }

    g_return_val_if_fail(jvm, NULL);

    return get_jni_env_from_jvm(jvm);
}

static struct {
    jobject class;
    jmethodID getNumberOfCameras;
    jmethodID getCameraInfo;
} Camera;

static void cache_class_camera(JNIEnv *env)
{
    jclass ref = NULL;

    ref = (*env)->FindClass(env, "android/hardware/Camera");
    g_assert(ref);

    Camera.class = (*env)->NewGlobalRef(env, ref);
    (*env)->DeleteLocalRef(env, ref);
    g_assert(Camera.class);

    Camera.getNumberOfCameras = (*env)->GetStaticMethodID(env, Camera.class, "getNumberOfCameras", "()I");
    g_assert(Camera.getNumberOfCameras);

    Camera.getCameraInfo = (*env)->GetStaticMethodID(env, Camera.class, "getCameraInfo", "(ILandroid/hardware/Camera$CameraInfo;)V");
    g_assert(Camera.getCameraInfo);
}

static struct {
    jobject class;
    jmethodID constructor;
    jfieldID facing;
    jint CAMERA_FACING_BACK;
    jint CAMERA_FACING_FRONT;
} CameraInfo;

static void cache_class_camera_info(JNIEnv *env)
{
    jclass ref = NULL;

    jfieldID field_id;

    ref = (*env)->FindClass(env, "android/hardware/Camera$CameraInfo");
    g_assert(ref);

    CameraInfo.class = (*env)->NewGlobalRef(env, ref);
    (*env)->DeleteLocalRef(env, ref);
    g_assert(CameraInfo.class);

    CameraInfo.constructor = (*env)->GetMethodID(env, CameraInfo.class, "<init>", "()V");

    CameraInfo.facing = (*env)->GetFieldID(env, CameraInfo.class, "facing", "I");

    field_id = (*env)->GetStaticFieldID(env, CameraInfo.class, "CAMERA_FACING_BACK", "I");
    CameraInfo.CAMERA_FACING_BACK = (*env)->GetStaticIntField(env, CameraInfo.class, field_id);
    g_assert(field_id);

    field_id = (*env)->GetStaticFieldID(env, CameraInfo.class, "CAMERA_FACING_FRONT", "I");
    CameraInfo.CAMERA_FACING_FRONT = (*env)->GetStaticIntField(env, CameraInfo.class, field_id);
    g_assert(field_id);
}

static void cache_java_classes(JNIEnv *env)
{
    cache_class_camera(env);
    cache_class_camera_info(env);
}

static gint get_number_of_cameras()
{
    JNIEnv *env = get_jni_env();
    return (*env)->CallStaticIntMethod(env, Camera.class, Camera.getNumberOfCameras);
}

static gchar *get_camera_name(gint camera_index)
{
    jint facing;
    jobject camera_info_instance;
    JNIEnv *env;

    env = get_jni_env();
    g_return_val_if_fail(env, NULL);

    camera_info_instance = (*env)->NewObject(env, CameraInfo.class, CameraInfo.constructor);
    if ((*env)->ExceptionCheck(env)) {
        g_error("android device list: failed to create CameraInfo object");
    }

    (*env)->CallStaticVoidMethod(env, Camera.class, Camera.getCameraInfo, camera_index, camera_info_instance);
    facing = (*env)->GetIntField(env, camera_info_instance, CameraInfo.facing);
    (*env)->DeleteLocalRef(env, camera_info_instance);

    if (facing == CameraInfo.CAMERA_FACING_FRONT) {
        g_print("found front facing camera\n");
        return g_strdup("Front facing Camera");
    } else if (facing == CameraInfo.CAMERA_FACING_BACK) {
        g_print("found back facing camera\n");
        return g_strdup("Back facing Camera");
    } else {
        g_print("found camera with unknown position\n");
        return g_strdup("Unknown Camera");
    }
}

static gboolean enumerate_video_source_devices(GClosure *callback)
{
    gint num;
    gint i;
    GList *sources = NULL;
    OwrLocalMediaSource *source;

    num = get_number_of_cameras();

    for (i = 0; i < num; ++i) {
        source = _owr_local_media_source_new(i, get_camera_name(i),
            OWR_MEDIA_TYPE_VIDEO, OWR_SOURCE_TYPE_CAPTURE);

        sources = g_list_append(sources, source);
    }

    _owr_utils_call_closure_with_list(callback, sources);

    return FALSE;
}

#endif /*defined(__ANDROID__)*/
