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
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <dirent.h>
#include <libzfs.h>

#include "config.h"
#include "disk.h"
#include "copy.h"

char program_name[] = "schillix-install";
char temp_mount[PATH_MAX] = DEFAULT_MNT_POINT;
char cdrom_path[PATH_MAX] = DEFAULT_CDROM_PATH;

/*
 * Print usage and exit
 */
void
usage (int retval)
{
	FILE *out;

	/*
	 * Print to stderr only on error
	 */
	if (retval == 0)
		out = stdout;
	else
		out = stderr;

	/*
	 * Add some space between error messages
	 */
	if (retval != 0)
		fprintf (out, "\n");

	fprintf (out, "Installer for Schillix\n");
	fprintf (out, "(c) Copyright 2013 - Andrew Stormont\n");
	fprintf (out, "\n");
	fprintf (out, "usage: schillix-install [opts] /path/to/disk or devname\n");
	fprintf (out, "\n");
	fprintf (out, "Where opts is:\n");
	fprintf (out, "\t-r name or new rpool (default is " DEFAULT_RPOOL_NAME ")\n");
	fprintf (out, "\t-m temporary mountpoint (default is " DEFAULT_MNT_POINT ")\n");
	fprintf (out, "\t-c path to livecd contents (default is " DEFAULT_CDROM_PATH ")\n");
	fprintf (out, "\t-? print this message and exit\n");

	exit (retval);
}

int
main (int argc, char **argv)
{
	char c, disk[PATH_MAX] = { '\0' }, rpool[ZPOOL_MAXNAMELEN] = DEFAULT_RPOOL_NAME;
	int i;
	DIR *dir;
	libzfs_handle_t *libzfs_handle;

	/*
	 * Parse command line arguments
	 */
	while ((c = getopt (argc, argv, "r:m:c:?")) != -1)
	{
		switch (c)
		{
			case 'r':

				if (strlen (optarg) >= ZPOOL_MAXNAMELEN)
				{
					fprintf (stderr, "Error: rpool name too long\n");
					usage (EXIT_FAILURE);
				}

				strcpy (rpool, optarg);
				break;

			case 'm':

				if (strlen (optarg) >= PATH_MAX)
				{
					fprintf (stderr, "Error: mountpoint path too long\n");
					usage (EXIT_FAILURE);
				}

				strcpy (temp_mount, optarg);
				break;

			case 'c':

				if (strlen (optarg) >= PATH_MAX)
				{
					fprintf (stderr, "Error: livecd path too long\n");
					usage (EXIT_FAILURE);
				}

				strcpy (cdrom_path, optarg);
				break;

			case '?':

				if (optopt == '?')
					usage (EXIT_SUCCESS);
				else
					usage (EXIT_FAILURE);
				break;

			default:
				/*
				 * Ignoring opts is bad!
				 */
				abort();
		}
	}

	for (i = optind; i < argc; i++)
	{
		if (disk[0] == '\0')
		{
			strcpy (disk, argv[i]);
		}
		else
		{
			fprintf (stderr, "Error: Please specify only one disk\n");
			usage (EXIT_FAILURE);
		}
	}

	if (disk[0] == '\0')
	{
		fprintf (stderr, "Error: No disk specified\n");
		usage (EXIT_FAILURE);
	}

	/*
	 * Ensure that the path to the livecd contents is a directory
	 * and that it can be opened.
	 */
	if ((dir = opendir (cdrom_path)) == NULL)
	{
		fprintf (stderr, "Error: unable to open %s: %s\n", cdrom_path, strerror (errno));
		usage (EXIT_FAILURE);
	}

	(void) closedir (dir);

	/*
	 * Get libzfs handle before outputting anything to stdout/stderr
	 * otherwise we won't be able to get it later (seriously)
	 */
	if ((libzfs_handle = libzfs_init ()) == NULL)
	{
		fprintf (stderr, "Error: Unable to get libzfs handle\n");
		return EXIT_FAILURE;
	}

	/*
	 * Warn the user before touching the disk
	 */
	printf ("All data on %s will be destroyed.  Continue? [yn] ", disk);
	while (scanf ("%c", &c) == 0 || (c != 'y' && c != 'n'))
		printf ("\rContinue? [yn] ");

	if (c == 'n')
	{
		fprintf (stderr, "User aborted format\n");
		return B_FALSE;
	}

	if (disk_in_use (libzfs_handle, disk) == B_TRUE)
	{
		fprintf (stderr, "Error: Disk appears to be in use already\n");
		return B_FALSE;
	}

	/*
	 * Reformat disk
	 */
	puts ("Reformatting disk...");

	if (create_root_partition (disk) == B_FALSE)
	{
		fprintf (stderr, "Error: Unable to create boot partition\n");
		return B_FALSE;
	}

	if (create_root_vtoc (disk) == B_FALSE)
	{
		fprintf (stderr, "Error: Unable to create new slices on disk\n");
		return B_FALSE;
	}

	/*
	 * Create new ZFS filesystem
	 */
	puts ("Creating new filesystem...");

	if (create_root_pool (libzfs_handle, disk, rpool) == B_FALSE)
	{
		fprintf (stderr, "Error: Unable to create new rpool\n");
		return B_FALSE;
	}

	if (create_root_datasets (libzfs_handle, rpool) == B_FALSE)
	{
		fprintf (stderr, "Error: Unable to create root datasets\n");
		return B_FALSE;
	}

	/*
	 * Mount new filesystem and copy files
	 */
	puts ("Mounting filesystem...");

	if (mount_root_datasets (libzfs_handle, rpool) == B_FALSE)
	{
		fprintf (stderr, "Error: Unable to mount root filesystem\n");
		return EXIT_FAILURE;
	}

	printf ("Copying files...\n");

	if (copy_files () == B_FALSE)
	{
		fprintf (stderr, "Error: Unable to copy livecd files\n");
		return EXIT_FAILURE;
	}

	(void) libzfs_fini (libzfs_handle);

	return EXIT_SUCCESS;
}
