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
#include <parted/parted.h>
#include <sys/dkio.h>
#include <sys/vtoc.h>

#define DISK_PATH "/dev/rdsk"

/*
 * Return a list of all suitable disks
 */
char **
get_suitable_disks (void)
{
	DIR *dir;
	struct dirent *dp;
	int i, fd, len, nodisks = 0;
	char path[PATH_MAX], **buf, **disks = NULL;

	if ((dir = opendir (DISK_PATH)) == NULL)
	{
		perror ("Unable to open " DISK_PATH);
		return NULL;
	}

	while ((dp = readdir (dir)) != NULL)
	{
		/*
		 * We're looking for the whole disk which is *s2 on sparc and *p0 on x86
		 */
		if ((dp->d_name[0] != 'c' || (len = strlen (dp->d_name)) < 2) ||
#ifdef sparc
		    (dp->d_name[len - 2] != 's' || dp->d_name[len - 1] != '2'))
#else
		    (dp->d_name[len - 2] != 'p' || dp->d_name[len - 1] != '0'))
#endif
			continue;

		(void) sprintf (path, DISK_PATH "/%s", dp->d_name);

		if ((fd = open (path, O_RDONLY)) == -1)
		{
			/*
			 * If the devlink doesn't actually go anywhere we ignore it
			 */
			if (errno != ENOENT && errno != ENXIO)
				fprintf (stderr, "Unable to probe disk %s: %s\n",
				    dp->d_name, strerror (errno));
			continue;
		}

		(void) close (fd);

		/*
		 * Allocate enough memory for the next disk and the NULL terminator
		 */
		if ((buf = (char **)realloc ((void *)disks, sizeof (char *) * (nodisks + 2))) == NULL)
			goto nomem;

		disks = buf;

		/*
		 * Copy the device name but drop the whole-device suffix (s2/p0)
		 */
		if ((disks[nodisks] = malloc (sizeof (char) * (len - 2))) == NULL)
			goto nomem;

		strncpy (disks[nodisks++], dp->d_name, len - 2);
	}

	(void) closedir (dir);

	disks[nodisks] = NULL;
	return disks;

nomem:
	perror ("Unable to allocate memory");

	for (i = 0; i < nodisks; i++)
		free (disks[i]);
	free (disks);

	(void) closedir (dir);
	return NULL;
}

int
open_disk (char *disk, int mode)
{
	char path[PATH_MAX];

#ifdef sparc
	(void) sprintf (path, "/dev/rdsk/%ss2", disk);
#else
	(void) sprintf (path, "/dev/rdsk/%sp0", disk);
#endif

	return open (path, mode);
}
	

/*
 * Create a single "Solaris2" boot partition
 * FIXME: Remove dependency on GNU libparted
 */
int
create_root_partition (char *disk)
{
	char path[PATH_MAX];
	PedDevice *pdev;
	PedDisk *pdisk;
	const PedDiskType *pdisk_type;
	PedPartition *ppart;
	const PedFileSystemType *pfs_type;

#ifdef sparc
	(void) sprintf (path, "/dev/rdsk/%ss2", disk);
#else
	(void) sprintf (path, "/dev/rdsk/%sp0", disk);
#endif

	if ((pdev = ped_device_get (path)) == NULL)
	{
		fprintf (stderr, "Unable to get device handle\n");
		return -1;
	}

	if ((pdisk_type = ped_disk_type_get ("msdos")) == NULL)
	{
		fprintf (stderr, "Unable to get disk type handle\n");
		return -1;
	}

	if ((pdisk = ped_disk_new_fresh (pdev, pdisk_type)) == NULL)
	{
		fprintf (stderr, "Unable to get disk handle\n");
		return -1;
	}

	if ((pfs_type = ped_file_system_type_get ("solaris")) == NULL)
	{
		fprintf (stderr, "Unable to get fs type handle\n");
		return -1;
	}

	if ((ppart = ped_partition_new (pdisk, PED_PARTITION_NORMAL, pfs_type, 0, pdev->length - 1)) == NULL)
	{
		fprintf (stderr, "Unable to get partition handle\n");
		return -1;
	}

	if (ped_partition_set_flag (ppart, PED_PARTITION_BOOT, 1) == 0)
	{
		fprintf (stderr, "Unable to set partition as active\n");
		return -1;
	}

	if (ped_disk_add_partition (pdisk, ppart, ped_device_get_constraint (pdev)) == 0)
	{
		fprintf (stderr, "Unable to add parition to disk\n");
		return -1;
	}

	if (ped_disk_commit_to_dev (pdisk) == 0)
	{
		fprintf (stderr, "Unable to commit changes to disk\n");
		return -1;
	}

	return 0;
}

/*
 * Create root slice for ZFS root filesystem
 */
int
create_root_slice (int fd)
{
	struct extvtoc vtoc;
	struct dk_geom geo;
	uint16_t cylinder_size;
	uint32_t disk_size;

	if (ioctl (fd, DKIOCGGEOM, &geo) == -1)
	{
		perror ("Unable to read disk geometry");
		(void) close (fd);
		return -1;
	}

	cylinder_size = geo.dkg_nhead * geo.dkg_nsect;
	disk_size = geo.dkg_ncyl * geo.dkg_nhead * geo.dkg_nsect;

	if (!read_extvtoc (fd, &vtoc))
	{
		fprintf (stderr, "Unable to read VTOC from disk\n");
		(void) close (fd);
		return -1;
	}
	
	vtoc.v_part[0].p_tag = V_ROOT;
	vtoc.v_part[0].p_start = cylinder_size;
	vtoc.v_part[0].p_size = disk_size - cylinder_size;

	if (write_extvtoc (fd, &vtoc) < 0)
	{
		fprintf (stderr, "Unable to write VTOC to disk\n");
		(void) close (fd);
		return -1;
	}

	return 0;
}
