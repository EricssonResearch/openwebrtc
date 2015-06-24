/*
 * Copyright (c) 2014-2015, Ericsson AB. All rights reserved.
 * Copyright (c) 2014, Centricular Ltd
 *     Author: Sebastian Dröge <sebastian@centricular.com>
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
\*\ Owr types
/*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "owr_types.h"

GType owr_codec_type_get_type(void)
{
    static const GEnumValue types[] = {
        {OWR_CODEC_TYPE_NONE, "No codec (raw)", "raw"},
        {OWR_CODEC_TYPE_PCMU, "u-law PCM", "pcmu"},
        {OWR_CODEC_TYPE_PCMA, "a-law PCM", "pcma"},
        {OWR_CODEC_TYPE_OPUS, "Opus", "opus"},
        {OWR_CODEC_TYPE_H264, "H264", "h264"},
        {OWR_CODEC_TYPE_VP8, "VP8", "vp8"},
        {0, NULL, NULL}
    };
    static volatile GType id = 0;

    if (g_once_init_enter((gsize *)&id)) {
        GType _id = g_enum_register_static("OwrCodecTypes", types);
        g_once_init_leave((gsize *)&id, _id);
    }

    return id;
}

GType owr_media_type_get_type(void)
{
    static const GEnumValue types[] = {
        {OWR_MEDIA_TYPE_UNKNOWN, "Unknown media", "unknown"},
        {OWR_MEDIA_TYPE_AUDIO, "Audio media", "audio"},
        {OWR_MEDIA_TYPE_VIDEO, "Video media", "video"},
        {0, NULL, NULL}
    };
    static volatile GType id = 0;

    if (g_once_init_enter((gsize *)&id)) {
        GType _id = g_enum_register_static("OwrMediaTypes", types);
        g_once_init_leave((gsize *)&id, _id);
    }

    return id;
}

GType owr_source_type_get_type(void)
{
    static const GEnumValue types[] = {
        {OWR_SOURCE_TYPE_UNKNOWN, "Unknown source", "unknown"},
        {OWR_SOURCE_TYPE_CAPTURE, "Capture source", "capture"},
        {OWR_SOURCE_TYPE_TEST, "Test source", "test"},
        {0, NULL, NULL}
    };
    static volatile GType id = 0;

    if (g_once_init_enter((gsize *)&id)) {
        GType _id = g_enum_register_static("OwrSourceTypes", types);
        g_once_init_leave((gsize *)&id, _id);
    }

    return id;
}

GType owr_adaptation_type_get_type(void)
{
    static const GEnumValue types[] = {
        {OWR_ADAPTATION_TYPE_DISABLED, "No adaptation (disabled)", "disabled"},
        {OWR_ADAPTATION_TYPE_SCREAM, "Scream", "scream"},
        {0, NULL, NULL}
    };
static volatile GType id = 0;

if (g_once_init_enter((gsize *)&id)) {
    GType _id = g_enum_register_static("OwrAdaptationTypes", types);
    g_once_init_leave((gsize *)&id, _id);
}

return id;
}
