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

#ifndef __OWR_BUS_PRIVATE_H__
#define __OWR_BUS_PRIVATE_H__

#include "owr_bus.h"

#ifndef __GTK_DOC_IGNORE__

G_BEGIN_DECLS

typedef struct {
    OwrMessageOrigin *origin;
    OwrMessageType type;
    OwrMessageSubType sub_type;
    GHashTable *data;
    volatile guint ref_count;
} OwrMessage;

void _owr_bus_post_message(OwrBus *bus, OwrMessage *message);
OwrMessage *_owr_message_new(OwrMessageOrigin *origin, OwrMessageType type, OwrMessageSubType sub_type, GHashTable *data);
void _owr_message_ref(OwrMessage *message);
void _owr_message_unref(OwrMessage *message);

G_END_DECLS

#endif /* __GTK_DOC_IGNORE__ */

#endif /* __OWR_BUS_PRIVATE_H__ */
