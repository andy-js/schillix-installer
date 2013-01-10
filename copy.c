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
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <sys/sendfile.h>

extern char temp_mount[PATH_MAX];
extern char cdrom_path[PATH_MAX];

#define ROOT_USER	0
#define STAFF_GROUP	10

/*
 * Copy a file to a new destination
 */
static boolean_t
copy_file (const char *path, const char *dest, const struct stat *statptr)
{
	int in_fd, out_fd;
	struct stat in_stat;
	off_t offset = 0;

	/*
	 * Stat the file if the caller hasn't
	 */
	if (statptr == NULL)
	{
		if (stat (path, &in_stat) == -1)
		{
			fprintf (stderr, "Unable to stat file %s: %s\n", path, strerror (errno));
			return B_FALSE;
		}
	}
	else
		in_stat = *statptr;

	/*
	 * Open the files
	 */
	if ((in_fd = open (path, O_RDONLY)) == -1)
	{
		fprintf (stderr, "Unable to open file %s: %s\n", path, strerror (errno));
		return B_FALSE;
	}

	if ((out_fd = creat (dest, in_stat.st_mode)) == -1)
	{
		if (errno == EEXIST)
		{
			if (unlink (dest) == -1)
			{
				fprintf (stderr, "Unable to remove file %s: %s\n", dest, strerror (errno));
				(void) close (in_fd);
				return B_FALSE;
			}

			if ((out_fd = creat (dest, in_stat.st_mode)) == -1)
			{
				fprintf (stderr, "Unable to recreate file %s: %s\n", dest, strerror (errno));
				(void) close (in_fd);
				return B_FALSE;
			}
		}
		else
		{
			fprintf (stderr, "Unable to create file %s: %s\n", dest, strerror (errno));
			(void) close (in_fd);
			return B_FALSE;
		}
	}

	/*
	 * Copy ownership
	 */
	if (chown (dest, in_stat.st_uid, in_stat.st_gid) == -1)
	{
		fprintf (stderr, "Unable to chown file %s: %s\n", dest, strerror (errno));
		(void) close (in_fd);
		(void) close (out_fd);
		return B_FALSE;
	}

	/*
	 * Copy contents over
	 */
	if (sendfile (out_fd, in_fd, &offset, in_stat.st_size) == -1)
	{
		fprintf (stderr, "Unable to copy file %s: %s\n", path, strerror (errno));
		(void) close (in_fd);
		(void) close (out_fd);
		return B_FALSE;
	}

	(void) close (in_fd);
	(void) close (out_fd);
	return B_TRUE;
}

/*
 * Install a file/directory/symlink.  Called by copy_files
 */
static int
process_path (const char *path, const struct stat *statptr, int fileflag, struct FTW *pftw)
{
	int read;
	char base[PATH_MAX], dest[PATH_MAX], target[PATH_MAX];

	/*
	 * Work out where we're copying to... again
	 */
	if (realpath (cdrom_path, base) == NULL)
	{
		perror ("Unable to resolve cdrom path");
		return 1;
	}

	switch (fileflag)
	{
		case FTW_F:

			/*
			 * Create new file and copy permissions
			 */
			(void) sprintf (dest, "%s/%s", temp_mount, path + strlen (base));

			if (copy_file (path, dest, statptr) == B_FALSE)
			{
				fprintf (stderr, "Unable to copy %s\n", path);
				return 1;
			}

			break;

		case FTW_D:

			/*
			 * Create new directory and copy permissions
			 */
			(void) sprintf (dest, "%s/%s", temp_mount, path + strlen (base));

			if (mkdir (dest, statptr->st_mode) == -1)
			{
				/*
				 * If the directory exists just copy permissions as it might be a mountpoint
				 */
				if (errno == EEXIST)
				{
					/*
					 * But not on the parent directory/root mountpoint
					 */
					if (pftw->level == 0)
						return 0;

					if (chmod (dest, statptr->st_mode) == -1)
					{
						fprintf (stderr, "Unable to chmod directory %s: %s\n", dest, strerror (errno));
						return 1;
					}
				}
				else
				{
					fprintf (stderr, "Unable to create directory %s: %s\n", dest, strerror (errno));
					return 1;
				}
			}

			if (chown (dest, statptr->st_uid, statptr->st_gid) == -1)
			{
				fprintf (stderr, "Unable to chown directory %s: %s\n", dest, strerror (errno));
				return 1;
			}

			break;

		case FTW_SL:

			/*
			 * Replicate symlink
			 */
			(void) sprintf (dest, "%s/%s", temp_mount, path + strlen (base));

			if ((read = readlink (path, target, PATH_MAX)) == -1)
			{
				fprintf (stderr, "Unable to read symlink %s: %s\n", path, strerror (errno));
				return 1;
			}

			target[read] = '\0';

			if (symlink(target, dest) == -1)
			{
				/*
				 * If the symlink exists recreat it
				 */
				if (errno == EEXIST)
				{
					if (unlink (dest) == -1)
					{
						fprintf (stderr, "Unable to remove symlink %s: %s\n", dest, strerror (errno));
						return 1;
					}

					if (symlink (target, dest) == -1)
					{
						fprintf (stderr, "Unable to recreate symlink %s: %s\n", dest, strerror (errno));
						return 1;
					}
				}
				else
				{
					fprintf (stderr, "Unable to replicate symlink %s: %s\n", path, strerror (errno));
					return 1;
				}
			}

			break;

		default:
			/*
			 * Ignoring an error might result in an unbootable system!
			 */
			abort();
	}

	return 0;
}

/*
 * Copy livecd files to new root fs
 */
boolean_t
copy_files (void)
{
	char path[PATH_MAX];

	if (realpath(cdrom_path, path) == NULL)
	{
		perror ("Unable to resolve cdrom path");
		return B_FALSE;
	}

	if (nftw(path, &process_path, 0, FTW_PHYS) != 0)
	{
		fprintf (stderr, "Unable to traverse directory: %s\n", path);
		return B_FALSE;
	}

	return B_TRUE;
}

/*
 * Copy grub files to rpool
 */
boolean_t
copy_grub (char *mnt, char *rpool)
{
	char dest[PATH_MAX], path[PATH_MAX];
	mode_t mode = 0;

	/*
	 * 0755. U = RWX, G = RX, A = X
	 */
	mode |= S_IRUSR | S_IWUSR | S_IXUSR;
	mode |= S_IRGRP | S_IXGRP;
	mode |= S_IROTH |S_IXOTH;

	/*
	 * ZFS boot pools have one global boot directory
	 */
	(void) sprintf (dest, "%s/%s/boot", mnt, rpool); 

	if (mkdir (dest, mode) == -1)
	{
		perror ("Unable to create boot directory");
		return B_FALSE;
	}

	if (chown (dest, ROOT_USER, STAFF_GROUP) == -1)
	{
		perror ("Unable to chown boot directory");
		return B_FALSE;
	}

	/*
	 * Create grub directory
	 */
	(void) sprintf (dest, "%s/%s/boot/grub", mnt, rpool);

	if (mkdir (dest, mode) == -1)
	{
		perror ("Unable to create grub directory");
		return B_FALSE;
	}

	if (chown (dest, ROOT_USER, STAFF_GROUP) == -1)
	{
		perror ("Unable to chown grub directory");
		return B_FALSE;
	}

	/*
	 * Copy /grub/capability
	 */
	(void) sprintf (path, "%s/boot/grub/capability", mnt);
	(void) sprintf (dest, "%s/%s/boot/grub/capability", mnt, rpool);

	if (copy_file (path, dest, NULL) == B_FALSE)
	{
		fprintf (stderr, "Unable to copy %s\n", path);
		return B_FALSE;
	}

	/*
	 * Copy /grub/menu.lst
	 */
	(void) sprintf (path, "%s/boot/grub/menu.lst", mnt);
	(void) sprintf (dest, "%s/%s/boot/grub/menu.lst", mnt, rpool);

	if (copy_file (path, dest, NULL) == B_FALSE)
	{
		fprintf (stderr, "Unable to copy %s\n", path);
		return B_FALSE;
	}

	/*
	 * Copy /grub/splash.xpm.gz
	 */
	(void) sprintf (path, "%s/boot/grub/splash.xpm.gz", mnt);
	(void) sprintf (dest, "%s/%s/boot/grub/splash.xpm.gz", mnt, rpool);

	if (copy_file (path, dest, NULL) == B_FALSE)
	{
		fprintf (stderr, "Unable to copy %s\n", path);
		return B_FALSE;
	}

	return B_TRUE;
}

