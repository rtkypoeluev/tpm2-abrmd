#ifndef TSS2TCTI_TABD_PRIV_H
#define TSS2TCTI_TABD_PRIV_H

#include <glib.h>
#include <pthread.h>
#include "tabd-generated.h"

#define TSS2_TCTI_TABD_MAGIC 0x1c8e03ff00db0f92

#define TSS2_TCTI_TABD_ID(context) \
    ((TSS2_TCTI_TABD_CONTEXT*)context)->id
#define TSS2_TCTI_TABD_WATCHER_ID(context) \
    ((TSS2_TCTI_TABD_CONTEXT*)context)->watcher_id
#define TSS2_TCTI_TABD_MUTEX(context) \
    ((TSS2_TCTI_TABD_CONTEXT*)context)->mutex
#define TSS2_TCTI_TABD_PIPE_FDS(context) \
    ((TSS2_TCTI_TABD_CONTEXT*)context)->pipe_fds
#define TSS2_TCTI_TABD_PIPE_RECEIVE(context) \
    TSS2_TCTI_TABD_PIPE_FDS(context)[0]
#define TSS2_TCTI_TABD_PIPE_TRANSMIT(context) \
    TSS2_TCTI_TABD_PIPE_FDS(context)[1]
#define TSS2_TCTI_TABD_PROXY(context) \
    ((TSS2_TCTI_TABD_CONTEXT*)context)->proxy

/* This is our private TCTI structure. We're required by the spec to have
 * the same structure as the non-opaque area defined by the
 * TSS2_TCTI_CONTEXT_COMMON_V1 structure. Anything after this data is opaque
 * and private to our implementation. See section 7.3 of the SAPI / TCTI spec
 * for the details.
 */
typedef struct {
    TSS2_TCTI_CONTEXT_COMMON_V1    common;
    guint64                        id;
    guint                          watcher_id;
    pthread_mutex_t                mutex;
    int                            pipe_fds[2];
    Tpm2AccessBroker              *proxy;
} TSS2_TCTI_TABD_CONTEXT;

#endif /* TSS2TCTI_TABD_PRIV_H */
