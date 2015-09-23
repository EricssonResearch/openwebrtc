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
\*\ OwrCandidate
/*/

#ifndef __OWR_CANDIDATE_H__
#define __OWR_CANDIDATE_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef enum {
    OWR_CANDIDATE_TYPE_HOST,
    OWR_CANDIDATE_TYPE_SERVER_REFLEXIVE,
    OWR_CANDIDATE_TYPE_PEER_REFLEXIVE,
    OWR_CANDIDATE_TYPE_RELAY
} OwrCandidateType;

typedef enum {
    OWR_COMPONENT_TYPE_RTP = 1,
    OWR_COMPONENT_TYPE_RTCP = 2
} OwrComponentType;

typedef enum {
    OWR_TRANSPORT_TYPE_UDP,
    OWR_TRANSPORT_TYPE_TCP_ACTIVE,
    OWR_TRANSPORT_TYPE_TCP_PASSIVE,
    OWR_TRANSPORT_TYPE_TCP_SO
} OwrTransportType;

#define OWR_TYPE_CANDIDATE_TYPE (owr_candidate_type_get_type())
GType owr_candidate_type_get_type(void);

#define OWR_TYPE_COMPONENT_TYPE (owr_component_type_get_type())
GType owr_component_type_get_type(void);

#define OWR_TYPE_TRANSPORT_TYPE (owr_transport_type_get_type())
GType owr_transport_type_get_type(void);

#define OWR_TYPE_CANDIDATE            (owr_candidate_get_type())
#define OWR_CANDIDATE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), OWR_TYPE_CANDIDATE, OwrCandidate))
#define OWR_CANDIDATE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), OWR_TYPE_CANDIDATE, OwrCandidateClass))
#define OWR_IS_CANDIDATE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), OWR_TYPE_CANDIDATE))
#define OWR_IS_CANDIDATE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), OWR_TYPE_CANDIDATE))
#define OWR_CANDIDATE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), OWR_TYPE_CANDIDATE, OwrCandidateClass))

typedef struct _OwrCandidate        OwrCandidate;
typedef struct _OwrCandidateClass   OwrCandidateClass;
typedef struct _OwrCandidatePrivate OwrCandidatePrivate;

struct _OwrCandidate {
    GObject parent_instance;

    /*< private >*/
    OwrCandidatePrivate *priv;
};

struct _OwrCandidateClass {
    GObjectClass parent_class;

};

GType owr_candidate_get_type(void) G_GNUC_CONST;

OwrCandidate * owr_candidate_new(OwrCandidateType type, OwrComponentType component_type);

G_END_DECLS

#endif /* __OWR_CANDIDATE_H__ */
