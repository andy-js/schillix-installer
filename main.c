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
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <parted/parted.h>
#include <sys/dkio.h>
#include <sys/vtoc.h>

char program_name[] = "schillix-install";

/*
 * Determine which disk schillix is being installed onto
 */
char *
get_disk (void)
{
	int i, d, len, nodisks = 0;
	PedDevice *pdev;
	char *ret, *devname, **buf, **disks = NULL;

	if ((pdev = ped_device_get_next (NULL)) == NULL)
		ped_device_probe_all ();

	/*
	 * Build a list of all suitable disks
	 */
	while ((pdev = ped_device_get_next (pdev)) != NULL 
	    && (devname = strrchr (pdev->path, '/')) != '\0'
	    && (len = strlen (++devname)) > 2)
	{
		if ((buf = (char **)realloc ((void *)disks, sizeof (char *) * (nodisks + 1))) == NULL)
		{
			perror ("Unable to allocate memory");
			for (i = 0; i < nodisks; i++)
				free (disks[i]);
			free (disks);
			return NULL;
		}

		disks = buf;

		if ((disks[nodisks] = (char *)malloc (sizeof (char) * len - 2)) == NULL)
		{
			perror ("Unable to allocate memory");
			for (i = 0; i < nodisks; i++)
				free (disks[i]);
			free (disks);
			return NULL;
		}

		strncpy (disks[nodisks], devname, len - 2)[len - 2] = '\0';
		nodisks++;
	}

	if (nodisks > 0)
	{
		/*
		 * Present the user with a list of suitable disks and return their chosen one
		 */
		printf ("Please choose a disk to install schillix on:\n");
		for (i = 0; i < nodisks; i++)
			printf ("[%d] %s\n", i, disks[i]);
		printf ("Enter choice: ");
		while ((scanf ("%d", &d) == 0 && fflush (stdin) < 1) || (d < 0 || d >= nodisks))
			printf ("\rEnter choice: ");
		(void) fflush(stdin);

		for (i = 0; i < nodisks; i++)
			if (i == d)
				ret = disks[i];
			else
				free (disks[i]);

		free (disks);
		return ret;
	}

	fprintf (stderr, "No suitable disks found\n");
	return NULL;
}

/*
 * Prepare the disk so that a schillix filesystem can be created on it
 */
int
format_disk (char *disk)
{
	int fd;
	char c, path[PATH_MAX];
	PedDevice *pdev;
	PedDisk *pdisk;
	const PedDiskType *pdisk_type;
	PedPartition *ppart;
	const PedFileSystemType *pfs_type;
	struct extvtoc vtoc;
	struct dk_geom geo;
	uint16_t cylinder_size;
	uint32_t disk_size;

	/*
	 * Warn the user before touching the disk
	 */
	printf ("All data on %s will be destroyed.  Continue? [yn] ", disk);
	while (scanf ("%c", &c) == 0 || (c != 'y' && c != 'n'))
		printf ("\rContinue? [yn] ");

	if (c == 'n')
	{
		fprintf (stderr, "User aborted format\n");
		return 0;
	}

	/*
	 * Use libparted to set up single "Solaris2" boot partition
	 */
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

	/*
	 * Create root slice for ZFS root filesystem
	 */
	if ((fd = open (pdev->path, O_RDWR)) == -1)
	{
		perror ("Unable to open disk for VTOC changes");
		(void) close (fd);
		return 0;
	}

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

	(void) close (fd);
	return 1;
}

int
main (int argc, char **argv)
{
	char *disk;

	if ((disk = get_disk ()) == NULL)
		return EXIT_FAILURE;

	if (format_disk (disk) == 0)
		return EXIT_FAILURE;

	free(disk);
	return EXIT_SUCCESS;
}
