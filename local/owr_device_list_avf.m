/*
 * Copyright (c) 2014-2015, Ericsson AB. All rights reserved.
 * Copyright (c) 2014, Centricular Ltd
 *     Author: Arun Raghavan <arun@centricular.com>
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
\*\ OwrDeviceListAVF
/*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "owr_device_list_private.h"
#include "owr_local_media_source_private.h"
#include "owr_media_source.h"

#include <TargetConditionals.h>

#import <AVFoundation/AVFoundation.h>
#import <CoreFoundation/CFString.h>
#import <Foundation/Foundation.h>

typedef OwrLocalMediaSource *(^DeviceMapFunc)(AVCaptureDevice *, gint);

static GList *generate_source_device_list(NSString *mediaType, DeviceMapFunc map_func)
{
    NSAutoreleasePool *pool = nil;
    NSArray *devices;
    GList *list = NULL;
    gint index = 0;

    pool = [[NSAutoreleasePool alloc] init];

    devices = [AVCaptureDevice devicesWithMediaType:mediaType];

    for (AVCaptureDevice *av_device in devices) {
        list = g_list_prepend(list, map_func(av_device, index));
        ++index;
    }

    list = g_list_reverse(list);

    [pool release];

    return list;
}

GList *_owr_get_avf_video_sources()
{
    GList *list = generate_source_device_list(AVMediaTypeVideo,
        ^(AVCaptureDevice *av_device, gint index)
            {
        OwrLocalMediaSource *source;
        const gchar *name;

        name = [av_device.localizedName UTF8String];

        source = _owr_local_media_source_new_cached(index, name,
            OWR_MEDIA_TYPE_VIDEO, OWR_SOURCE_TYPE_CAPTURE);

        return source;
    });

#if TARGET_OS_IPHONE
    return g_list_reverse(list);
#else
    return list;
#endif
}

#if !TARGET_OS_IPHONE
static OwrLocalMediaSource * make_source_from_device(AudioObjectID device)
{
    OwrLocalMediaSource *source = NULL;
    gchar name[1024];
    CFStringRef cf_name;
    UInt32 psize;
    OSStatus status;

    AudioObjectPropertyAddress streams_prop_addr = {
        kAudioDevicePropertyStreams,
        kAudioObjectPropertyScopeInput,
        kAudioObjectPropertyElementMaster,
    };

    AudioObjectPropertyAddress name_prop_addr = {
        kAudioObjectPropertyName,
        kAudioObjectPropertyScopeInput,
        kAudioObjectPropertyElementMaster,
    };

    status = AudioObjectGetPropertyDataSize(device, &streams_prop_addr, 0, NULL, &psize);
    if (status != noErr) {
        g_warning("Could not get 'device streams' property");
        goto out;
    }

    if (!psize) {
        /* No input streams, skip this device */
        goto out;
    }

    status = AudioObjectGetPropertyDataSize(device, &name_prop_addr, 0, NULL, &psize);
    if (status != noErr) {
        g_warning("Could not get 'device name' property size");
        goto out;
    }

    status = AudioObjectGetPropertyData(device, &name_prop_addr, 0, NULL, &psize, &cf_name);
    if (status != noErr) {
        g_warning("Could not get 'device name' property");
        goto out;
    }

    if (!CFStringGetCString(cf_name, name, sizeof(name), CFStringGetSystemEncoding())) {
        g_warning("Could not get device name as a string");
        CFRelease(cf_name);
        goto out;
    }
    CFRelease(cf_name);

    source = _owr_local_media_source_new_cached(device, name,
        OWR_MEDIA_TYPE_AUDIO, OWR_SOURCE_TYPE_CAPTURE);

out:
    return source;
}

GList *_owr_get_core_audio_sources()
{
    OwrLocalMediaSource *source;
    GList *ret = NULL;
    gint i, n;
    AudioObjectID *devices;
    UInt32 psize;
    OSStatus status;

    AudioObjectPropertyAddress devices_prop_addr = {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMaster,
    };

    status = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject,
        &devices_prop_addr, 0, NULL, &psize);
    if (status != noErr) {
        g_warning("Could not get 'audio devices' property size");
        goto error;
    }

    n = psize / sizeof(AudioObjectID);
    devices = g_new(AudioObjectID, n);

    status = AudioObjectGetPropertyData(kAudioObjectSystemObject,
        &devices_prop_addr, 0, NULL, &psize, devices);
    if (status != noErr) {
        g_warning("Could not get 'audio devices' property");
        goto error;
    }

    for (i = 0; i < n; i++) {
        source = make_source_from_device(devices[i]);
        if (source)
            ret = g_list_prepend(ret, source);
    }

    g_free(devices);

    return g_list_reverse(ret);

error:
    return NULL;
}

#else /* TARGET_OS_IPHONE */

GList *_owr_get_core_audio_sources()
{
    OwrLocalMediaSource *source;

    source = _owr_local_media_source_new_cached(-1, "Default audio input",
        OWR_MEDIA_TYPE_AUDIO, OWR_SOURCE_TYPE_CAPTURE);

    return g_list_prepend(NULL, source);
}

#endif /* TARGET_OS_IPHONE */
