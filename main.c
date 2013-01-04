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
#include <dirent.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>

#define DISK_PATH "/dev/rdsk"

/*
 * Determine which disk schillix is being installed onto
 */
char *
get_disk (void)
{
	DIR *dir;
	struct dirent *dp;
	int i, d, fd, len, nodisks = 0;
	char path[PATH_MAX], *ret, **buf, **disks = NULL;

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

		if ((buf = (char **)realloc ((void *)disks, sizeof (char *) * (nodisks + 1))) == NULL)
		{
			perror ("Unable to allocate memory\n");
			for (i = 0; i < nodisks; i++)
				free (disks[i]);
			free (disks);
			return NULL;
		}

		disks = buf;

		if ((disks[nodisks] = strdup (dp->d_name)) == NULL)
		{
			perror ("Unable to allocate memory\n");
			for (i = 0; i < nodisks; i++)
				free (disks[i]);
			free (disks);
			return NULL;
		}

		nodisks++;
	}

	(void) closedir(dir);

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
	char c;

	printf ("All data on %s will be destroyed.  Continue? [yn] ", disk);
	while (scanf ("%c", &c) == 0 || (c != 'y' && c != 'n'))
		printf ("\rContinue? [yn] ");

	if (c == 'n')
	{
		printf ("User aborted format\n");
		return 0;
	}

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
