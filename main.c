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
#include <libzfs.h>

#include "disk.h"

char program_name[] = "schillix-install";
libzfs_handle_t *libzfs_handle = NULL;

/*
 * Determine which disk schillix is being installed onto
 */
char *
get_disk (void)
{
	int i = 0, d = 0, nodisks = 0;
	char *ret = NULL, **disks = NULL;

	if ((disks = get_suitable_disks ()) == NULL)
	{
		fprintf (stderr, "No suitable disks found\n");
		return NULL;
	}

	/*
	 * Present the user with a list of suitable disks and get their chosen one
	 */
	printf ("Please choose a disk to install schillix on:\n");
	for (nodisks = 0; disks[nodisks] != NULL; nodisks++)
		printf ("[%d] %s\n", nodisks, disks[nodisks]);
	printf ("Enter choice: ");
	while ((scanf ("%d", &d) == 0 && fflush (stdin) < 1) || (d < 0 || d >= nodisks))
		printf ("\rEnter choice: ");
	(void) fflush (stdin);

	/*
	 * Free all other device references and return the one we're using
	 */
	for (i = 0; i < nodisks; i++)
		if (i == d)
			ret = disks[i];
		else
			free(disks[i]);
	free(disks);
	return ret;
}

/*
 * Prepare the disk so that a schillix filesystem can be created on it
 */
int
format_disk (char *disk)
{
	char c;

	/*
	 * Warn the user before touching the disk
	 */
	printf ("All data on %s will be destroyed.  Continue? [yn] ", disk);
	while (scanf ("%c", &c) == 0 || (c != 'y' && c != 'n'))
		printf ("\rContinue? [yn] ");

	if (c == 'n')
	{
		fprintf (stderr, "User aborted format\n");
		return -1;
	}

	if (create_root_partition (disk) == -1)
	{
		fprintf (stderr, "Unable to create schillix boot partition\n");
		return -1;
	}

	if (create_root_vtoc (disk) == -1)
	{
		fprintf (stderr, "Unable to create new slices on disk\n");
		return -1;
	}

	if (create_root_filesystem (disk) == -1)
	{
		fprintf (stderr, "Unable to create root filesystem on disk\n");
		return -1;
	}

	return 0;
}

int
main (int argc, char **argv)
{
	char *disk;

	/*
	 * XXX: libzfs_init won't work unless it's called early on
	 * Find out why so we can get rid of this nasty global
	 */
	if ((libzfs_handle = libzfs_init ()) == NULL)
	{
		fprintf (stderr, "Unable to get libzfs handle\n");
		return EXIT_FAILURE;
	}

	if ((disk = get_disk ()) == NULL)
	{
		fprintf (stderr, "Unable to find suitable disk for install\n");
		return EXIT_FAILURE;
	}

	if (format_disk (disk) == -1)
	{
		fprintf (stderr, "Unable to complete disk format\n");
		return EXIT_FAILURE;
	}

	free(disk);
	return EXIT_SUCCESS;
}
