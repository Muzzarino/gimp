/* GIMP - The GNU Image Manipulation Program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <string.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "tinyscheme/scheme-private.h"

#include "script-fu-types.h"

#include "script-fu-script.h"
#include "script-fu-scripts.h"
#include "script-fu-utils.h"

#include "script-fu-intl.h"


/*
 *  Local Functions
 */

static gboolean   script_fu_script_param_init (SFScript             *script,
                                               const GimpValueArray *args,
                                               SFArgType             type,
                                               gint                  n);




/*
 *  Function definitions
 */

SFScript *
script_fu_script_new (const gchar *name,
                      const gchar *menu_label,
                      const gchar *blurb,
                      const gchar *author,
                      const gchar *copyright,
                      const gchar *date,
                      const gchar *image_types,
                      gint         n_args)
{
  SFScript *script;

  script = g_slice_new0 (SFScript);

  script->name        = g_strdup (name);
  script->menu_label  = g_strdup (menu_label);
  script->blurb       = g_strdup (blurb);
  script->author      = g_strdup (author);
  script->copyright   = g_strdup (copyright);
  script->date        = g_strdup (date);
  script->image_types = g_strdup (image_types);

  script->n_args = n_args;
  script->args   = g_new0 (SFArg, script->n_args);

  return script;
}

void
script_fu_script_free (SFScript *script)
{
  gint i;

  g_return_if_fail (script != NULL);

  g_free (script->name);
  g_free (script->blurb);
  g_free (script->menu_label);
  g_free (script->author);
  g_free (script->copyright);
  g_free (script->date);
  g_free (script->image_types);

  for (i = 0; i < script->n_args; i++)
    {
      SFArg *arg = &script->args[i];

      g_free (arg->label);

      switch (arg->type)
        {
        case SF_IMAGE:
        case SF_DRAWABLE:
        case SF_LAYER:
        case SF_CHANNEL:
        case SF_VECTORS:
        case SF_DISPLAY:
        case SF_COLOR:
        case SF_TOGGLE:
          break;

        case SF_VALUE:
        case SF_STRING:
        case SF_TEXT:
          g_free (arg->default_value.sfa_value);
          g_free (arg->value.sfa_value);
          break;

        case SF_ADJUSTMENT:
          break;

        case SF_FILENAME:
        case SF_DIRNAME:
          g_free (arg->default_value.sfa_file.filename);
          g_free (arg->value.sfa_file.filename);
          break;

        case SF_FONT:
          g_free (arg->default_value.sfa_font);
          g_free (arg->value.sfa_font);
          break;

        case SF_PALETTE:
          g_free (arg->default_value.sfa_palette);
          g_free (arg->value.sfa_palette);
          break;

        case SF_PATTERN:
          g_free (arg->default_value.sfa_pattern);
          g_free (arg->value.sfa_pattern);
          break;

        case SF_GRADIENT:
          g_free (arg->default_value.sfa_gradient);
          g_free (arg->value.sfa_gradient);
          break;

        case SF_BRUSH:
          g_free (arg->default_value.sfa_brush.name);
          g_free (arg->value.sfa_brush.name);
          break;

        case SF_OPTION:
          g_slist_free_full (arg->default_value.sfa_option.list,
                             (GDestroyNotify) g_free);
          break;

        case SF_ENUM:
          g_free (arg->default_value.sfa_enum.type_name);
          break;
        }
    }

  g_free (script->args);

  g_slice_free (SFScript, script);
}


/*
 * From the script, create a temporary PDB procedure,
 * and install it as owned by the scriptfu extension PDB proc.
 */
void
script_fu_script_install_proc (GimpPlugIn  *plug_in,
                               SFScript    *script,
                               GimpRunFunc  run_func)
{
  GimpProcedure *procedure;

  g_return_if_fail (GIMP_IS_PLUG_IN (plug_in));
  g_return_if_fail (script != NULL);
  g_return_if_fail (run_func != NULL);

  procedure = script_fu_script_create_PDB_procedure (plug_in,
                                                     script,
                                                     run_func,
                                                     GIMP_PDB_PROC_TYPE_TEMPORARY);

  gimp_plug_in_add_temp_procedure (plug_in, procedure);
  g_object_unref (procedure);
}


/*
 * Create and return a GimpProcedure.
 * Caller typically either:
 *    install it owned by self as TEMPORARY type procedure
 *    OR return it as the result of a create_procedure callback from GIMP (PLUGIN type procedure.)
 *
 * Caller must unref the procedure.
 */
GimpProcedure *
script_fu_script_create_PDB_procedure (GimpPlugIn     *plug_in,
                                       SFScript       *script,
                                       GimpRunFunc     run_func,
                                       GimpPDBProcType plug_in_type)
{
  GimpProcedure *procedure;
  const gchar   *menu_label            = NULL;
  gint           arg_count[SF_DISPLAY] = { 0, };
  gint           i;

  g_debug ("script_fu_script_create_PDB_procedure: %s of type %i", script->name, plug_in_type);

  /* Allow scripts with no menus */
  if (strncmp (script->menu_label, "<None>", 6) != 0)
    menu_label = script->menu_label;

  procedure = gimp_procedure_new (plug_in, script->name,
                                  plug_in_type,
                                  run_func, script, NULL);

  gimp_procedure_set_image_types (procedure, script->image_types);

  if (menu_label && strlen (menu_label))
    gimp_procedure_set_menu_label (procedure, menu_label);

  gimp_procedure_set_documentation (procedure,
                                    script->blurb,
                                    NULL,
                                    script->name);
  gimp_procedure_set_attribution (procedure,
                                  script->author,
                                  script->copyright,
                                  script->date);

  gimp_procedure_add_argument (procedure,
                               g_param_spec_enum ("run-mode",
                                                  "Run mode",
                                                  "The run mode",
                                                  GIMP_TYPE_RUN_MODE,
                                                  GIMP_RUN_INTERACTIVE,
                                                  G_PARAM_READWRITE));

  for (i = 0; i < script->n_args; i++)
    {
      GParamSpec  *pspec = NULL;
      const gchar *name  = NULL;
      const gchar *nick  = NULL;
      gchar        numbered_name[64];
      gchar        numbered_nick[64];

      switch (script->args[i].type)
        {
        case SF_IMAGE:
          name = "image";
          nick = "Image";
          break;

        case SF_DRAWABLE:
          name = "drawable";
          nick = "Drawable";
          break;

        case SF_LAYER:
          name = "layer";
          nick = "Layer";
          break;

        case SF_CHANNEL:
          name = "channel";
          nick = "Channel";
          break;

        case SF_VECTORS:
          name = "vectors";
          nick = "Vectors";
          break;

        case SF_DISPLAY:
          name = "display";
          nick = "Display";
          break;

        case SF_COLOR:
          name = "color";
          nick = "Color";
          break;

        case SF_TOGGLE:
          name = "toggle";
          nick = "Toggle";
          break;

        case SF_VALUE:
          name = "value";
          nick = "Value";
          break;

        case SF_STRING:
          name = "string";
          nick = "String";
          break;

        case SF_TEXT:
          name = "text";
          nick = "Text";
          break;

        case SF_ADJUSTMENT:
          name = "adjustment";
          nick = "Adjustment";
          break;

        case SF_FILENAME:
          name = "filename";
          nick = "Filename";
          break;

        case SF_DIRNAME:
          name = "dirname";
          nick = "Dirname";
          break;

        case SF_FONT:
          name = "font";
          nick = "Font";
          break;

        case SF_PALETTE:
          name = "palette";
          nick = "Palette";
          break;

        case SF_PATTERN:
          name = "pattern";
          nick = "Pattern";
          break;

        case SF_BRUSH:
          name = "brush";
          nick = "Brush";
          break;

        case SF_GRADIENT:
          name = "gradient";
          nick = "Gradient";
          break;

        case SF_OPTION:
          name = "option";
          nick = "Option";
          break;

        case SF_ENUM:
          name = "enum";
          nick = "Enum";
          break;
        }

      if (arg_count[script->args[i].type] == 0)
        {
          g_strlcpy (numbered_name, name, sizeof (numbered_name));
          g_strlcpy (numbered_nick, nick, sizeof (numbered_nick));
        }
      else
        {
          g_snprintf (numbered_name, sizeof (numbered_name),
                      "%s-%d", name, arg_count[script->args[i].type] + 1);
          g_snprintf (numbered_nick, sizeof (numbered_nick),
                      "%s %d", nick, arg_count[script->args[i].type] + 1);
        }

      arg_count[script->args[i].type]++;

      switch (script->args[i].type)
        {
        case SF_IMAGE:
          pspec = gimp_param_spec_image (numbered_name,
                                         numbered_nick,
                                         script->args[i].label,
                                         TRUE,
                                         G_PARAM_READWRITE);
          break;

        case SF_DRAWABLE:
          pspec = gimp_param_spec_drawable (numbered_name,
                                            numbered_nick,
                                            script->args[i].label,
                                            TRUE,
                                            G_PARAM_READWRITE);
          break;

        case SF_LAYER:
          pspec = gimp_param_spec_layer (numbered_name,
                                         numbered_nick,
                                         script->args[i].label,
                                         TRUE,
                                         G_PARAM_READWRITE);
          break;

        case SF_CHANNEL:
          pspec = gimp_param_spec_channel (numbered_name,
                                           numbered_nick,
                                           script->args[i].label,
                                           TRUE,
                                           G_PARAM_READWRITE);
          break;

        case SF_VECTORS:
          pspec = gimp_param_spec_vectors (numbered_name,
                                           numbered_nick,
                                           script->args[i].label,
                                           TRUE,
                                           G_PARAM_READWRITE);
          break;

        case SF_DISPLAY:
          pspec = gimp_param_spec_display (numbered_name,
                                           numbered_nick,
                                           script->args[i].label,
                                           TRUE,
                                           G_PARAM_READWRITE);
          break;

        case SF_COLOR:
          pspec = gimp_param_spec_rgb (numbered_name,
                                       numbered_nick,
                                       script->args[i].label,
                                       TRUE, NULL,
                                       G_PARAM_READWRITE);
          break;

        case SF_TOGGLE:
          pspec = g_param_spec_boolean (numbered_name,
                                        numbered_nick,
                                        script->args[i].label,
                                        FALSE,
                                        G_PARAM_READWRITE);
          break;

        case SF_VALUE:
          pspec = g_param_spec_string (numbered_name,
                                       numbered_nick,
                                       script->args[i].label,
                                       NULL,
                                       G_PARAM_READWRITE);
          break;

        case SF_STRING:
        case SF_TEXT:
          pspec = g_param_spec_string (numbered_name,
                                       numbered_nick,
                                       script->args[i].label,
                                       NULL,
                                       G_PARAM_READWRITE);
          break;

        case SF_ADJUSTMENT:
          pspec = g_param_spec_double (numbered_name,
                                       numbered_nick,
                                       script->args[i].label,
                                       -G_MAXDOUBLE, G_MAXDOUBLE, 0,
                                       G_PARAM_READWRITE);
          break;

        case SF_FILENAME:
          pspec = g_param_spec_string (numbered_name,
                                       numbered_nick,
                                       script->args[i].label,
                                       NULL,
                                       G_PARAM_READWRITE |
                                       GIMP_PARAM_NO_VALIDATE);
          break;

        case SF_DIRNAME:
          pspec = g_param_spec_string (numbered_name,
                                       numbered_nick,
                                       script->args[i].label,
                                       NULL,
                                       G_PARAM_READWRITE |
                                       GIMP_PARAM_NO_VALIDATE);
          break;

        case SF_FONT:
          pspec = g_param_spec_string (numbered_name,
                                       numbered_nick,
                                       script->args[i].label,
                                       NULL,
                                       G_PARAM_READWRITE);
          break;

        case SF_PALETTE:
          pspec = g_param_spec_string (numbered_name,
                                       numbered_nick,
                                       script->args[i].label,
                                       NULL,
                                       G_PARAM_READWRITE);
          break;

        case SF_PATTERN:
          pspec = g_param_spec_string (numbered_name,
                                       numbered_nick,
                                       script->args[i].label,
                                       NULL,
                                       G_PARAM_READWRITE);
          break;

        case SF_BRUSH:
          pspec = g_param_spec_string (numbered_name,
                                       numbered_nick,
                                       script->args[i].label,
                                       NULL,
                                       G_PARAM_READWRITE);
          break;

        case SF_GRADIENT:
          pspec = g_param_spec_string (numbered_name,
                                       numbered_nick,
                                       script->args[i].label,
                                       NULL,
                                       G_PARAM_READWRITE);
          break;

        case SF_OPTION:
          pspec = g_param_spec_int (numbered_name,
                                    numbered_nick,
                                    script->args[i].label,
                                    G_MININT, G_MAXINT, 0,
                                    G_PARAM_READWRITE);
          break;

        case SF_ENUM:
          pspec = g_param_spec_int (numbered_name,
                                    numbered_nick,
                                    script->args[i].label,
                                    G_MININT, G_MAXINT, 0,
                                    G_PARAM_READWRITE);
          break;
        }

      gimp_procedure_add_argument (procedure, pspec);
    }

  return procedure;
}

void
script_fu_script_uninstall_proc (GimpPlugIn *plug_in,
                                 SFScript   *script)
{
  g_return_if_fail (GIMP_IS_PLUG_IN (plug_in));
  g_return_if_fail (script != NULL);

  gimp_plug_in_remove_temp_procedure (plug_in, script->name);
}

gchar *
script_fu_script_get_title (SFScript *script)
{
  gchar *title;
  gchar *tmp;

  g_return_val_if_fail (script != NULL, NULL);

  /* strip mnemonics from the menupath */
  title = gimp_strip_uline (script->menu_label);

  /* if this looks like a full menu path, use only the last part */
  if (title[0] == '<' && (tmp = strrchr (title, '/')) && tmp[1])
    {
      tmp = g_strdup (tmp + 1);

      g_free (title);
      title = tmp;
    }

  /* cut off ellipsis */
  tmp = (strstr (title, "..."));
  if (! tmp)
    /* U+2026 HORIZONTAL ELLIPSIS */
    tmp = strstr (title, "\342\200\246");

  if (tmp && tmp == (title + strlen (title) - 3))
    *tmp = '\0';

  return title;
}

void
script_fu_script_reset (SFScript *script,
                        gboolean  reset_ids)
{
  gint i;

  g_return_if_fail (script != NULL);

  for (i = 0; i < script->n_args; i++)
    {
      SFArgValue *value         = &script->args[i].value;
      SFArgValue *default_value = &script->args[i].default_value;

      switch (script->args[i].type)
        {
        case SF_IMAGE:
        case SF_DRAWABLE:
        case SF_LAYER:
        case SF_CHANNEL:
        case SF_VECTORS:
        case SF_DISPLAY:
          if (reset_ids)
            value->sfa_image = default_value->sfa_image;
          break;

        case SF_COLOR:
          value->sfa_color = default_value->sfa_color;
          break;

        case SF_TOGGLE:
          value->sfa_toggle = default_value->sfa_toggle;
          break;

        case SF_VALUE:
        case SF_STRING:
        case SF_TEXT:
          g_free (value->sfa_value);
          value->sfa_value = g_strdup (default_value->sfa_value);
          break;

        case SF_ADJUSTMENT:
          value->sfa_adjustment.value = default_value->sfa_adjustment.value;
          break;

        case SF_FILENAME:
        case SF_DIRNAME:
          g_free (value->sfa_file.filename);
          value->sfa_file.filename = g_strdup (default_value->sfa_file.filename);
          break;

        case SF_FONT:
          g_free (value->sfa_font);
          value->sfa_font = g_strdup (default_value->sfa_font);
          break;

        case SF_PALETTE:
          g_free (value->sfa_palette);
          value->sfa_palette = g_strdup (default_value->sfa_palette);
          break;

        case SF_PATTERN:
          g_free (value->sfa_pattern);
          value->sfa_pattern = g_strdup (default_value->sfa_pattern);
          break;

        case SF_GRADIENT:
          g_free (value->sfa_gradient);
          value->sfa_gradient = g_strdup (default_value->sfa_gradient);
          break;

        case SF_BRUSH:
          g_free (value->sfa_brush.name);
          value->sfa_brush.name = g_strdup (default_value->sfa_brush.name);
          value->sfa_brush.opacity    = default_value->sfa_brush.opacity;
          value->sfa_brush.spacing    = default_value->sfa_brush.spacing;
          value->sfa_brush.paint_mode = default_value->sfa_brush.paint_mode;
          break;

        case SF_OPTION:
          value->sfa_option.history = default_value->sfa_option.history;
          break;

        case SF_ENUM:
          value->sfa_enum.history = default_value->sfa_enum.history;
          break;
        }
    }
}

gint
script_fu_script_collect_standard_args (SFScript             *script,
                                        const GimpValueArray *args)
{
  gint params_consumed = 0;

  g_return_val_if_fail (script != NULL, 0);

  /*  the first parameter may be a DISPLAY id  */
  if (script_fu_script_param_init (script,
                                   args, SF_DISPLAY,
                                   params_consumed))
    {
      params_consumed++;
    }

  /*  an IMAGE id may come first or after the DISPLAY id  */
  if (script_fu_script_param_init (script,
                                   args, SF_IMAGE,
                                   params_consumed))
    {
      params_consumed++;

      /*  and may be followed by a DRAWABLE, LAYER, CHANNEL or
       *  VECTORS id
       */
      if (script_fu_script_param_init (script,
                                       args, SF_DRAWABLE,
                                       params_consumed) ||
          script_fu_script_param_init (script,
                                       args, SF_LAYER,
                                       params_consumed) ||
          script_fu_script_param_init (script,
                                       args, SF_CHANNEL,
                                       params_consumed) ||
          script_fu_script_param_init (script,
                                       args, SF_VECTORS,
                                       params_consumed))
        {
          params_consumed++;
        }
    }

  return params_consumed;
}

gchar *
script_fu_script_get_command (SFScript *script)
{
  GString *s;
  gint     i;

  g_return_val_if_fail (script != NULL, NULL);

  s = g_string_new ("(");
  g_string_append (s, script->name);

  for (i = 0; i < script->n_args; i++)
    {
      SFArgValue *arg_value = &script->args[i].value;

      g_string_append_c (s, ' ');

      switch (script->args[i].type)
        {
        case SF_IMAGE:
        case SF_DRAWABLE:
        case SF_LAYER:
        case SF_CHANNEL:
        case SF_VECTORS:
        case SF_DISPLAY:
          g_string_append_printf (s, "%d", arg_value->sfa_image);
          break;

        case SF_COLOR:
          {
            guchar r, g, b;

            gimp_rgb_get_uchar (&arg_value->sfa_color, &r, &g, &b);
            g_string_append_printf (s, "'(%d %d %d)",
                                    (gint) r, (gint) g, (gint) b);
          }
          break;

        case SF_TOGGLE:
          g_string_append (s, arg_value->sfa_toggle ? "TRUE" : "FALSE");
          break;

        case SF_VALUE:
          g_string_append (s, arg_value->sfa_value);
          break;

        case SF_STRING:
        case SF_TEXT:
          {
            gchar *tmp;

            tmp = script_fu_strescape (arg_value->sfa_value);
            g_string_append_printf (s, "\"%s\"", tmp);
            g_free (tmp);
          }
          break;

        case SF_ADJUSTMENT:
          {
            gchar buffer[G_ASCII_DTOSTR_BUF_SIZE];

            g_ascii_dtostr (buffer, sizeof (buffer),
                            arg_value->sfa_adjustment.value);
            g_string_append (s, buffer);
          }
          break;

        case SF_FILENAME:
        case SF_DIRNAME:
          {
            gchar *tmp;

            tmp = script_fu_strescape (arg_value->sfa_file.filename);
            g_string_append_printf (s, "\"%s\"", tmp);
            g_free (tmp);
          }
          break;

        case SF_FONT:
          g_string_append_printf (s, "\"%s\"", arg_value->sfa_font);
          break;

        case SF_PALETTE:
          g_string_append_printf (s, "\"%s\"", arg_value->sfa_palette);
          break;

        case SF_PATTERN:
          g_string_append_printf (s, "\"%s\"", arg_value->sfa_pattern);
          break;

        case SF_GRADIENT:
          g_string_append_printf (s, "\"%s\"", arg_value->sfa_gradient);
          break;

        case SF_BRUSH:
          {
            gchar buffer[G_ASCII_DTOSTR_BUF_SIZE];

            g_ascii_dtostr (buffer, sizeof (buffer),
                            arg_value->sfa_brush.opacity);
            g_string_append_printf (s, "'(\"%s\" %s %d %d)",
                                    arg_value->sfa_brush.name,
                                    buffer,
                                    arg_value->sfa_brush.spacing,
                                    arg_value->sfa_brush.paint_mode);
          }
          break;

        case SF_OPTION:
          g_string_append_printf (s, "%d", arg_value->sfa_option.history);
          break;

        case SF_ENUM:
          g_string_append_printf (s, "%d", arg_value->sfa_enum.history);
          break;
        }
    }

  g_string_append_c (s, ')');

  return g_string_free (s, FALSE);
}

gchar *
script_fu_script_get_command_from_params (SFScript             *script,
                                          const GimpValueArray *args)
{
  GString *s;
  gint     i;

  g_return_val_if_fail (script != NULL, NULL);

  s = g_string_new ("(");
  g_string_append (s, script->name);

  for (i = 0; i < script->n_args; i++)
    {
      GValue *value = gimp_value_array_index (args, i + 1);

      g_string_append_c (s, ' ');

      switch (script->args[i].type)
        {
        case SF_IMAGE:
        case SF_DRAWABLE:
        case SF_LAYER:
        case SF_CHANNEL:
        case SF_VECTORS:
        case SF_DISPLAY:
          {
            GObject *object = g_value_get_object (value);
            gint     id     = -1;

            if (object)
              g_object_get (object, "id", &id, NULL);

            g_string_append_printf (s, "%d", id);
          }
          break;

        case SF_COLOR:
          {
            GimpRGB color;
            guchar  r, g, b;

            gimp_value_get_rgb (value, &color);
            gimp_rgb_get_uchar (&color, &r, &g, &b);
            g_string_append_printf (s, "'(%d %d %d)",
                                    (gint) r, (gint) g, (gint) b);
          }
          break;

        case SF_TOGGLE:
          g_string_append_printf (s, (g_value_get_boolean (value) ?
                                      "TRUE" : "FALSE"));
          break;

        case SF_VALUE:
          g_string_append (s, g_value_get_string (value));
          break;

        case SF_STRING:
        case SF_TEXT:
        case SF_FILENAME:
        case SF_DIRNAME:
          {
            gchar *tmp;

            tmp = script_fu_strescape (g_value_get_string (value));
            g_string_append_printf (s, "\"%s\"", tmp);
            g_free (tmp);
          }
          break;

        case SF_ADJUSTMENT:
          {
            gchar buffer[G_ASCII_DTOSTR_BUF_SIZE];

            g_ascii_dtostr (buffer, sizeof (buffer), g_value_get_double (value));
            g_string_append (s, buffer);
          }
          break;

        case SF_FONT:
        case SF_PALETTE:
        case SF_PATTERN:
        case SF_GRADIENT:
        case SF_BRUSH:
          g_string_append_printf (s, "\"%s\"", g_value_get_string (value));
          break;

        case SF_OPTION:
        case SF_ENUM:
          g_string_append_printf (s, "%d", g_value_get_int (value));
          break;
        }
    }

  g_string_append_c (s, ')');

  return g_string_free (s, FALSE);
}


/*
 *  Local Functions
 */

static gboolean
script_fu_script_param_init (SFScript             *script,
                             const GimpValueArray *args,
                             SFArgType             type,
                             gint                  n)
{
  SFArg *arg = &script->args[n];

  if (script->n_args > n &&
      arg->type == type  &&
      gimp_value_array_length (args) > n + 1)
    {
      GValue *value = gimp_value_array_index (args, n + 1);

      switch (type)
        {
        case SF_IMAGE:
          if (GIMP_VALUE_HOLDS_IMAGE (value))
            {
              GimpImage *image = g_value_get_object (value);

              arg->value.sfa_image = gimp_image_get_id (image);
              return TRUE;
            }
          break;

        case SF_DRAWABLE:
          if (GIMP_VALUE_HOLDS_DRAWABLE (value))
            {
              GimpItem *item = g_value_get_object (value);

              arg->value.sfa_drawable = gimp_item_get_id (item);
              return TRUE;
            }
          break;

        case SF_LAYER:
          if (GIMP_VALUE_HOLDS_LAYER (value))
            {
              GimpItem *item = g_value_get_object (value);

              arg->value.sfa_layer = gimp_item_get_id (item);
              return TRUE;
            }
          break;

        case SF_CHANNEL:
          if (GIMP_VALUE_HOLDS_CHANNEL (value))
            {
              GimpItem *item = g_value_get_object (value);

              arg->value.sfa_channel = gimp_item_get_id (item);
              return TRUE;
            }
          break;

        case SF_VECTORS:
          if (GIMP_VALUE_HOLDS_VECTORS (value))
            {
              GimpItem *item = g_value_get_object (value);

              arg->value.sfa_vectors = gimp_item_get_id (item);
              return TRUE;
            }
          break;

        case SF_DISPLAY:
          if (GIMP_VALUE_HOLDS_DISPLAY (value))
            {
              GimpDisplay *display = g_value_get_object (value);

              arg->value.sfa_display = gimp_display_get_id (display);
              return TRUE;
            }
          break;

        default:
          break;
        }
    }

  return FALSE;
}