/*
 * Copyright © 2015 Red Hat, Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *       Alexander Larsson <alexl@redhat.com>
 */

#include "config.h"

#include "flatpak-utils.h"
#include "flatpak-remote-private.h"
#include "flatpak-remote-ref-private.h"
#include "flatpak-enum-types.h"

#include <string.h>

/**
 * SECTION:flatpak-remote
 * @Short_description: Remote repository
 * @Title: FlatpakRemote
 *
 * A #FlatpakRemote object provides information about a remote
 * repository (or short: remote) that has been configured.
 *
 * At its most basic level, a remote has a name and the URL for
 * the repository. In addition, they provide some additional
 * information that can be useful when presenting repositories
 * in a UI, such as a title, a priority or a "don't enumerate"
 * flags.
 *
 * To obtain FlatpakRemote objects for the configured remotes
 * on a system, use flatpak_installation_list_remotes() or
 * flatpak_installation_get_remote_by_name().
 */

typedef struct _FlatpakRemotePrivate FlatpakRemotePrivate;

struct _FlatpakRemotePrivate
{
  char       *name;
  FlatpakDir *dir;

  char       *local_url;
  char       *local_title;
  char       *local_default_branch;
  gboolean    local_gpg_verify;
  gboolean    local_noenumerate;
  gboolean    local_disabled;
  int         local_prio;

  guint       local_url_set : 1;
  guint       local_title_set : 1;
  guint       local_default_branch_set : 1;
  guint       local_gpg_verify_set : 1;
  guint       local_noenumerate_set : 1;
  guint       local_disabled_set : 1;
  guint       local_prio_set : 1;

  GBytes     *local_gpg_key;
};

G_DEFINE_TYPE_WITH_PRIVATE (FlatpakRemote, flatpak_remote, G_TYPE_OBJECT)

enum {
  PROP_0,

  PROP_NAME,
};

static void
flatpak_remote_finalize (GObject *object)
{
  FlatpakRemote *self = FLATPAK_REMOTE (object);
  FlatpakRemotePrivate *priv = flatpak_remote_get_instance_private (self);

  g_free (priv->name);
  if (priv->dir)
    g_object_unref (priv->dir);
  if (priv->local_gpg_key)
    g_bytes_unref (priv->local_gpg_key);

  g_free (priv->local_url);
  g_free (priv->local_title);
  g_free (priv->local_default_branch);

  G_OBJECT_CLASS (flatpak_remote_parent_class)->finalize (object);
}

static void
flatpak_remote_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  FlatpakRemote *self = FLATPAK_REMOTE (object);
  FlatpakRemotePrivate *priv = flatpak_remote_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_NAME:
      g_clear_pointer (&priv->name, g_free);
      priv->name = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
flatpak_remote_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  FlatpakRemote *self = FLATPAK_REMOTE (object);
  FlatpakRemotePrivate *priv = flatpak_remote_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_NAME:
      g_value_set_string (value, priv->name);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
flatpak_remote_class_init (FlatpakRemoteClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = flatpak_remote_get_property;
  object_class->set_property = flatpak_remote_set_property;
  object_class->finalize = flatpak_remote_finalize;

  g_object_class_install_property (object_class,
                                   PROP_NAME,
                                   g_param_spec_string ("name",
                                                        "Name",
                                                        "The name of the remote",
                                                        NULL,
                                                        G_PARAM_READWRITE));
}

static void
flatpak_remote_init (FlatpakRemote *self)
{
}

/**
 * flatpak_remote_get_name:
 * @self: a #FlatpakRemote
 *
 * Returns the name of the remote repository.
 *
 * Returns: (transfer none): the name
 */
const char *
flatpak_remote_get_name (FlatpakRemote *self)
{
  FlatpakRemotePrivate *priv = flatpak_remote_get_instance_private (self);

  return priv->name;
}

/**
 * flatpak_remote_get_appstream_dir:
 * @self: a #FlatpakRemote
 * @arch: (nullable): which architecture to fetch (default: current architecture)
 *
 * Returns the directory where this remote will store locally cached
 * appstream information for the specified @arch.
 *
 * Returns: (transfer full): a #GFile
 **/
GFile *
flatpak_remote_get_appstream_dir (FlatpakRemote *self,
                                  const char    *arch)
{
  FlatpakRemotePrivate *priv = flatpak_remote_get_instance_private (self);
  g_autofree char *subdir = NULL;

  if (priv->dir == NULL)
    return NULL;

  if (arch == NULL)
    arch = flatpak_get_arch ();

  subdir = g_strdup_printf ("appstream/%s/%s/active", priv->name, arch);
  return g_file_resolve_relative_path (flatpak_dir_get_path (priv->dir),
                                       subdir);
}

/**
 * flatpak_remote_get_appstream_timestamp:
 * @self: a #FlatpakRemote
 * @arch: (nullable): which architecture to fetch (default: current architecture)
 *
 * Returns the timestamp file that will be updated whenever the appstream information
 * has been updated (or tried to update) for the specified @arch.
 *
 * Returns: (transfer full): a #GFile
 **/
GFile *
flatpak_remote_get_appstream_timestamp (FlatpakRemote *self,
                                        const char    *arch)
{
  FlatpakRemotePrivate *priv = flatpak_remote_get_instance_private (self);
  g_autofree char *subdir = NULL;

  if (priv->dir == NULL)
    return NULL;

  if (arch == NULL)
    arch = flatpak_get_arch ();

  subdir = g_strdup_printf ("appstream/%s/%s/.timestamp", priv->name, arch);
  return g_file_resolve_relative_path (flatpak_dir_get_path (priv->dir),
                                       subdir);
}

/**
 * flatpak_remote_get_url:
 * @self: a #FlatpakRemote
 *
 * Returns the repository URL of this remote.
 *
 * Returns: (transfer full): the URL
 */
char *
flatpak_remote_get_url (FlatpakRemote *self)
{
  FlatpakRemotePrivate *priv = flatpak_remote_get_instance_private (self);
  char *url;

  if (priv->local_url_set)
    return g_strdup (priv->local_url);

  if (priv->dir)
    {
      OstreeRepo *repo = flatpak_dir_get_repo (priv->dir);
      if (ostree_repo_remote_get_url (repo, priv->name, &url, NULL))
        return url;
    }

  return NULL;
}

/**
 * flatpak_remote_set_url:
 * @self: a #FlatpakRemote
 * @url: The new url
 *
 * Sets the repository URL of this remote.
 *
 * Note: This is a local modification of this object, you must commit changes
 * using flatpak_installation_modify_remote() for the changes to take
 * effect.
 */
void
flatpak_remote_set_url (FlatpakRemote *self,
                        const char    *url)
{
  FlatpakRemotePrivate *priv = flatpak_remote_get_instance_private (self);

  g_free (priv->local_url);
  priv->local_url = g_strdup (url);
  priv->local_url_set = TRUE;
}

/**
 * flatpak_remote_get_title:
 * @self: a #FlatpakRemote
 *
 * Returns the title of the remote.
 *
 * Returns: (transfer full): the title
 */
char *
flatpak_remote_get_title (FlatpakRemote *self)
{
  FlatpakRemotePrivate *priv = flatpak_remote_get_instance_private (self);

  if (priv->local_title_set)
    return g_strdup (priv->local_title);

  if (priv->dir)
    return flatpak_dir_get_remote_title (priv->dir, priv->name);

  return NULL;
}

/**
 * flatpak_remote_set_title:
 * @self: a #FlatpakRemote
 * @title: The new title
 *
 * Sets the repository title of this remote.
 *
 * Note: This is a local modification of this object, you must commit changes
 * using flatpak_installation_modify_remote() for the changes to take
 * effect.
 */
void
flatpak_remote_set_title (FlatpakRemote *self,
                          const char    *title)
{
  FlatpakRemotePrivate *priv = flatpak_remote_get_instance_private (self);

  g_free (priv->local_title);
  priv->local_title = g_strdup (title);
  priv->local_title_set = TRUE;
}

/**
 * flatpak_remote_get_default_branch:
 * @self: a #FlatpakRemote
 *
 * Returns the default branch configured for the remote.
 *
 * Returns: (transfer full): the default branch, or %NULL
 *
 * Since: 0.6.12
 */
char *
flatpak_remote_get_default_branch (FlatpakRemote *self)
{
  FlatpakRemotePrivate *priv = flatpak_remote_get_instance_private (self);

  if (priv->local_default_branch_set)
    return g_strdup (priv->local_default_branch);

  if (priv->dir)
    return flatpak_dir_get_remote_default_branch (priv->dir, priv->name);

  return NULL;
}

/**
 * flatpak_remote_set_default_branch:
 * @self: a #FlatpakRemote
 * @default_branch: The new default_branch
 *
 * Sets the default branch configured for this remote.
 *
 * Note: This is a local modification of this object, you must commit changes
 * using flatpak_installation_modify_remote() for the changes to take
 * effect.
 *
 * Since: 0.6.12
 */
void
flatpak_remote_set_default_branch (FlatpakRemote *self,
                                   const char    *default_branch)
{
  FlatpakRemotePrivate *priv = flatpak_remote_get_instance_private (self);

  g_free (priv->local_default_branch);
  priv->local_default_branch = g_strdup (default_branch);
  priv->local_default_branch_set = TRUE;
}

/**
 * flatpak_remote_get_noenumerate:
 * @self: a #FlatpakRemote
 *
 * Returns whether this remote should be used to list applications.
 *
 * Returns: whether the remote is marked as "don't enumerate"
 */
gboolean
flatpak_remote_get_noenumerate (FlatpakRemote *self)
{
  FlatpakRemotePrivate *priv = flatpak_remote_get_instance_private (self);

  if (priv->local_noenumerate_set)
    return priv->local_noenumerate;

  if (priv->dir)
    return flatpak_dir_get_remote_noenumerate (priv->dir, priv->name);

  return FALSE;
}

/**
 * flatpak_remote_set_noenumerate:
 * @self: a #FlatpakRemote
 * @noenumerate: a bool
 *
 * Sets the noenumeration config of this remote. See flatpak_remote_get_noenumerate().
 *
 * Note: This is a local modification of this object, you must commit changes
 * using flatpak_installation_modify_remote() for the changes to take
 * effect.
 */
void
flatpak_remote_set_noenumerate (FlatpakRemote *self,
                                gboolean noenumerate)
{
  FlatpakRemotePrivate *priv = flatpak_remote_get_instance_private (self);

  priv->local_noenumerate = noenumerate;
  priv->local_noenumerate_set = TRUE;
}

/**
 * flatpak_remote_get_disabled:
 * @self: a #FlatpakRemote
 *
 * Returns whether this remote is disabled.
 *
 * Returns: whether the remote is marked as "don't enumerate"
 */
gboolean
flatpak_remote_get_disabled (FlatpakRemote *self)
{
  FlatpakRemotePrivate *priv = flatpak_remote_get_instance_private (self);

  if (priv->local_disabled_set)
    return priv->local_disabled;

  if (priv->dir)
    return flatpak_dir_get_remote_disabled (priv->dir, priv->name);

  return FALSE;
}
/**
 * flatpak_remote_set_disabled:
 * @self: a #FlatpakRemote
 * @disabled: a bool
 *
 * Sets the disabled config of this remote. See flatpak_remote_get_disable().
 *
 * Note: This is a local modification of this object, you must commit changes
 * using flatpak_installation_modify_remote() for the changes to take
 * effect.
 */
void
flatpak_remote_set_disabled (FlatpakRemote *self,
                             gboolean disabled)
{
  FlatpakRemotePrivate *priv = flatpak_remote_get_instance_private (self);

  priv->local_disabled = disabled;
  priv->local_disabled_set = TRUE;
}

/**
 * flatpak_remote_get_prio:
 * @self: a #FlatpakRemote
 *
 * Returns the priority for the remote.
 *
 * Returns: the priority
 */
int
flatpak_remote_get_prio (FlatpakRemote *self)
{
  FlatpakRemotePrivate *priv = flatpak_remote_get_instance_private (self);

  if (priv->local_prio_set)
    return priv->local_prio;

  if (priv->dir)
    return flatpak_dir_get_remote_prio (priv->dir, priv->name);

  return 1;
}

/**
 * flatpak_remote_set_prio:
 * @self: a #FlatpakRemote
 * @prio: a bool
 *
 * Sets the prio config of this remote. See flatpak_remote_get_prio().
 *
 * Note: This is a local modification of this object, you must commit changes
 * using flatpak_installation_modify_remote() for the changes to take
 * effect.
 */
void
flatpak_remote_set_prio (FlatpakRemote *self,
                         int prio)
{
  FlatpakRemotePrivate *priv = flatpak_remote_get_instance_private (self);

  priv->local_prio = prio;
  priv->local_prio_set = TRUE;
}

/**
 * flatpak_remote_get_gpg_verify:
 * @self: a #FlatpakRemote
 *
 * Returns whether GPG verification is enabled for the remote.
 *
 * Returns: whether GPG verification is enabled
 */
gboolean
flatpak_remote_get_gpg_verify (FlatpakRemote *self)
{
  FlatpakRemotePrivate *priv = flatpak_remote_get_instance_private (self);
  gboolean res;

  if (priv->local_gpg_verify_set)
    return priv->local_gpg_verify;

  if (priv->dir)
    {
      OstreeRepo *repo = flatpak_dir_get_repo (priv->dir);
      if (ostree_repo_remote_get_gpg_verify (repo, priv->name, &res, NULL))
        return res;
    }

  return FALSE;
}

/**
 * flatpak_remote_set_gpg_verify:
 * @self: a #FlatpakRemote
 * @gpg_verify: a bool
 *
 * Sets the gpg_verify config of this remote. See flatpak_remote_get_gpg_verify().
 *
 * Note: This is a local modification of this object, you must commit changes
 * using flatpak_installation_modify_remote() for the changes to take
 * effect.
 */
void
flatpak_remote_set_gpg_verify (FlatpakRemote *self,
                               gboolean gpg_verify)
{
  FlatpakRemotePrivate *priv = flatpak_remote_get_instance_private (self);

  priv->local_gpg_verify = gpg_verify;
  priv->local_gpg_verify_set = TRUE;
}

/**
 * flatpak_remote_set_gpg_key:
 * @self: a #FlatpakRemote
 * @gpg_key: a #GBytes with gpg binary key data
 *
 * Sets the trusted gpg key for this remote.
 *
 * Note: This is a local modification of this object, you must commit changes
 * using flatpak_installation_modify_remote() for the changes to take
 * effect.
 */
void
flatpak_remote_set_gpg_key (FlatpakRemote *self,
                            GBytes        *gpg_key)
{
  FlatpakRemotePrivate *priv = flatpak_remote_get_instance_private (self);

  if (priv->local_gpg_key != NULL)
    g_bytes_unref (priv->local_gpg_key);
  priv->local_gpg_key = g_bytes_ref (gpg_key);
}

FlatpakRemote *
flatpak_remote_new_with_dir (const char *name,
                             FlatpakDir *dir)
{
  FlatpakRemotePrivate *priv;
  FlatpakRemote *self = g_object_new (FLATPAK_TYPE_REMOTE,
                                      "name", name,
                                      NULL);

  priv = flatpak_remote_get_instance_private (self);
  if (dir)
    priv->dir = g_object_ref (dir);

  return self;
}

/**
 * flatpak_remote_new:
 * @name: a name
 *
 * Returns a new remote object which can be used to configure a new remote.
 *
 * Note: This is a local configuration object, you must commit changes
 * using flatpak_installation_modify_remote() for the changes to take
 * effect.
 *
 * Returns: (transfer full): a new #FlatpakRemote
 **/
FlatpakRemote *
flatpak_remote_new (const char *name)
{
  return flatpak_remote_new_with_dir (name, NULL);
}

gboolean
flatpak_remote_commit (FlatpakRemote   *self,
                       FlatpakDir      *dir,
                       GCancellable    *cancellable,
                       GError         **error)
{
  FlatpakRemotePrivate *priv = flatpak_remote_get_instance_private (self);
  g_autofree char *url = NULL;
  g_autoptr(GKeyFile) config = NULL;
  g_autofree char *group = g_strdup_printf ("remote \"%s\"", priv->name);

  url = flatpak_remote_get_url (self);
  if (url == NULL || *url == 0)
    return flatpak_fail (error, "No url specified");

  config = ostree_repo_copy_config (flatpak_dir_get_repo (dir));
  if (priv->local_url_set)
    g_key_file_set_string (config, group, "url", priv->local_url);

  if (priv->local_title_set)
    g_key_file_set_string (config, group, "xa.title", priv->local_title);

  if (priv->local_default_branch_set)
    g_key_file_set_string (config, group, "xa.default-branch", priv->local_default_branch);

  if (priv->local_gpg_verify_set)
    {
      g_key_file_set_boolean (config, group, "gpg-verify", priv->local_gpg_verify);
      g_key_file_set_boolean (config, group, "gpg-verify-summary", priv->local_gpg_verify);
    }

  if (priv->local_noenumerate_set)
    g_key_file_set_boolean (config, group, "xa.noenumerate", priv->local_noenumerate);

  if (priv->local_disabled_set)
    g_key_file_set_boolean (config, group, "xa.disable", priv->local_disabled);

  if (priv->local_prio_set)
    {
      g_autofree char *prio_as_string = g_strdup_printf ("%d", priv->local_prio);
      g_key_file_set_string (config, group, "xa.prio", prio_as_string);
    }

  return flatpak_dir_modify_remote (dir, priv->name, config, priv->local_gpg_key, cancellable, error);
}
