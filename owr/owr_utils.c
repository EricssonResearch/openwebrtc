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

void _owr_utils_print_bin(GstElement* bin, gboolean printpads)
{
    GstState state;
    GstState pending;
    gchar *binName = gst_element_get_name(bin);
    GstIterator *elementIt;
    gboolean done = FALSE;
    GValue item = G_VALUE_INIT;
    GstElement *element = NULL;
    gchar *name = NULL;

    gst_element_get_state(bin, &state, &pending, 0);

    g_print("==================  %s start - state: %s, pending: %s ==================\n",
        binName, gst_element_state_get_name(state), gst_element_state_get_name(pending));

    elementIt = gst_bin_iterate_elements(GST_BIN(bin));
    while (!done) {
        GstIteratorResult it = gst_iterator_next(elementIt, &item);
        switch (it) {
        case GST_ITERATOR_OK: {
            element = (GstElement*)g_value_get_object(&item);
            g_value_reset(&item);

            name = gst_element_get_name(element);
            gst_element_get_state(element, &state, &pending, 0);
            g_print("Element: %s - state: %s, pending: %s\n", name, gst_element_state_get_name(state), gst_element_state_get_name(pending));
            if (printpads)
                _owr_utils_print_pads(element);
            g_free(name);
            break;
        }
        case GST_ITERATOR_RESYNC:
            gst_iterator_resync(elementIt);
            break;
        case GST_ITERATOR_DONE:
            done = TRUE;
            break;
        case GST_ITERATOR_ERROR:
            g_print("GST_ITERATOR_ERROR\n");
            done = TRUE;
            break;
        default:
            g_print("Default..\n");
        }
    }
    g_print("==================  Pipeline end ==================\n");

    g_free(binName);
}


void _owr_utils_print_pads(GstElement *element)
{
    GstIterator *padsIt = gst_element_iterate_pads(element);
    gboolean done = FALSE;
    GValue item = G_VALUE_INIT;
    GstPad *pad = NULL;
    gchar *padname = NULL;
    GstPad *peer = NULL;
    gchar *peerParentName = NULL;

    while (!done) {
        GstIteratorResult it = gst_iterator_next(padsIt, &item);
        switch (it) {
        case GST_ITERATOR_OK: {
            pad = (GstPad*)g_value_get_object(&item);
            g_value_reset(&item);
            padname = gst_pad_get_name(pad);
            peer = gst_pad_get_peer(pad);
            g_print("Pad: %s\n", padname);
            if (peer) {
                gchar *peerpadname = gst_pad_get_name(peer);
                GstElement *peerParent = gst_pad_get_parent_element(peer);
                if (peerParent) {
                    peerParentName = gst_element_get_name(peerParent);
                    g_print(" - connected to %s of %s\n", peerpadname, peerParentName);
                    g_free(peerParentName);
                    gst_object_unref(peerParent);
                }
                gst_object_unref(peer);
                g_free(peerpadname);
            }
            g_free(padname);
            break;
        }
        case GST_ITERATOR_RESYNC:
            gst_iterator_resync(padsIt);
            break;
        case GST_ITERATOR_DONE:
            g_print("\n");
            done = TRUE;
            break;
        case GST_ITERATOR_ERROR:
            g_print("GST_ITERATOR_ERROR\n");
            done = TRUE;
            break;
        default:
            g_print("Default..\n");
        }
    }
}

void _owr_utils_remove_request_pad(GstPad *pad)
{
    GstPad *peerpad;
    GstElement *peerparent, *parent;
    GstPadTemplate *padtemplate;
    gboolean result;

    parent = gst_pad_get_parent_element(pad);
    peerpad = gst_pad_get_peer(pad);
    g_return_if_fail(GST_IS_PAD(peerpad));
    peerparent = gst_pad_get_parent_element(peerpad);

    gst_pad_set_active(pad, FALSE);
    gst_pad_set_active(peerpad, FALSE);

    result = gst_pad_unlink(pad, peerpad);
    g_warn_if_fail(result);

    /* release the pads if they are request pads */
    padtemplate = gst_pad_get_pad_template(pad);
    if (padtemplate) {
        if (GST_PAD_TEMPLATE_PRESENCE(padtemplate) == GST_PAD_REQUEST)
            gst_element_release_request_pad(parent, pad);
        gst_object_unref(padtemplate);
    }
    padtemplate = gst_pad_get_pad_template(peerpad);
    if (padtemplate) {
        if (GST_PAD_TEMPLATE_PRESENCE(padtemplate) == GST_PAD_REQUEST)
            gst_element_release_request_pad(peerparent, peerpad);
        gst_object_unref(padtemplate);
    }

    gst_object_unref(peerpad);
    gst_object_unref(peerparent);
    gst_object_unref(parent);
}

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
