/****************************************************************************
 * apps/nshlib/nsh_fileapps_helper.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_NSH_FILE_APPS

#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "nsh.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: app_name_compare
 *
 * Description:
 *   Comparison function for qsort to sort file app names
 *
 ****************************************************************************/

static int app_name_compare(const void *a, const void *b)
{
  return strcmp(*(FAR const char * const *)a, *(FAR const char * const *)b);
}

/****************************************************************************
 * Name: deduplicate_file_apps
 *
 * Description:
 *   Sort and remove duplicate file app names
 *
 ****************************************************************************/

static int deduplicate_file_apps(FAR struct file_app_info_s *info)
{
  unsigned int i;
  unsigned int j;

  if (info->count <= 1)
    {
      return OK;
    }

  qsort(info->names, info->count, sizeof(FAR char *), app_name_compare);

  for (i = 0, j = 1; j < info->count; j++)
    {
      if (strcmp(info->names[i], info->names[j]) != 0)
        {
          i++;
          if (i != j)
            {
              info->names[i] = info->names[j];
            }
        }
      else
        {
          free(info->names[j]);
        }
    }

  info->count = i + 1;
  return OK;
}

/****************************************************************************
 * Name: add_file_app
 *
 * Description:
 *   Add a file app name to the collection (duplicates handled later)
 *
 ****************************************************************************/

static int add_file_app(FAR struct file_app_info_s *info,
                        FAR const char *name)
{
  FAR char **tmp;
  FAR char *copy;

  if (info->count >= info->alloc)
    {
      unsigned int alloc = info->alloc == 0 ? 16 : info->alloc * 2;

      tmp = (FAR char **)realloc(info->names, alloc * sizeof(FAR char *));
      if (tmp == NULL)
        {
          return -ENOMEM;
        }

      info->names = tmp;
      info->alloc = alloc;
    }

  copy = strdup(name);
  if (copy == NULL)
    {
      return -ENOMEM;
    }

  info->names[info->count++] = copy;
  return OK;
}

/****************************************************************************
 * Name: is_executable_file_app
 *
 * Description:
 *   Return true if the candidate path refers to a regular executable file.
 *
 ****************************************************************************/

static bool is_executable_file_app(FAR const char *dirpath,
                                   FAR const char *name)
{
  struct stat buf;
  FAR char *filepath;
  size_t dirlen;
  size_t namelen;
  int ret;

  dirlen = strlen(dirpath);
  namelen = strlen(name);
  filepath = (FAR char *)malloc(dirlen + namelen + 2);
  if (filepath == NULL)
    {
      return false;
    }

  memcpy(filepath, dirpath, dirlen);
  filepath[dirlen] = '/';
  memcpy(filepath + dirlen + 1, name, namelen + 1);

  ret = stat(filepath, &buf);
  free(filepath);
  if (ret < 0)
    {
      return false;
    }

  if (!S_ISREG(buf.st_mode) ||
      (buf.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) == 0)
    {
      return false;
    }

  return true;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nsh_free_file_apps
 ****************************************************************************/

void nsh_free_file_apps(FAR struct file_app_info_s *info)
{
  unsigned int i;

  for (i = 0; i < info->count; i++)
    {
      free(info->names[i]);
    }

  free(info->names);
  info->names = NULL;
  info->count = 0;
  info->alloc = 0;
}

/****************************************************************************
 * Name: nsh_collect_path_file_apps
 *
 * Description:
 *   Collect file app names from PATH for tab-completion cache population.
 *
 ****************************************************************************/

int nsh_collect_path_file_apps(FAR struct file_app_info_s *info,
                               FAR const char *prefix,
                               int prefix_len)
{
  FAR char *pathenv;
  FAR char *pathlist;
  FAR char *saveptr = NULL;
  FAR char *dirpath;

  nsh_free_file_apps(info);

#ifndef CONFIG_DISABLE_ENVIRON
  pathenv = getenv("PATH");
#else
  pathenv = NULL;
#endif

  if (pathenv == NULL || pathenv[0] == '\0')
    {
      return OK;
    }

  pathlist = strdup(pathenv);
  if (pathlist == NULL)
    {
      return -ENOMEM;
    }

  for (dirpath = strtok_r(pathlist, ":", &saveptr);
       dirpath != NULL;
       dirpath = strtok_r(NULL, ":", &saveptr))
    {
      DIR *dirp;
      FAR struct dirent *entryp;

      if (dirpath[0] == '\0')
        {
          dirpath = ".";
        }

      dirp = opendir(dirpath);
      if (dirp == NULL)
        {
          continue;
        }

      while ((entryp = readdir(dirp)) != NULL)
        {
          int ret;

          if (strcmp(entryp->d_name, ".") == 0 ||
              strcmp(entryp->d_name, "..") == 0)
            {
              continue;
            }

          if (DIRENT_ISDIRECTORY(entryp->d_type))
            {
              continue;
            }

          if (prefix_len > 0 &&
              strncmp(entryp->d_name, prefix, prefix_len) != 0)
            {
              continue;
            }

          if (!is_executable_file_app(dirpath, entryp->d_name))
            {
              continue;
            }

          ret = add_file_app(info, entryp->d_name);
          if (ret < 0)
            {
              closedir(dirp);
              free(pathlist);
              nsh_free_file_apps(info);
              return ret;
            }
        }

      closedir(dirp);
    }

  free(pathlist);
  return deduplicate_file_apps(info);
}

#endif /* CONFIG_NSH_FILE_APPS */
