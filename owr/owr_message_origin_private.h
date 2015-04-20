/*
 * Copyright (c) 2015, Ericsson AB. All rights reserved.
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

#ifndef __OWR_MESSAGE_ORIGIN_PRIVATE_H__
#define __OWR_MESSAGE_ORIGIN_PRIVATE_H__

#include "owr_message_origin.h"

#include "owr_bus.h"

#ifndef __GTK_DOC_IGNORE__

G_BEGIN_DECLS

typedef struct {
    GHashTable *table;
    GMutex mutex;
} OwrMessageOriginBusSet;

OwrMessageOriginBusSet *owr_message_origin_bus_set_new();
void owr_message_origin_bus_set_free(OwrMessageOriginBusSet *bus_set);
OwrMessageOriginBusSet *owr_message_origin_get_bus_set(OwrMessageOrigin *origin);
void owr_message_origin_post_message(OwrMessageOrigin *origin, OwrMessageType type, OwrMessageSubType sub_type, GHashTable *data);

#define OWR_POST_MESSAGE(origin, type, sub_type, data) owr_message_origin_post_message\
    (OWR_MESSAGE_ORIGIN(origin), G_PASTE(OWR_MESSAGE_TYPE_, type)\
        , G_PASTE(G_PASTE(OWR_, type), G_PASTE(_TYPE_, sub_type)), data)
#define OWR_POST_ERROR(origin, sub_type, data) OWR_POST_MESSAGE(origin, ERROR, sub_type, data)
#define OWR_POST_STATS(origin, sub_type, data) OWR_POST_MESSAGE(origin, STATS, sub_type, data)
#define OWR_POST_EVENT(origin, sub_type, data) OWR_POST_MESSAGE(origin, EVENT, sub_type, data)

G_END_DECLS

#endif /* __GTK_DOC_IGNORE__ */

#endif /* __OWR_MESSAGE_ORIGIN_PRIVATE_H__ */
