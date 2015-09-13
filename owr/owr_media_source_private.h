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
\*\ OwrMediaSource private
/*/

#ifndef __OWR_MEDIA_SOURCE_PRIVATE_H__
#define __OWR_MEDIA_SOURCE_PRIVATE_H__

#include "owr_media_source.h"

#include "owr_types.h"

#include <glib-object.h>
#include <gst/gst.h>

#ifndef __GTK_DOC_IGNORE__

G_BEGIN_DECLS

GstElement *_owr_media_source_get_source_bin(OwrMediaSource *media_source);
void _owr_media_source_set_source_bin(OwrMediaSource *media_source, GstElement *bin);

GstElement *_owr_media_source_get_source_tee(OwrMediaSource *media_source);
void _owr_media_source_set_source_tee(OwrMediaSource *media_source, GstElement *tee);

GstElement *_owr_media_source_request_source(OwrMediaSource *media_source, GstCaps *caps);
void _owr_media_source_release_source(OwrMediaSource *media_source, GstElement *source);

void _owr_media_source_set_type(OwrMediaSource *source, OwrSourceType type);

void _owr_media_source_set_codec(OwrMediaSource *source, OwrCodecType codec_type);
OwrCodecType _owr_media_source_get_codec(OwrMediaSource *source);

G_END_DECLS

#endif /* __GTK_DOC_IGNORE__ */

#endif /* __OWR_MEDIA_SOURCE_PRIVATE_H__ */
