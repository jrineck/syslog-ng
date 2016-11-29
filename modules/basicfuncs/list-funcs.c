/*
 * Copyright (c) 2016 Balabit
 * Copyright (c) 2016 Balázs Scheidler
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

#include "str-repr/list-scanner.h"
#include "str-repr/encode.h"

static void
_append_comma(GString *result)
{
  if (result->len == 0)
    return;

  if (result->str[result->len - 1] != ',')
    g_string_append_c(result, ',');
}

static void
tf_list_concat(LogMessage *msg, gint argc, GString *argv[], GString *result)
{
  ListScanner scanner;

  list_scanner_init(&scanner);
  list_scanner_input_gstring_array(&scanner, argc, argv);
  while (list_scanner_scan_next(&scanner))
    {
      _append_comma(result);
      str_repr_encode_append(result, list_scanner_get_current_value(&scanner), -1, ",");
    }
  list_scanner_deinit(&scanner);
}

TEMPLATE_FUNCTION_SIMPLE(tf_list_concat);

static void
tf_list_append(LogMessage *msg, gint argc, GString *argv[], GString *result)
{
  if (argc == 0)
    return;
  g_string_append_len(result, argv[0]->str, argv[0]->len);
  for (gint i = 1; i < argc; i++)
    {
      _append_comma(result);
      str_repr_encode_append(result, argv[i]->str, argv[i]->len, ",");
    }
}

TEMPLATE_FUNCTION_SIMPLE(tf_list_append);

static gint
_list_count(gint argc, GString *argv[])
{
  ListScanner scanner;
  gint count = 0;

  if (argc != 0)
    {
      list_scanner_init(&scanner);
      list_scanner_input_gstring_array(&scanner, argc, argv);

      while (list_scanner_scan_next(&scanner))
        count++;

      list_scanner_deinit(&scanner);
    }
  return count;
}

static void
_adjust_list_slice(gint argc, GString *argv[],
                   gint *first_ndx, gint *last_ndx)
{
  gint count = -1;

  if (*first_ndx < 0 || *last_ndx < 0)
    count = _list_count(argc, argv);

  if (*first_ndx < 0)
    *first_ndx += count;
  if (*last_ndx < 0)
    *last_ndx += count;

}

static void
_list_slice(gint argc, GString *argv[], GString *result,
            gint first_ndx, gint last_ndx)
{
  ListScanner scanner;
  gint i;

  if (argc == 0)
    return;

  _adjust_list_slice(argc, argv, &first_ndx, &last_ndx);

  list_scanner_init(&scanner);
  list_scanner_input_gstring_array(&scanner, argc, argv);

  i = 0;
  while (i < first_ndx && list_scanner_scan_next(&scanner))
    i++;

  while (i >= first_ndx &&
         i < last_ndx &&
         list_scanner_scan_next(&scanner))
    {
      _append_comma(result);
      str_repr_encode_append(result, list_scanner_get_current_value(&scanner), -1, ",");
      i++;
    }
  list_scanner_deinit(&scanner);
}

static void
tf_list_head(LogMessage *msg, gint argc, GString *argv[], GString *result)
{
  if (argc == 0)
    return;

  _list_slice(argc, argv, result, 0, 1);
}

TEMPLATE_FUNCTION_SIMPLE(tf_list_head);

static void
tf_list_tail(LogMessage *msg, gint argc, GString *argv[], GString *result)
{
  if (argc == 0)
    return;

  _list_slice(argc, argv, result, 1, INT_MAX);
}

TEMPLATE_FUNCTION_SIMPLE(tf_list_tail);

static void
tf_list_count(LogMessage *msg, gint argc, GString *argv[], GString *result)
{
  gint count = _list_count(argc, argv);
  format_uint32_padded(result, -1, ' ', 10, count);
}

TEMPLATE_FUNCTION_SIMPLE(tf_list_count);

/* $(list-slice FIRST:LAST list ...) */
static void
tf_list_slice(LogMessage *msg, gint argc, GString *argv[], GString *result)
{
  gint64 first_ndx = 0, last_ndx = INT_MAX;
  const gchar *slice_spec, *first_spec, *last_spec;
  gchar *colon;

  if (argc < 1)
    return;

  slice_spec = argv[0]->str;
  first_spec = slice_spec;
  colon = strchr(first_spec, ':');
  if (colon)
    {
      last_spec = colon + 1;
      *colon = 0;
    }
  else
    last_spec = NULL;

  /* get start position from first argument */
  if (first_spec && first_spec[0] &&
      !parse_number(first_spec, &first_ndx))
    {
      msg_error("$(list-slice) parsing failed, first could not be parsed",
                evt_tag_str("start", first_spec));
      return;
    }

  /* get last position from second argument */
  if (last_spec && last_spec[0] &&
      !parse_number(last_spec, &last_ndx))
    {
      msg_error("$(list-slice) parsing failed, last could not be parsed",
                evt_tag_str("last", last_spec));
      return;
    }
  _list_slice(argc - 1, &argv[1], result,
              (gint) first_ndx, (gint) last_ndx);
}

TEMPLATE_FUNCTION_SIMPLE(tf_list_slice);
