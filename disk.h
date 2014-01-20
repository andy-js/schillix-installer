/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 *
 * Installer for Schillix
 * (c) Copyright 2013 - Andrew Stormont <andyjstormont@gmail.com>
 */

#include <libzfs.h>

boolean_t disk_in_use(libzfs_handle_t *libzfs_handle, char *disk);
boolean_t create_root_partition (char *disk);
boolean_t create_root_vtoc (char *disk);
boolean_t create_root_pool (libzfs_handle_t *libzfs_handle, char *disk, char *pool, char *mnt);
boolean_t export_root_pool (libzfs_handle_t *libzfs_handle, char *pool);
boolean_t create_root_datasets (libzfs_handle_t *libzfs_handle, char *pool);
boolean_t set_root_bootfs (libzfs_handle_t *libzfs_handle, char *pool);
boolean_t mount_root_datasets (libzfs_handle_t *libzfs_handle, char *pool);
boolean_t unmount_root_datasets (libzfs_handle_t *libzfs_handle, char *pool);
