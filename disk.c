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
#include <parted/parted.h>
#include <sys/dkio.h>
#include <sys/vtoc.h>

/*
 * Return a list of all suitable disks
 */
char **
get_suitable_disks (void)
{
	int i, len, nodisks = 0;
	char *devname, **buf, **disks = NULL;
	PedDevice *pdev;

	if ((pdev = ped_device_get_next (NULL)) == NULL)
		ped_device_probe_all ();

	while ((pdev = ped_device_get_next (pdev)) != NULL 
	    && (devname = strrchr (pdev->path, '/')) != '\0'
	    && (len = strlen (++devname)) > 2)
	{
		/*
		 * Allocate enough space for the next disk and the NULL terminator
		 */
		if ((buf = (char **)realloc ((void *)disks, sizeof (char *) * (nodisks + 2))) == NULL)
		{
			perror ("Unable to allocate memory");
			for (i = 0; i < nodisks; i++)
				free (disks[i]);
			free (disks);
			return NULL;
		}

		disks = buf;

		/*
		 * Copy the disk device name but leave off the arch specific suffix (s2/p0)
		 */
		if ((disks[nodisks] = (char *)malloc (sizeof (char) * len - 2)) == NULL)
		{
			perror ("Unable to allocate memory");
			for (i = 0; i < nodisks; i++)
				free (disks[i]);
			free (disks);
			return NULL;
		}

		strncpy (disks[nodisks], devname, len - 2)[len - 2] = '\0';
		disks[++nodisks] = NULL;
	}

	return disks;
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
		return 0;
	}

	if ((pdisk_type = ped_disk_type_get ("msdos")) == NULL)
	{
		fprintf (stderr, "Unable to get disk type handle\n");
		return 0;
	}

	if ((pdisk = ped_disk_new_fresh (pdev, pdisk_type)) == NULL)
	{
		fprintf (stderr, "Unable to get disk handle\n");
		return 0;
	}

	if ((pfs_type = ped_file_system_type_get ("solaris")) == NULL)
	{
		fprintf (stderr, "Unable to get fs type handle\n");
		return 0;
	}

	if ((ppart = ped_partition_new (pdisk, PED_PARTITION_NORMAL, pfs_type, 0, pdev->length - 1)) == NULL)
	{
		fprintf (stderr, "Unable to get partition handle\n");
		return 0;
	}

	if (ped_partition_set_flag (ppart, PED_PARTITION_BOOT, 1) == 0)
	{
		fprintf (stderr, "Unable to set partition as active\n");
		return 0;
	}

	if (ped_disk_add_partition (pdisk, ppart, ped_device_get_constraint (pdev)) == 0)
	{
		fprintf (stderr, "Unable to add parition to disk\n");
		return 0;
	}

	if (ped_disk_commit_to_dev (pdisk) == 0)
	{
		fprintf (stderr, "Unable to commit changes to disk\n");
		return 0;
	}

	return 1;
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
		return 0;
	}

	cylinder_size = geo.dkg_nhead * geo.dkg_nsect;
	disk_size = geo.dkg_ncyl * geo.dkg_nhead * geo.dkg_nsect;

	if (!read_extvtoc (fd, &vtoc))
	{
		fprintf (stderr, "Unable to read VTOC from disk\n");
		(void) close (fd);
		return 0;
	}
	
	vtoc.v_part[0].p_tag = V_ROOT;
	vtoc.v_part[0].p_start = cylinder_size;
	vtoc.v_part[0].p_size = disk_size - cylinder_size;

	if (write_extvtoc (fd, &vtoc) < 0)
	{
		fprintf (stderr, "Unable to write VTOC to disk\n");
		(void) close (fd);
		return 0;
	}

	return 1;
}
