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

#ifndef __OWR_TYPES_H__
#define __OWR_TYPES_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef enum _OwrCodecType {
    OWR_CODEC_TYPE_NONE,
    OWR_CODEC_TYPE_PCMU,
    OWR_CODEC_TYPE_PCMA,
    OWR_CODEC_TYPE_OPUS,
    OWR_CODEC_TYPE_H264,
    OWR_CODEC_TYPE_VP8
} OwrCodecType;

typedef enum _OwrMediaType {
    OWR_MEDIA_TYPE_UNKNOWN = 0,
    OWR_MEDIA_TYPE_AUDIO = (1 << 0),
    OWR_MEDIA_TYPE_VIDEO = (1 << 1)
} OwrMediaType;

typedef enum _OwrSourceType {
    OWR_SOURCE_TYPE_UNKNOWN,
    OWR_SOURCE_TYPE_CAPTURE,
    OWR_SOURCE_TYPE_TEST
} OwrSourceType;

typedef enum _OwrAdaptationType {
    OWR_ADAPTATION_TYPE_DISABLED,
    OWR_ADAPTATION_TYPE_SCREAM
} OwrAdaptationType;

#define OWR_TYPE_CODEC_TYPE (owr_codec_type_get_type())
GType owr_codec_type_get_type(void);

#define OWR_TYPE_SOURCE_TYPE (owr_source_type_get_type())
GType owr_source_type_get_type(void);

#define OWR_TYPE_MEDIA_TYPE (owr_media_type_get_type())
GType owr_media_type_get_type(void);

#define OWR_TYPE_ADAPTATION_TYPE (owr_adaptation_type_get_type())
GType owr_adaptation_type_get_type(void);


G_END_DECLS

#endif /* __OWR_TYPES_H__ */
