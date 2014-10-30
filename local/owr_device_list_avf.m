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
\*\ OwrDeviceListAVF
/*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "owr_media_source.h"
#include "owr_local_media_source_private.h"
#include "owr_device_list_private.h"

#include <TargetConditionals.h>

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>

typedef OwrLocalMediaSource *(^DeviceMapFunc)(AVCaptureDevice *, gint);

static GList *generate_source_device_list(NSString *mediaType, DeviceMapFunc map_func)
{
    NSArray *devices;
    GList *list = NULL;
    gint index = 0;

    devices = [AVCaptureDevice devicesWithMediaType:mediaType];

    for (AVCaptureDevice *av_device in devices) {
        list = g_list_prepend(list, map_func(av_device, index));
        ++index;
    }

    return g_list_reverse(list);
}

GList *_get_avf_video_sources()
{
    return generate_source_device_list(AVMediaTypeVideo,
            ^(AVCaptureDevice *av_device, gint index) {
        OwrLocalMediaSource *source;
        const gchar *name;

        name = [av_device.localizedName UTF8String];

        source = _owr_local_media_source_new(index, name,
            OWR_MEDIA_TYPE_VIDEO, OWR_SOURCE_TYPE_CAPTURE);

        return source;
    });
}

GList *_get_avf_audio_sources()
{
    return generate_source_device_list(AVMediaTypeAudio,
            ^(AVCaptureDevice *av_device, gint index) {
        OwrLocalMediaSource *source;
        const gchar *name;

        name = [av_device.localizedName UTF8String];

        source = _owr_local_media_source_new(index, name,
            OWR_MEDIA_TYPE_AUDIO, OWR_SOURCE_TYPE_CAPTURE);

        return source;
    });
}
