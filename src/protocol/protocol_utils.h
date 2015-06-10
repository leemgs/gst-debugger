/*
 * protocol_utils.h
 *
 *  Created on: Jun 9, 2015
 *      Author: mkolny
 */

#ifndef PROTOCOL_UTILS_H_
#define PROTOCOL_UTILS_H_

#include <gio/gio.h>

gboolean gst_debugger_protocol_utils_read_requested_size (GInputStream * istream, gint requested_size, guint8 * buffer);

gint gst_debugger_protocol_utils_read_header (GInputStream * istream);

void gst_debugger_protocol_utils_serialize_integer (gint value, guint8 * buffer, gint size);

gint gst_debugger_protocol_utils_deserialize_integer (guint8 * buffer, gint size);

#endif /* PROTOCOL_UTILS_H_ */