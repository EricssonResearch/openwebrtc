/*
 * Copyright (C) 2015 Centricular Ltd.
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "owr_inter_src.h"

#define GST_CAT_DEFAULT owr_inter_src_debug
GST_DEBUG_CATEGORY_STATIC(GST_CAT_DEFAULT);
static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY
    );

/* Part of a workaround to avoid exposing the get_type symbol */
#define owr_inter_src_get_type _owr_inter_src_get_type

#define parent_class owr_inter_src_parent_class
G_DEFINE_TYPE(OwrInterSrc, owr_inter_src, GST_TYPE_BIN);

static gboolean owr_inter_src_internal_src_query(GstPad *pad, GstObject *parent,
    GstQuery *query);
static gboolean owr_inter_src_internal_src_event(GstPad *pad, GstObject *parent,
    GstEvent *event);

static GstStateChangeReturn owr_inter_src_change_state(GstElement *element, GstStateChange transition);
static void owr_inter_src_dispose(GObject *object);

static void owr_inter_src_class_init(OwrInterSrcClass *klass)
{
    GObjectClass *gobject_class;
    GstElementClass *gstelement_class;

    GST_DEBUG_CATEGORY_INIT(owr_inter_src_debug, "owr_inter_src", 0,
        "Owr Inter Src");

    gobject_class = (GObjectClass *) klass;
    gstelement_class = (GstElementClass *) klass;

    gobject_class->dispose = owr_inter_src_dispose;

    gstelement_class->change_state = owr_inter_src_change_state;
    gst_element_class_add_pad_template(gstelement_class,
        gst_static_pad_template_get(&src_template));
}

static void owr_inter_src_init(OwrInterSrc *self)
{
    GstPad *srcpad, *sinkpad;

    GST_OBJECT_FLAG_SET(self, GST_ELEMENT_FLAG_SOURCE);

    self->queue = gst_element_factory_make("queue", NULL);
    gst_bin_add(GST_BIN(self), self->queue);

    srcpad = gst_element_get_static_pad(self->queue, "src");
    self->srcpad = gst_ghost_pad_new_from_template("src", srcpad, gst_static_pad_template_get(&src_template));
    gst_object_unref(srcpad);

    gst_element_add_pad(GST_ELEMENT(self), self->srcpad);

    /* Just to allow linking... */
    self->dummy_sinkpad = gst_pad_new("dummy_sinkpad", GST_PAD_SINK);
    gst_object_set_parent(GST_OBJECT(self->dummy_sinkpad), GST_OBJECT(self));

    self->internal_srcpad = gst_pad_new("internal_src", GST_PAD_SRC);
    gst_object_set_parent(GST_OBJECT(self->internal_srcpad), GST_OBJECT(self->dummy_sinkpad));
    gst_pad_set_event_function(self->internal_srcpad, owr_inter_src_internal_src_event);
    gst_pad_set_query_function(self->internal_srcpad, owr_inter_src_internal_src_query);

    sinkpad = gst_element_get_static_pad(self->queue, "sink");
    gst_pad_link(self->internal_srcpad, sinkpad);
    gst_object_unref(sinkpad);
}

static void owr_inter_src_dispose(GObject *object)
{
    OwrInterSrc *self = OWR_INTER_SRC(object);

    gst_object_unparent(GST_OBJECT(self->dummy_sinkpad));
    self->dummy_sinkpad = NULL;

    gst_object_unparent(GST_OBJECT(self->internal_srcpad));
    self->internal_srcpad = NULL;

    G_OBJECT_CLASS(owr_inter_src_parent_class)->dispose(object);
}

static GstStateChangeReturn owr_inter_src_change_state(GstElement *element, GstStateChange transition)
{
    OwrInterSrc *self = OWR_INTER_SRC(element);
    GstStateChangeReturn ret;

    ret = GST_ELEMENT_CLASS(owr_inter_src_parent_class)->change_state(element, transition);
    if (ret == GST_STATE_CHANGE_FAILURE)
        return ret;

    switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
        ret = GST_STATE_CHANGE_NO_PREROLL;
        gst_pad_set_active(self->internal_srcpad, TRUE);
        break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
        gst_pad_set_active(self->internal_srcpad, FALSE);
        break;
    default:
        break;
    }

    return ret;
}

static gboolean owr_inter_src_internal_src_query(GstPad *pad, GstObject *parent,
    GstQuery *query)
{
    OwrInterSrc *self = OWR_INTER_SRC(gst_object_get_parent(parent));
    GstPad *otherpad;
    gboolean ret = FALSE;

    if (!self)
        return ret;

    GST_LOG_OBJECT(pad, "Handling query of type '%s'",
        gst_query_type_get_name(GST_QUERY_TYPE(query)));

    otherpad = g_weak_ref_get(&self->sink_sinkpad);
    if (otherpad) {
        ret = gst_pad_peer_query(otherpad, query);
        gst_object_unref(otherpad);
    }

    gst_object_unref(self);

    return ret;
}

static gboolean owr_inter_src_internal_src_event(GstPad *pad, GstObject *parent,
    GstEvent *event)
{
    OwrInterSrc *self = OWR_INTER_SRC(gst_object_get_parent(parent));
    GstPad *otherpad;
    gboolean ret = FALSE;

    if (!self)
        return ret;

    GST_LOG_OBJECT(pad, "Got %s event", GST_EVENT_TYPE_NAME(event));

    otherpad = g_weak_ref_get(&self->sink_sinkpad);
    if (otherpad) {
        ret = gst_pad_push_event(otherpad, event);
        gst_object_unref(otherpad);
    } else
        gst_event_unref(event);


    gst_object_unref(self);

    return ret;
}
