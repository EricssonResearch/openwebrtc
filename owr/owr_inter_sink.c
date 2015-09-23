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
#include "owr_inter_sink.h"

#define GST_CAT_DEFAULT owr_inter_sink_debug
GST_DEBUG_CATEGORY_STATIC(GST_CAT_DEFAULT);

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY
    );

/* Part of a workaround to avoid exposing the get_type symbol */
#define owr_inter_sink_get_type _owr_inter_sink_get_type

#define parent_class owr_inter_sink_parent_class
G_DEFINE_TYPE(OwrInterSink, owr_inter_sink, GST_TYPE_ELEMENT);

static gboolean owr_inter_sink_sink_query(GstPad *pad, GstObject *parent,
    GstQuery *query);
static GstFlowReturn owr_inter_sink_sink_chain(GstPad *pad,
    GstObject *parent, GstBuffer *buffer);
static GstFlowReturn owr_inter_sink_sink_chain_list(GstPad *pad,
    GstObject *parent, GstBufferList *list);
static gboolean owr_inter_sink_sink_event(GstPad *pad, GstObject *parent,
    GstEvent *event);

static GstStateChangeReturn owr_inter_sink_change_state(GstElement *element, GstStateChange transition);

static void owr_inter_sink_class_init(OwrInterSinkClass *klass)
{
    GstElementClass *gstelement_class;

    GST_DEBUG_CATEGORY_INIT(owr_inter_sink_debug, "owr_inter_sink", 0,
        "Owr Inter Sink");

    gstelement_class = (GstElementClass *) klass;

    gstelement_class->change_state = owr_inter_sink_change_state;

    gst_element_class_add_pad_template(gstelement_class,
        gst_static_pad_template_get(&sink_template));
}

static void owr_inter_sink_init(OwrInterSink *self)
{
    self->sinkpad = gst_pad_new_from_static_template(&sink_template, "sink");
    gst_pad_set_chain_function(self->sinkpad,
        GST_DEBUG_FUNCPTR(owr_inter_sink_sink_chain));
    gst_pad_set_chain_list_function(self->sinkpad,
        GST_DEBUG_FUNCPTR(owr_inter_sink_sink_chain_list));
    gst_pad_set_event_function(self->sinkpad,
        GST_DEBUG_FUNCPTR(owr_inter_sink_sink_event));
    gst_pad_set_query_function(self->sinkpad,
        GST_DEBUG_FUNCPTR(owr_inter_sink_sink_query));
    gst_element_add_pad(GST_ELEMENT(self), self->sinkpad);
}

static GstStateChangeReturn owr_inter_sink_change_state(GstElement *element, GstStateChange transition)
{
    OwrInterSink *self = OWR_INTER_SINK(element);
    GstStateChangeReturn ret;

    switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
        self->pending_sticky_events = FALSE;
        break;
    default:
        break;
    }

    ret = GST_ELEMENT_CLASS(owr_inter_sink_parent_class)->change_state(element, transition);

    return ret;
}

static gboolean owr_inter_sink_sink_query(GstPad *pad, GstObject *parent,
    GstQuery *query)
{
    OwrInterSink *self = OWR_INTER_SINK(parent);
    GstPad *otherpad;
    gboolean ret = FALSE;

    GST_LOG_OBJECT(pad, "Handling query of type '%s'",
        gst_query_type_get_name(GST_QUERY_TYPE(query)));

    otherpad = g_weak_ref_get(&self->src_srcpad);
    if (otherpad) {
        ret = gst_pad_peer_query(otherpad, query);
        gst_object_unref(otherpad);
    }

    return ret;
}

typedef struct {
    GstPad *otherpad;
    GstFlowReturn ret;
} CopyStickyEventsData;

static gboolean copy_sticky_events(G_GNUC_UNUSED GstPad *pad, GstEvent **event, gpointer user_data)
{
    CopyStickyEventsData *data = user_data;

    data->ret = gst_pad_store_sticky_event(data->otherpad, *event);

    return data->ret == GST_FLOW_OK;
}

static gboolean owr_inter_sink_sink_event(GstPad *pad, GstObject *parent,
    GstEvent *event)
{
    OwrInterSink *self = OWR_INTER_SINK(parent);
    GstPad *otherpad;
    gboolean ret = FALSE;
    gboolean sticky = GST_EVENT_IS_STICKY(event);

    GST_LOG_OBJECT(pad, "Got %s event", GST_EVENT_TYPE_NAME(event));

    if (GST_EVENT_TYPE(event) == GST_EVENT_FLUSH_STOP)
        self->pending_sticky_events = FALSE;

    otherpad = g_weak_ref_get(&self->src_srcpad);
    if (otherpad) {
        if (sticky && self->pending_sticky_events) {
            CopyStickyEventsData data = { otherpad, GST_FLOW_OK };

            gst_pad_sticky_events_foreach(pad, copy_sticky_events, &data);
            self->pending_sticky_events = data.ret != GST_FLOW_OK;
        }

        ret = gst_pad_push_event(otherpad, event);
        gst_object_unref(otherpad);

        if (!ret && sticky) {
            self->pending_sticky_events = TRUE;
            ret = TRUE;
        }
    } else
        gst_event_unref(event);

    return ret;
}

static GstFlowReturn owr_inter_sink_sink_chain(GstPad *pad, GstObject *parent,
    GstBuffer *buffer)
{
    OwrInterSink *self = OWR_INTER_SINK(parent);
    GstPad *otherpad;
    GstFlowReturn ret = GST_FLOW_OK;

    GST_LOG_OBJECT(pad, "Chaining buffer %p", buffer);

    otherpad = g_weak_ref_get(&self->src_srcpad);
    if (otherpad) {
        if (self->pending_sticky_events) {
            CopyStickyEventsData data = { otherpad, GST_FLOW_OK };

            gst_pad_sticky_events_foreach(pad, copy_sticky_events, &data);
            self->pending_sticky_events = data.ret != GST_FLOW_OK;
        }

        ret = gst_pad_push(otherpad, buffer);
        gst_object_unref(otherpad);
    } else
        gst_buffer_unref(buffer);

    GST_LOG_OBJECT(pad, "Chained buffer %p: %s", buffer, gst_flow_get_name(ret));

    return GST_FLOW_OK;
}

static GstFlowReturn owr_inter_sink_sink_chain_list(GstPad *pad, GstObject *parent,
    GstBufferList *list)
{
    OwrInterSink *self = OWR_INTER_SINK(parent);
    GstPad *otherpad;
    GstFlowReturn ret = GST_FLOW_OK;

    GST_LOG_OBJECT(pad, "Chaining buffer list %p", list);

    otherpad = g_weak_ref_get(&self->src_srcpad);
    if (otherpad) {
        if (self->pending_sticky_events) {
            CopyStickyEventsData data = { otherpad, GST_FLOW_OK };

            gst_pad_sticky_events_foreach(pad, copy_sticky_events, &data);
            self->pending_sticky_events = data.ret != GST_FLOW_OK;
        }

        ret = gst_pad_push_list(otherpad, list);
        gst_object_unref(otherpad);
    } else
        gst_buffer_list_unref(list);

    GST_LOG_OBJECT(pad, "Chained buffer list %p: %s", list, gst_flow_get_name(ret));

    return GST_FLOW_OK;
}
