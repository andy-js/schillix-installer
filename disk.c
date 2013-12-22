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

#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <libzfs.h>
#include <libnvpair.h>
#include <parted/parted.h>
#include <sys/dkio.h>
#include <sys/vtoc.h>

#include "disk.h"

/*
 * Determine if a disk is in use already
 */
boolean_t
disk_in_use (libzfs_handle_t *libzfs_handle, char *disk)
{
	int fd;
	char *poolname, path[PATH_MAX];
	pool_state_t poolstate;
	boolean_t inuse = B_FALSE;

	(void) sprintf (path, "%ss0", disk);

	/*
	 * If we can't open the disk, assume it's in use.
	 */
	if ((fd = open (path, O_RDONLY)) == -1)
	{
		/*
		 * If the disk has no s0 slice yet we can probably consider it unused
		 */
		if (errno == ENOENT || errno == EIO)
			return B_FALSE;

		fprintf (stderr, "Error: Unable to probe disk: %s\n", strerror (errno));
		return B_TRUE;
	}

	/*
	 * Check to see if the disk is already part of a zpool.
	 */
	if (zpool_in_use(libzfs_handle, fd, &poolstate, &poolname, &inuse) == -1)
	{
		fprintf (stderr, "Error: Unable to determine if disk is in a zpool\n");
		(void) close (fd);
		return B_TRUE;
	}

	(void) close (fd);

	if (inuse == B_TRUE)
	{
		fprintf (stderr, "Error: Disk is already part of pool: %s\n", poolname);
		return B_TRUE;
	}

	return B_FALSE;
}

/*
 * Create a single "Solaris2" boot partition
 * FIXME: Remove dependency on GNU libparted
 */
boolean_t
create_root_partition (char *disk)
{
	char path[PATH_MAX];
	PedDevice *pdev;
	PedDisk *pdisk;
	const PedDiskType *pdisk_type;
	PedPartition *ppart;
	const PedFileSystemType *pfs_type;

#ifdef sparc
	(void) sprintf (path, "%ss2", disk);
#else
	(void) sprintf (path, "%sp0", disk);
#endif

	if ((pdev = ped_device_get (path)) == NULL)
	{
		fprintf (stderr, "Error: Unable to get device handle\n");
		return B_FALSE;
	}

	if ((pdisk_type = ped_disk_type_get ("msdos")) == NULL)
	{
		fprintf (stderr, "Error: Unable to get disk type handle\n");
		return B_FALSE;
	}

	if ((pdisk = ped_disk_new_fresh (pdev, pdisk_type)) == NULL)
	{
		fprintf (stderr, "Error: Unable to get disk handle\n");
		return B_FALSE;
	}

	if ((pfs_type = ped_file_system_type_get ("solaris")) == NULL)
	{
		fprintf (stderr, "Error: Unable to get fs type handle\n");
		return B_FALSE;
	}

	if ((ppart = ped_partition_new (pdisk, PED_PARTITION_NORMAL, pfs_type, 0, pdev->length - 1)) == NULL)
	{
		fprintf (stderr, "Error: Unable to get partition handle\n");
		return B_FALSE;
	}

	if (ped_partition_set_flag (ppart, PED_PARTITION_BOOT, 1) == 0)
	{
		fprintf (stderr, "Error: Unable to set partition as active\n");
		return B_FALSE;
	}

	if (ped_disk_add_partition (pdisk, ppart, ped_device_get_constraint (pdev)) == 0)
	{
		fprintf (stderr, "Error: Unable to add parition to disk\n");
		return B_FALSE;
	}

	if (ped_disk_commit_to_dev (pdisk) == 0)
	{
		fprintf (stderr, "Error: Unable to commit changes to disk\n");
		return B_FALSE;
	}

	return B_TRUE;
}

/*
 * Create the slices needed for a ZFS root filesystem
 */
boolean_t
create_root_vtoc (char *disk)
{
	int i, fd;
	char path[PATH_MAX];
	struct extvtoc vtoc;
	struct dk_geom geo;
	uint16_t cylinder_size;
	uint32_t disk_size;

#ifdef sparc
	(void) sprintf (path, "%ss2", disk);
#else
	(void) sprintf (path, "%sp0", disk);
#endif

	if ((fd = open (path, O_RDWR)) == -1)
	{
		perror ("Error: Unable to open disk for VTOC changes");
		return B_FALSE;
	}

	if (ioctl (fd, DKIOCGGEOM, &geo) == -1)
	{
		perror ("Error: Unable to read disk geometry");
		(void) close (fd);
		return B_FALSE;
	}

	cylinder_size = geo.dkg_nhead * geo.dkg_nsect;
	disk_size = geo.dkg_ncyl * geo.dkg_nhead * geo.dkg_nsect;

	if (!read_extvtoc (fd, &vtoc))
	{
		fprintf (stderr, "Error: Unable to read VTOC from disk\n");
		(void) close (fd);
		return B_FALSE;
	}

	for (i = 0; i < V_NUMPAR; i++)
	{
		switch (i)
		{
			case 0:
				vtoc.v_part[i].p_tag = V_ROOT;
				vtoc.v_part[i].p_flag = 0;
				vtoc.v_part[i].p_start = cylinder_size;
				vtoc.v_part[i].p_size = disk_size - cylinder_size;
				break;
			case 2:
				vtoc.v_part[i].p_tag = V_BACKUP;
				vtoc.v_part[i].p_flag = V_UNMNT;
				vtoc.v_part[i].p_start = 0;
				vtoc.v_part[i].p_size = disk_size;
				break;
			case 8:
				vtoc.v_part[i].p_tag = V_BOOT;
				vtoc.v_part[i].p_flag = V_UNMNT;
				vtoc.v_part[i].p_start = 0;
				vtoc.v_part[i].p_size = cylinder_size;
				break;
			default:
				vtoc.v_part[i].p_tag = V_UNASSIGNED;
				vtoc.v_part[i].p_flag = 0;
				vtoc.v_part[i].p_start = 0;
				vtoc.v_part[i].p_size = 0;
				break;
		}
	}

	if (write_extvtoc (fd, &vtoc) < 0)
	{
		fprintf (stderr, "Error: Unable to write VTOC to disk\n");
		(void) close (fd);
		return B_FALSE;
	}

	(void) close (fd);
	return B_TRUE;
}

#define ROOT_NAME "schillix"

/*
 * Create root ZFS pool on first slice (s0)
 */
boolean_t
create_root_pool (libzfs_handle_t *libzfs_handle, char *disk, char *rpool)
{
	char path[PATH_MAX];
	nvlist_t *vdev, *nvroot, *props, *fsprops;
#ifdef ZPOOL_CREATE_ALTROOT_BUG
	zfs_handle_t *zfs_handle;
#endif

	/*
	 * Create the vdev which is just an nvlist
	 */
	if (nvlist_alloc (&vdev, NV_UNIQUE_NAME, 0) != 0)
	{
		fprintf (stderr, "Error: Unable to allocate vdev\n");
		return B_FALSE;
	}

	(void) sprintf (path, "%ss0", disk);

	if (nvlist_add_string(vdev, ZPOOL_CONFIG_PATH, path) != 0)
	{
		fprintf (stderr, "Error: Unable to set vdev path\n");
		(void) nvlist_free (vdev);
		return B_FALSE;
	}

	if (nvlist_add_string(vdev, ZPOOL_CONFIG_TYPE, VDEV_TYPE_DISK) != 0)
	{
		fprintf (stderr, "Error: Unable to set vdev type\n");
		(void) nvlist_free (vdev);
		return B_FALSE;
	}

	/*
	 * Create the nvroot which is the list of all vdevs
	 * TODO: Add support for mirrored pools?
	 */
	if (nvlist_alloc (&nvroot, NV_UNIQUE_NAME, 0) != 0)
	{
		fprintf (stderr, "Error: Unable to allocate vdev list\n");
		(void) nvlist_free (vdev);
		return B_FALSE;
	}

	if (nvlist_add_string(nvroot, ZPOOL_CONFIG_TYPE, VDEV_TYPE_ROOT) != 0)
	{
		fprintf (stderr, "Error: Unable to set vdev list type\n");
		(void) nvlist_free (vdev);
		(void) nvlist_free (nvroot);
		return B_FALSE;
	}

	if (nvlist_add_nvlist_array(nvroot, ZPOOL_CONFIG_CHILDREN, &vdev, 1) != 0)
	{
		fprintf (stderr, "Error: Unable to add vdev to list\n");
		(void) nvlist_free (vdev);
		(void) nvlist_free (nvroot);
		return B_FALSE;
	}

	/*
	 * Create the root zpool (rpool/syspool/whatever)
	 */
	if (nvlist_alloc (&props, NV_UNIQUE_NAME, 0) != 0)
	{
		fprintf (stderr, "Error: Unable to allocate prop list\n");
		(void) nvlist_free (vdev);
		(void) nvlist_free (nvroot);
		return B_FALSE;
	}

	if (nvlist_add_string (props, zpool_prop_to_name (ZPOOL_PROP_ALTROOT), "/mnt") != 0)
	{
		fprintf (stderr, "Error: Unable to set root mountpoint\n");
		(void) nvlist_free (vdev);
		(void) nvlist_free (nvroot);
		(void) nvlist_free (props);
		return B_FALSE;
	}

	if (nvlist_alloc (&fsprops, NV_UNIQUE_NAME, 0) != 0)
	{
		fprintf (stderr, "Error: Unable to allocate fsprop list\n");
		(void) nvlist_free (vdev);
		(void) nvlist_free (nvroot);
		(void) nvlist_free (props);
		return B_FALSE;
	}

	(void) sprintf (path, "/%s", rpool);

	if (nvlist_add_string (fsprops, zfs_prop_to_name (ZFS_PROP_MOUNTPOINT), path) != 0)
	{
		fprintf (stderr, "Error: Unable to set root mountpoint\n");
		(void) nvlist_free (vdev);
		(void) nvlist_free (nvroot);
		(void) nvlist_free (props);
		(void) nvlist_free (fsprops);
		return B_FALSE;
	}

	if (zpool_create (libzfs_handle, rpool, nvroot, props, fsprops) == -1)
	{
		fprintf (stderr, "Error: Unable to create rpool\n");
		(void) nvlist_free (vdev);
		(void) nvlist_free (nvroot);
		(void) nvlist_free (props);
		(void) nvlist_free (fsprops);
		return B_FALSE;
	}

#ifdef ZPOOL_CREATE_ALTROOT_BUG
	/*
	 * Workaround a bug in libzfs which causes the root dataset to not inherit the altroot on creation.
	 */
	if ((zfs_handle = zfs_path_to_zhandle (libzfs_handle, rpool, ZFS_TYPE_DATASET)) == NULL)
	{
		fprintf (stderr, "Error: Unable to get zfs handle\n");
		(void) nvlist_free (vdev);
		(void) nvlist_free (nvroot);
		(void) nvlist_free (props);
		(void) nvlist_free (fsprops);
		return B_FALSE;
	}

	if (zfs_prop_set (zfs_handle, zfs_prop_to_name (ZFS_PROP_MOUNTPOINT), path) == -1)
	{
		fprintf (stderr, "Error: Unable to set root mountpoint\n");
		(void) nvlist_free (vdev);
		(void) nvlist_free (nvroot);
		(void) nvlist_free (props);
		(void) nvlist_free (fsprops);
		return B_FALSE;
	}
#endif

	(void) nvlist_free (vdev);
	(void) nvlist_free (nvroot);
	(void) nvlist_free (props);
	(void) nvlist_free (fsprops);
	return B_TRUE;
}

/*
 * Export ZFS root pool
 */
boolean_t
export_root_pool (libzfs_handle_t *libzfs_handle, char *rpool)
{
	zpool_handle_t *zpool_handle;

	if ((zpool_handle = zpool_open (libzfs_handle, rpool)) == NULL)
	{
		fprintf (stderr, "Error: Unable to open rpool\n");
		return B_FALSE;
	}

	if (zpool_export (zpool_handle, B_FALSE, NULL) == -1)
	{
		fprintf (stderr, "Error: Unable to unmount rpool\n");
		return B_FALSE;
	}

	return B_TRUE;
}

/*
 * Create root ZFS filesystem on first slice (s0)
 */
boolean_t
create_root_datasets (libzfs_handle_t *libzfs_handle, char *rpool)
{
	char path[PATH_MAX];
	nvlist_t *fsprops;

	/*
	 * Create the /ROOT dataset which holds all of the different roots
	 */
	if (nvlist_alloc (&fsprops, NV_UNIQUE_NAME, 0) != 0)
	{
		fprintf (stderr, "Error: Unable to allocate fsprop list\n");
		(void) nvlist_free (fsprops);
		return B_FALSE;
	}

	if (nvlist_add_string (fsprops, zfs_prop_to_name (ZFS_PROP_MOUNTPOINT), ZFS_MOUNTPOINT_LEGACY) != 0)
	{
		fprintf (stderr, "Error: Unable to set root mountpoint\n");
		(void) nvlist_free (fsprops);
		return B_FALSE;
	}

	(void) sprintf (path, "%s/ROOT", rpool);

	if (zfs_create (libzfs_handle, path, ZFS_TYPE_DATASET, fsprops) != 0)
	{
		fprintf (stderr, "Error: Unable to create root datatset\n");
		(void) nvlist_free (fsprops);
		return B_FALSE;
	}

	/*
	 * Create the actual root filesystem /ROOT/schillix
	 */
	if (nvlist_add_string (fsprops, zfs_prop_to_name (ZFS_PROP_MOUNTPOINT), "/") != 0)
	{
		fprintf (stderr, "Error: Unable to set fsroot mountpoint\n");
		(void) nvlist_free (fsprops);
		return B_FALSE;
	}

	(void) sprintf (path, "%s/ROOT/" ROOT_NAME, rpool);

	if (zfs_create (libzfs_handle, path, ZFS_TYPE_DATASET, fsprops) != 0)
	{
		fprintf (stderr, "Error: Unable to create rootfs datatset\n");
		(void) nvlist_free (fsprops);
		return B_FALSE;
	}

	/*
	 * Store user data on a seperate globally accessible dataset
	 */
	if (nvlist_add_string (fsprops, zfs_prop_to_name (ZFS_PROP_MOUNTPOINT), "/export") != 0)
	{
		fprintf (stderr, "Error: Unable to set export mountpoint\n");
		(void) nvlist_free (fsprops);
		return B_FALSE;
	}

	(void) sprintf (path, "%s/export", rpool);

	if (zfs_create (libzfs_handle, path, ZFS_TYPE_DATASET, fsprops) != 0)
	{
		fprintf (stderr, "Error: Unable to create export dataset\n");
		(void) nvlist_free (fsprops);
		return B_FALSE;
	}

	if (nvlist_add_string (fsprops, zfs_prop_to_name (ZFS_PROP_MOUNTPOINT), "/export/home") != 0)
	{
		fprintf (stderr, "Error: Unable to set home mountpoint\n");
		(void) nvlist_free (fsprops);
		return B_FALSE;
	}

	(void) sprintf (path, "%s/export/home", rpool);

	if (zfs_create(libzfs_handle, path, ZFS_TYPE_DATASET, NULL) != 0)
	{
		fprintf (stderr, "Error: Unable to create home dataset\n");
		(void) nvlist_free (fsprops);
		return B_FALSE;
	}

	if (nvlist_add_string (fsprops, zfs_prop_to_name (ZFS_PROP_MOUNTPOINT), "/export/home/schillix") != 0)
	{
		fprintf (stderr, "Error: Unable to set schillix mountpoint\n");
		(void) nvlist_free (fsprops);
		return B_FALSE;
	}

	(void) sprintf (path, "%s/export/home/schillix", rpool);

	if (zfs_create(libzfs_handle, path, ZFS_TYPE_DATASET, NULL) != 0)
	{
		fprintf (stderr, "Error: Unable to create schillix dataset\n");
		(void) nvlist_free (fsprops);
		return B_FALSE;
	}

	(void) nvlist_free (fsprops);
	return B_TRUE;
}

/*
 * Set bootfs property on rpool
 */
boolean_t
set_root_bootfs (libzfs_handle_t *libzfs_handle, char *rpool)
{
	char path[PATH_MAX];
	zpool_handle_t *zpool_handle;

	if ((zpool_handle = zpool_open (libzfs_handle, rpool)) == NULL)
	{
		fprintf (stderr, "Error: Unable to open rpool\n");
		zpool_close (zpool_handle);
		return B_FALSE;
	}

	(void) sprintf (path, "%s/ROOT/" ROOT_NAME, rpool);

	if (zpool_set_prop (zpool_handle, "bootfs", path) == -1)
	{
		fprintf (stderr, "Error: Unable to set rpool bootfs\n");
		zpool_close (zpool_handle);
		return B_FALSE;
	}

	zpool_close (zpool_handle);
	return B_TRUE;
}

/*
 * Recursively mount datasets on new rpool
 */
boolean_t
mount_root_datasets (libzfs_handle_t *libzfs_handle, char *rpool)
{
	zpool_handle_t *zpool_handle;

	if ((zpool_handle = zpool_open (libzfs_handle, rpool)) == NULL)
	{
		fprintf (stderr, "Error: Unable to open rpool\n");
		zpool_close (zpool_handle);
		return B_FALSE;
	}

	if (zpool_enable_datasets (zpool_handle, NULL, 0) == -1)
	{
		fprintf (stderr, "Error: Unable to mount rpool\n");
		zpool_close (zpool_handle);
		return B_FALSE;
	}

	zpool_close (zpool_handle);
	return B_TRUE;
}

/*
 * Recursively unmount datasets on rpool
 */
boolean_t
unmount_root_datasets (libzfs_handle_t *libzfs_handle, char *rpool)
{
	zpool_handle_t *zpool_handle;

	if ((zpool_handle = zpool_open (libzfs_handle, rpool)) == NULL)
	{
		fprintf (stderr, "Error: Unable to open rpool\n");
		zpool_close (zpool_handle);
		return B_FALSE;
	}

	if (zpool_disable_datasets (zpool_handle, B_TRUE) == -1)
	{
		fprintf (stderr, "Error: Unable to unmount rpool\n");
		zpool_close (zpool_handle);
		return B_FALSE;
	}

	zpool_close (zpool_handle);
	return B_TRUE;
}

