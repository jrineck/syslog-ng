/*
 * Copyright (c) 2015-2016 Balabit
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
#include "kv-scanner.h"
#include "str-repr/decode.h"
#include <string.h>

static inline gboolean
_is_valid_key_character(gchar c)
{
  return (c >= 'a' && c <= 'z') ||
         (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') ||
         (c == '_') ||
         (c == '-');
}

static inline const gchar *
_locate_separator(KVScanner *self, const gchar *start)
{
  return strchr(start, self->value_separator);
}

static inline void
_locate_start_of_key(KVScanner *self, const gchar *end_of_key, const gchar **start_of_key)
{
  const gchar *input = &self->input[self->input_pos];
  const gchar *cur;

  cur = end_of_key;
  while (cur > input && _is_valid_key_character(*(cur - 1)))
    cur--;
  *start_of_key = cur;
}

static inline void
_locate_end_of_key(KVScanner *self, const gchar *separator, const gchar **end_of_key)
{
  const gchar *input = &self->input[self->input_pos];
  const gchar *cur;

  /* this function locates the character pointing right next to the end of
   * the key, e.g. with this input
   *   foo   = bar
   *
   * it would start with the '=' sign and skip spaces backwards, to locate
   * the space right next to "foo" */

  cur = separator;
  if (self->allow_space)
    {
      while (cur > input && (*(cur - 1)) == ' ')
        cur--;
    }
  *end_of_key = cur;
}

static inline gboolean
_extract_key_from_positions(KVScanner *self, const gchar *start_of_key, const gchar *end_of_key)
{
  gint len = end_of_key - start_of_key;

  if (len >= 1)
    {
      g_string_assign_len(self->key, start_of_key, len);
      return TRUE;
    }
  return FALSE;
}

static gboolean
_extract_key(KVScanner *self)
{
  const gchar *input = &self->input[self->input_pos];
  const gchar *start_of_key, *end_of_key;
  const gchar *separator;

  separator = _locate_separator(self, input);
  while (separator)
    {
      _locate_end_of_key(self, separator, &end_of_key);
      _locate_start_of_key(self, end_of_key, &start_of_key);

      if (_extract_key_from_positions(self, start_of_key, end_of_key))
        {
          self->input_pos = separator - self->input + 1;
          return TRUE;
        }
      separator = _locate_separator(self, separator + 1);
    }

  return FALSE;
}

static gboolean
_is_quoted(const gchar *input)
{
  return *input == '\'' || *input == '\"';
}

static gboolean
_key_follows(KVScanner *self, const gchar *cur)
{
  const gchar *key = cur;

  while (_is_valid_key_character(*key))
    key++;

  while (*key == ' ')
    key++;
  return (key != cur) && (*key == self->value_separator);
}

static inline void
_skip_spaces(const gchar **input)
{
  const gchar *cur = *input;

  while (*cur == ' ')
    cur++;
  *input = cur;
}

static inline gboolean
_end_of_string(const gchar *cur)
{
  return *cur == 0;
}

static gboolean
_match_delimiter(const gchar *cur, const gchar **new_cur, gpointer user_data)
{
  KVScanner *self = (gpointer) user_data;
  gboolean result = FALSE;

  if (!self->value_was_quoted &&
      self->allow_space &&
      *cur == ' ')
    {
      _skip_spaces(&cur);

      if (_end_of_string(cur) ||
          _key_follows(self, cur))
        {
          *new_cur = cur;
          result = TRUE;
        }
    }
  else
    {
      result = (*cur == ' ') || (strncmp(cur, ", ", 2) == 0);
      *new_cur = cur + 1;
    }
  return result;
}

static inline void
_skip_initial_spaces(KVScanner *self)
{
  if (self->allow_space)
    {
      const gchar *input = &self->input[self->input_pos];
      const gchar *end;

      while (*input == ' ' && !_match_delimiter(input, &end, self))
        input++;
      self->input_pos = input - self->input;
    }
}

static inline void
_decode_value(KVScanner *self)
{
  const gchar *input = &self->input[self->input_pos];
  const gchar *end;

  self->value_was_quoted = _is_quoted(input);
  if (str_repr_decode_until_delimiter(self->value, input, &end, _match_delimiter, self))
    {
      self->input_pos = end - self->input;
    }
  else
    {
      /* quotation error, set was_quoted to FALSE */
      self->value_was_quoted = FALSE;
      g_string_truncate(self->value, 0);
      g_string_append_len(self->value, input, end - input);
    }
}

static void
_extract_value(KVScanner *self)
{
  _skip_initial_spaces(self);
  _decode_value(self);
}

static inline void
_transform_value(KVScanner *self)
{
  if (self->transform_value)
    {
      g_string_truncate(self->decoded_value, 0);
      if (self->transform_value(self))
        g_string_assign_len(self->value, self->decoded_value->str, self->decoded_value->len);
    }
}

gboolean
kv_scanner_scan_next(KVScanner *s)
{
  KVScanner *self = (KVScanner *)s;

  if (!_extract_key(self))
    return FALSE;

  _extract_value(self);
  _transform_value(s);

  return TRUE;
}

static KVScanner *
_clone(KVScanner *s)
{
  KVScanner *self = (KVScanner *) s;
  return kv_scanner_new(s->value_separator, s->transform_value, self->allow_space);
}

void
kv_scanner_init(KVScanner *self, gchar value_separator, KVTransformValueFunc transform_value)
{
  self->key = g_string_sized_new(32);
  self->value = g_string_sized_new(64);
  self->decoded_value = g_string_sized_new(64);
  self->value_separator = value_separator;
  self->transform_value = transform_value;
}

void
kv_scanner_free(KVScanner *self)
{
  if (!self)
    return;
  g_string_free(self->key, TRUE);
  g_string_free(self->value, TRUE);
  g_string_free(self->decoded_value, TRUE);
  g_free(self);
}

KVScanner *
kv_scanner_new(gchar value_separator, KVTransformValueFunc transform_value, gboolean allow_space)
{
  KVScanner *self = g_new0(KVScanner, 1);

  kv_scanner_init(self, value_separator, transform_value);
  self->clone = _clone;
  self->allow_space = allow_space;

  return self;
}
