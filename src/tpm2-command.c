/*
 * Copyright (c) 2017, Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <inttypes.h>
#include <stdint.h>

#include "tpm2-command.h"
#include "tpm2-header.h"
#include "util.h"
/* macros to make getting at fields in the command more simple */

#define HANDLE_AREA_OFFSET TPM_HEADER_SIZE
#define HANDLE_OFFSET(i) (HANDLE_AREA_OFFSET + sizeof (TPM_HANDLE) * i)
#define HANDLE_END_OFFSET(i) (HANDLE_OFFSET (i) + sizeof (TPM_HANDLE))
#define HANDLE_GET(buffer, count) \
    (*(TPM_HANDLE*)(&buffer [HANDLE_OFFSET (count)]))
/*
 * Offset of capability field in TPM2_GetCapability command buffer is
 * immediately after the header.
 */
#define CAP_OFFSET TPM_HEADER_SIZE
#define CAP_END_OFFSET (CAP_OFFSET + sizeof (TPM_CAP))
#define CAP_GET(buffer) (*(TPM_CAP*)(&buffer [CAP_OFFSET]))
/*
 * Offset of property field in TPM2_GetCapability command buffer is
 * immediately after the capability field.
 */
#define PROPERTY_OFFSET CAP_END_OFFSET
#define PROPERTY_END_OFFSET (PROPERTY_OFFSET + sizeof (UINT32))
#define PROPERTY_GET(buffer) (*(UINT32*)(&buffer [PROPERTY_OFFSET]))
/*
 * The offset of the property count field is immediately following the
 * property field.
 */
#define PROPERTY_COUNT_OFFSET PROPERTY_END_OFFSET
#define PROPERTY_COUNT_END_OFFSET (PROPERTY_COUNT_OFFSET + sizeof (UINT32))
#define PROPERTY_COUNT_GET(buffer) (*(UINT32*)(&buffer [PROPERTY_COUNT_OFFSET]))
/*
 * Helper macros to aid in the access of various parts of the command
 * authorization area.
 */
#define AUTH_AREA_OFFSET(cmd) \
    (TPM_HEADER_SIZE + (tpm2_command_get_handle_count (cmd) * sizeof (TPM_HANDLE)))
#define AUTH_AREA_SIZE_OFFSET(cmd) AUTH_AREA_OFFSET (cmd)
#define AUTH_AREA_SIZE_END_OFFSET(cmd) \
    (AUTH_AREA_SIZE_OFFSET (cmd) + sizeof (UINT32))
#define AUTH_AREA_GET_SIZE(cmd) \
    (be32toh (*(UINT32*)(&cmd->buffer [AUTH_AREA_SIZE_OFFSET (cmd)])))
#define AUTH_AREA_FIRST_OFFSET(cmd) (AUTH_AREA_SIZE_END_OFFSET (cmd))
#define AUTH_AREA_END_OFFSET(cmd) \
    (AUTH_AREA_FIRST_OFFSET (cmd) + AUTH_AREA_GET_SIZE (cmd))
/*
 * Helper macros to aid in accessing parts of a session authorization. These
 * are the individual authorizations in the authorization area of the command.
 * The 'cmd' parameter must be a reference to a valid Tpm2Command object.
 * The 'index' parameter must be the index into the Tpm2Command buffer where
 * the authorization we wish to query resides.
 */
#define AUTH_HANDLE_OFFSET(index) (index + 0)
#define AUTH_HANDLE_END_OFFSET(index) \
    (AUTH_HANDLE_OFFSET(index) + sizeof (TPM_HANDLE))
#define AUTH_GET_HANDLE(cmd, index) \
    (be32toh (*(TPM_HANDLE*)&cmd->buffer [AUTH_HANDLE_OFFSET (index)]))
/* nonce */
#define AUTH_NONCE_SIZE_OFFSET(index) (AUTH_HANDLE_END_OFFSET (index))
#define AUTH_NONCE_SIZE_END_OFFSET(index) \
    (AUTH_NONCE_SIZE_OFFSET(index) + sizeof (UINT16))
#define AUTH_GET_NONCE_SIZE(cmd, index) \
    (be16toh (*(UINT16*)&cmd->buffer [AUTH_NONCE_SIZE_OFFSET (index)]))
#define AUTH_NONCE_BUF_OFFSET(index) \
    (AUTH_NONCE_SIZE_END_OFFSET (index))
#define AUTH_NONCE_BUF_END_OFFSET(cmd, index) \
    (AUTH_NONCE_BUF_OFFSET (index) + AUTH_GET_NONCE_SIZE (cmd, index))
/* session attributes */
#define AUTH_SESSION_ATTRS_OFFSET(cmd, index) \
    (AUTH_NONCE_BUF_END_OFFSET (cmd, index))
/*
 * in AUTH_SESSION_ATTRS_END_OFFSET the UINT8 should be TPMA_SESSION but the
 * TSS headers got this wrong
 */
#define AUTH_SESSION_ATTRS_END_OFFSET(cmd, index) \
    (AUTH_SESSION_ATTRS_OFFSET (cmd, index) + sizeof (UINT8))
#define AUTH_GET_SESSION_ATTRS(cmd, index) \
    ((TPMA_SESSION)(UINT32)cmd->buffer [AUTH_SESSION_ATTRS_OFFSET (cmd, index)])
/* authorization */
#define AUTH_AUTH_SIZE_OFFSET(cmd, index) \
    (AUTH_SESSION_ATTRS_END_OFFSET (cmd, index))
#define AUTH_AUTH_SIZE_END_OFFSET(cmd, index) \
    (AUTH_AUTH_SIZE_OFFSET (cmd, index) + sizeof (UINT16))
#define AUTH_GET_AUTH_SIZE(cmd, index) \
    (be16toh (*(UINT16*)&cmd->buffer [AUTH_AUTH_SIZE_OFFSET (cmd, index)]))
#define AUTH_AUTH_BUF_OFFSET(cmd, index) \
    (AUTH_AUTH_SIZE_END_OFFSET (cmd, index))
#define AUTH_AUTH_BUF_END_OFFSET(cmd, index) \
    (AUTH_AUTH_BUF_OFFSET(cmd, index) + AUTH_GET_AUTH_SIZE (cmd, index))

G_DEFINE_TYPE (Tpm2Command, tpm2_command, G_TYPE_OBJECT);

enum {
    PROP_0,
    PROP_ATTRIBUTES,
    PROP_SESSION,
    PROP_BUFFER,
    PROP_BUFFER_SIZE,
    N_PROPERTIES
};
static GParamSpec *obj_properties [N_PROPERTIES] = { NULL, };
/**
 * GObject property setter.
 */
static void
tpm2_command_set_property (GObject        *object,
                           guint           property_id,
                           GValue const   *value,
                           GParamSpec     *pspec)
{
    Tpm2Command *self = TPM2_COMMAND (object);

    switch (property_id) {
    case PROP_ATTRIBUTES:
        self->attributes = (TPMA_CC)g_value_get_uint (value);
        break;
    case PROP_BUFFER:
        if (self->buffer != NULL) {
            g_warning ("  buffer already set");
            break;
        }
        self->buffer = (guint8*)g_value_get_pointer (value);
        break;
    case PROP_BUFFER_SIZE:
        self->buffer_size = g_value_get_uint (value);
        break;
    case PROP_SESSION:
        if (self->connection != NULL) {
            g_warning ("  connection already set");
            break;
        }
        self->connection = g_value_get_object (value);
        g_object_ref (self->connection);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}
/**
 * GObject property getter.
 */
static void
tpm2_command_get_property (GObject     *object,
                           guint        property_id,
                           GValue      *value,
                           GParamSpec  *pspec)
{
    Tpm2Command *self = TPM2_COMMAND (object);

    g_debug ("tpm2_command_get_property: 0x%" PRIxPTR, (uintptr_t)self);
    switch (property_id) {
    case PROP_ATTRIBUTES:
        g_value_set_uint (value, self->attributes.val);
        break;
    case PROP_BUFFER:
        g_value_set_pointer (value, self->buffer);
        break;
    case PROP_BUFFER_SIZE:
        g_value_set_uint (value, self->buffer_size);
        break;
    case PROP_SESSION:
        g_value_set_object (value, self->connection);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}
/**
 * override the parent finalize method so we can free the data associated with
 * the DataMessage instance.
 */
static void
tpm2_command_finalize (GObject *obj)
{
    Tpm2Command *cmd = TPM2_COMMAND (obj);

    g_debug ("tpm2_command_finalize");
    if (cmd->buffer)
        g_free (cmd->buffer);
    if (cmd->connection)
        g_object_unref (cmd->connection);
    G_OBJECT_CLASS (tpm2_command_parent_class)->finalize (obj);
}
static void
tpm2_command_init (Tpm2Command *command)
{ /* noop */ }
/**
 * Boilerplate GObject initialization. Get a pointer to the parent class,
 * setup a finalize function.
 */
static void
tpm2_command_class_init (Tpm2CommandClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    if (tpm2_command_parent_class == NULL)
        tpm2_command_parent_class = g_type_class_peek_parent (klass);
    object_class->finalize     = tpm2_command_finalize;
    object_class->get_property = tpm2_command_get_property;
    object_class->set_property = tpm2_command_set_property;

    obj_properties [PROP_ATTRIBUTES] =
        g_param_spec_uint ("attributes",
                           "TPMA_CC",
                           "Attributes for command.",
                           0,
                           UINT32_MAX,
                           0,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    obj_properties [PROP_BUFFER] =
        g_param_spec_pointer ("buffer",
                              "TPM2 command buffer",
                              "memory buffer holding a TPM2 command",
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    obj_properties [PROP_BUFFER_SIZE] =
        g_param_spec_uint ("buffer-size",
                           "sizeof command buffer",
                           "size of buffer holding the TPM2 command",
                           0,
                           UTIL_BUF_MAX,
                           0,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    obj_properties [PROP_SESSION] =
        g_param_spec_object ("connection",
                             "Session object",
                             "The Connection object that sent the command",
                             TYPE_CONNECTION,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_properties (object_class,
                                       N_PROPERTIES,
                                       obj_properties);
}
/**
 * Boilerplate constructor, but some GObject properties would be nice.
 */
Tpm2Command*
tpm2_command_new (Connection     *connection,
                  guint8          *buffer,
                  size_t           size,
                  TPMA_CC          attributes)
{
    return TPM2_COMMAND (g_object_new (TYPE_TPM2_COMMAND,
                                       "attributes", attributes,
                                       "buffer",  buffer,
                                       "buffer-size", size,
                                       "connection", connection,
                                       NULL));
}
/* Simple "getter" to expose the attributes associated with the command. */
TPMA_CC
tpm2_command_get_attributes (Tpm2Command *command)
{
    return command->attributes;
}
/**
 */
guint8*
tpm2_command_get_buffer (Tpm2Command *command)
{
    return command->buffer;
}
/**
 */
TPM_CC
tpm2_command_get_code (Tpm2Command *command)
{
    return get_command_code (command->buffer);
}
/**
 */
guint32
tpm2_command_get_size (Tpm2Command *command)
{
    return get_command_size (command->buffer);
}
/**
 */
TPMI_ST_COMMAND_TAG
tpm2_command_get_tag (Tpm2Command *command)
{
    return get_command_tag (command->buffer);
}
/*
 * Return the Connection object associated with this Tpm2Command. This
 * is the Connection object representing the client that sent this command.
 * The reference count on this object is incremented before the Connection
 * object is returned to the caller. The caller is responsible for
 * decrementing the reference count when it is done using the connection object.
 */
Connection*
tpm2_command_get_connection (Tpm2Command *command)
{
    g_object_ref (command->connection);
    return command->connection;
}
/* Return the number of handles in the command. */
guint8
tpm2_command_get_handle_count (Tpm2Command *command)
{
    g_debug ("tpm2_command_get_handle_count");
    uint32_t tmp;

    if (command == NULL) {
        g_warning ("tpm2_command_get_handle_count received NULL parameter");
        return 0;
    }
    tmp = tpm2_command_get_attributes (command).val;
    return (guint8)((tmp & TPMA_CC_CHANDLES) >> 25);
}
/*
 * Simple function to access handles in the provided Tpm2Command. The
 * 'handle_number' parameter is a 0-based index into the handle area
 * which is effectively an array. If the handle_number requests a handle
 * beyond the end of this array 0 is returned.
 */
TPM_HANDLE
tpm2_command_get_handle (Tpm2Command *command,
                         guint8       handle_number)
{
    guint8 real_count;
    size_t end;

    if (command == NULL)
        return 0;
    real_count = tpm2_command_get_handle_count (command);
    end = HANDLE_END_OFFSET (handle_number);
    if (real_count > handle_number && end <= command->buffer_size) {
        return be32toh (HANDLE_GET (command->buffer, handle_number));
    } else {
        return 0;
    }
}
/*
 * Simple function to set a handle at the 0-based index into the Tpm2Command
 * handle area to the provided value. If the handle_number is past the bounds
 * FALSE is returned.
 */
gboolean
tpm2_command_set_handle (Tpm2Command *command,
                         TPM_HANDLE   handle,
                         guint8       handle_number)
{
    guint8 real_count;
    size_t end;

    if (command == NULL)
        return FALSE;
    real_count = tpm2_command_get_handle_count (command);
    end = HANDLE_END_OFFSET (handle_number);
    if (real_count > handle_number && end <= command->buffer_size) {
        HANDLE_GET (command->buffer, handle_number) = htobe32 (handle);
        return TRUE;
    } else {
        return FALSE;
    }
}
/*
 * Return the handles from the Tpm2Command back to the caller by way of the
 * handles parameter. The caller must allocate sufficient space to hold
 * however many handles are in this command. Take a look @
 * tpm2_command_get_handle_count.
 */
gboolean
tpm2_command_get_handles (Tpm2Command *command,
                          TPM_HANDLE   handles[],
                          size_t      *count)
{
    guint8 real_count;
    size_t i;

    if (command == NULL || handles == NULL || count == NULL) {
        g_warning ("tpm2_command_get_handles passed NULL parameter");
        return FALSE;
    }
    real_count = tpm2_command_get_handle_count (command);
    if (real_count > *count) {
        g_warning ("tpm2_command_get_handles passed insufficient handle array");
        return FALSE;
    }

    for (i = 0; i < real_count; ++i) {
        handles[i] = tpm2_command_get_handle (command, i);

        if (handles[i] == 0) {
            // no more handle could be extracted
            break;
        }
    }
    *count = i;

    return TRUE;
}
/*
 * Set handles in the Tpm2Command buffer. The handles are passed in the
 * 'handles' parameter, the size of this array through the 'count'
 * parameter. Each invocation of this function sets all handles in the
 * Tpm2Command and so the size of the handles array (aka 'count') must be
 * the same as the number of handles in the command.
 */
gboolean
tpm2_command_set_handles (Tpm2Command *command,
                          TPM_HANDLE   handles[],
                          guint8       count)
{
    guint8 real_count, i;
    gboolean ret = TRUE;

    if (command == NULL || handles == NULL) {
        g_warning ("tpm2_command_get_handles passed NULL parameter");
        return FALSE;
    }
    real_count = tpm2_command_get_handle_count (command);
    if (real_count != count) {
        g_warning ("tpm2_command_set_handles passed handle array with wrong "
                   "number of handles");
        return FALSE;
    }

    for (i = 0; i < real_count; ++i) {
        ret = tpm2_command_set_handle (command, handles [i], i);
        if (ret == FALSE) {
            break;
        }
    }

    return ret;
}
/*
 * This function is a work around. The handle in a command buffer for the
 * FlushContext command is not in the handle area and no handles are reported
 * in the attributes structure. This means that in order to cope with
 * virtualized handles in this command we must reach into the parameter area
 * which breaks the abstractions we've built for accessing handles. Thus we
 * provide a special function for getting at this single handle.
 * Use this function with caution.
 */
TPM_RC
tpm2_command_get_flush_handle (Tpm2Command *command,
                               TPM_HANDLE  *handle)
{
    if (command == NULL || handle == NULL) {
        g_error ("tpm2_command_get_flush_handle passed null parameter");
    }
    if (tpm2_command_get_code (command) != TPM_CC_FlushContext) {
        g_warning ("tpm2_command_get_flush_handle called with wrong command");
        *handle = 0;
        return RM_RC (TPM_RC_TYPE);
    }
    if (command->buffer_size < TPM_HEADER_SIZE + sizeof (TPM_HANDLE)) {
        *handle = 0;
        return RM_RC (TPM_RC_INSUFFICIENT);
    }
    /*
     * Despite not technically being in the "handle area" of the command we
     * are still able to access the handle as though it were. This is because
     * there are no other handles or authorizations allowed in the command and
     * the handle being flushed is the first parameter.
     */
    *handle = be32toh (HANDLE_GET (tpm2_command_get_buffer (command), 0));
    return TSS2_RC_SUCCESS;
}
/*
 * When provided with a Tpm2Command that represents a call to the
 * GetCapability command this function will extract the 'capability' field.
 * On error 0 is returned.
 */
TPM_CAP
tpm2_command_get_cap (Tpm2Command *command)
{
    if (command == NULL) {
        g_warning ("tpm2_command_get_cap passed NULL parameter");
        return 0;
    }
    if (tpm2_command_get_code (command) != TPM_CC_GetCapability) {
        g_warning ("tpm2_command_get_cap provided a Tpm2Command buffer "
                   "containing the wrong command code.");
        return 0;
    }
    if (command->buffer_size < CAP_END_OFFSET) {
        g_warning ("%s insufficient buffer", __func__);
        return 0;
    }
    return (TPM_CAP)be32toh (CAP_GET (tpm2_command_get_buffer (command)));
}
/*
 * When provided with a Tpm2Command that represents a call to the
 * GetCapability command this function will extract the 'property' field.
 * On error 0 is returned.
 */
UINT32
tpm2_command_get_prop (Tpm2Command *command)
{
    if (command == NULL) {
        g_warning ("tpm2_command_get_prop passed NULL parameter");
        return 0;
    }
    if (tpm2_command_get_code (command) != TPM_CC_GetCapability) {
        g_warning ("tpm2_command_get_cap provided a Tpm2Command buffer "
                   "containing the wrong command code.");
        return 0;
    }
    if (command->buffer_size < PROPERTY_END_OFFSET) {
        g_warning ("%s insufficient buffer", __func__);
        return 0;
    }
    return (UINT32)be32toh (PROPERTY_GET (tpm2_command_get_buffer (command)));
}
/*
 * When provided with a Tpm2Command that represents a call to the
 * GetCapability command this function will extract the 'propertyCount' field.
 * On error 0 is returned.
 */
UINT32
tpm2_command_get_prop_count (Tpm2Command *command)
{
    if (command == NULL) {
        g_warning ("tpm2_command_get_prop_count assed NULL parameter");
        return 0;
    }
    if (tpm2_command_get_code (command) != TPM_CC_GetCapability) {
        g_warning ("tpm2_command_get_cap provided a Tpm2Command buffer "
                   "containing the wrong command code.");
        return 0;
    }
    if (command->buffer_size < PROPERTY_COUNT_END_OFFSET) {
        g_warning ("%s insufficient buffer", __func__);
        return 0;
    }
    return (UINT32)be32toh (PROPERTY_COUNT_GET (tpm2_command_get_buffer (command)));
}
/*
 * This is a convencience function to keep from having to compare the tag
 * value to TPM_ST_(NO_)?_SESSIONS repeatedly.
 */
gboolean
tpm2_command_has_auths (Tpm2Command *command)
{
    if (command == NULL) {
        g_warning ("tpm2_command_has_auths passed NULL parameter");
        return FALSE;
    }
    if (tpm2_command_get_tag (command) == TPM_ST_NO_SESSIONS) {
        return FALSE;
    } else {
        return TRUE;
    }
}
/*
 * When provided with a Tpm2Command with auths in the auth area this function
 * will return the total size of the auth area.
 */
UINT32
tpm2_command_get_auths_size (Tpm2Command *command)
{
    size_t auth_size_end;

    if (command == NULL) {
        g_warning ("tpm2_command_get_auths_size passed NULL parameter");
        return 0;
    }
    if (!tpm2_command_has_auths (command)) {
        g_warning ("tpm2_command_get_auths_size, Tpm2Command 0x%" PRIxPTR
                   " has no auths", (uintptr_t)command);
        return 0;
    }
    auth_size_end = AUTH_AREA_SIZE_END_OFFSET (command);
    g_debug ("%s: auth_size_end: %zu", __func__, auth_size_end);
    g_debug ("%s: buffer_size: %zu", __func__, command->buffer_size);
    if (AUTH_AREA_SIZE_END_OFFSET (command) > command->buffer_size) {
        g_warning ("%s reading size of auth area would overrun command buffer."
                   " Returning 0", __func__);
        return 0;
    }

    return AUTH_AREA_GET_SIZE (command);
}
/*
 * This function extracts the authorization handle from the entry in the
 * auth area that begins at offset 'auth_offset'. Any failure to read this
 * value will return 0.
 */
TPM_HANDLE
tpm2_command_get_auth_handle (Tpm2Command *command,
                              size_t       auth_index)
{
    if (command == NULL) {
        return 0;
    }
    if (AUTH_HANDLE_END_OFFSET (auth_index) > command->buffer_size) {
        g_warning ("%s attempt to access authorization handle overruns "
                   " command buffer", __func__);
        return 0;
    }
    return AUTH_GET_HANDLE (command, auth_index);
}
TPMA_SESSION
tpm2_command_get_auth_attrs (Tpm2Command *command,
                             size_t       auth_offset)
{
    size_t attrs_end;

    if (command == NULL) {
        return (TPMA_SESSION)(UINT32)0;
    }
    attrs_end = AUTH_SESSION_ATTRS_END_OFFSET (command, auth_offset);
    if (attrs_end > command->buffer_size) {
        g_warning ("%s attempt to access session attributes overruns command "
                   "buffer", __func__);
        return (TPMA_SESSION)(UINT32)0;
    }
    return AUTH_GET_SESSION_ATTRS (command, auth_offset);
}
/*
 * The caller provided GFunc is invoked once for each authorization in the
 * command authorization area. The first parameter passed to 'func' is a
 * pointer to the start of the authorization data. The second parameter is
 * the caller provided 'user_data'.
 */
gboolean
tpm2_command_foreach_auth (Tpm2Command *command,
                           GFunc        callback,
                           gpointer     user_data)
{
    size_t   offset;

    if (command == NULL || callback == NULL) {
        g_warning ("%s passed NULL parameter", __func__);
        return FALSE;
    }

    if (AUTH_AREA_FIRST_OFFSET (command) > command->buffer_size) {
        g_warning ("floop");
        return FALSE;
    }

    if (AUTH_AREA_END_OFFSET (command) > command->buffer_size) {
        g_warning ("%s: command buffer size insufficient to iterate all auths",
                   __func__);
        return FALSE;
    }

    for (offset =  AUTH_AREA_FIRST_OFFSET (command);
         offset <  AUTH_AREA_END_OFFSET (command);
         offset = AUTH_AUTH_BUF_END_OFFSET   (command, offset))
    {
        size_t offset_tmp = offset;
        g_debug ("invoking callback at 0x%" PRIxPTR " with authorization at "
                 "offset: %zu and user data: 0x%" PRIxPTR,
                 (uintptr_t)callback,
                 offset_tmp,
                 (uintptr_t)user_data);
        callback (&offset_tmp, user_data);
    }

    return TRUE;
}
