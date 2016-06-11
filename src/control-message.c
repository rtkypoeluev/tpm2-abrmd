#include "control-message.h"

/* Boiler-plate gobject code.
 */
static gpointer control_message_parent_class = NULL;
static void
control_message_class_init (gpointer klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    if (control_message_parent_class == NULL)
        control_message_parent_class = g_type_class_peek_parent (klass);
}
/* Upon first call to *_get_type we register the type with the GType system.
 * We keep a static GType around to speed up future calls.
 */
GType
control_message_get_type (void)
{
    static GType type = 0;
    if (type == 0) {
        type = g_type_register_static_simple (G_TYPE_OBJECT,
                                              "ControlMessage",
                                              sizeof (ControlMessageClass),
                                              (GClassInitFunc) control_message_class_init,
                                              sizeof (ControlMessage),
                                              NULL,
                                              0);
    }
    return type;
}

ControlMessage*
control_message_new (ControlCode code)
{
    ControlMessage *msg;

    msg = CONTROL_MESSAGE (g_object_new (TYPE_CONTROL_MESSAGE, NULL));
    msg->code = code;
    return msg;
}
