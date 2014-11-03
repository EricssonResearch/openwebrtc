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
\*\ OwrUtils
/*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "owr_utils.h"

#include "owr_types.h"

OwrCodecType _owr_caps_to_codec_type(GstCaps *caps)
{
    GstStructure *structure;

    structure = gst_caps_get_structure(caps, 0);
    if (gst_structure_has_name(structure, "video/x-raw")
        || gst_structure_has_name(structure, "audio/x-raw"))
        return OWR_CODEC_TYPE_NONE;
    if (gst_structure_has_name(structure, "audio/x-mulaw"))
        return OWR_CODEC_TYPE_PCMU;
    if (gst_structure_has_name(structure, "audio/x-alaw"))
        return OWR_CODEC_TYPE_PCMA;
    if (gst_structure_has_name(structure, "audio/x-opus"))
        return OWR_CODEC_TYPE_OPUS;
    if (gst_structure_has_name(structure, "video/x-h264"))
        return OWR_CODEC_TYPE_H264;
    if (gst_structure_has_name(structure, "video/x-vp8"))
        return OWR_CODEC_TYPE_VP8;

    GST_ERROR("Unknown caps: %" GST_PTR_FORMAT, (gpointer)caps);
    return OWR_CODEC_TYPE_NONE;
}
