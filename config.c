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

/*
 * Final steps required to create a bootable system
 * Usage of system(3c) makes me feel dirty - find
 * out how these utilities do what they do and copy it
 */
boolean_t
config_grub (char *mnt, char *disk)
{
	char *cmd;

	if (asprintf (&cmd, "/usr/sbin/installgrub -mf %s/boot/grub/stage1 "
		"%s/boot/grub/stage2 %ss0", mnt, mnt, disk) == -1)
	{
		fprintf (stderr, "Error: out of memory\n");
		return B_FALSE;
	}

	if (system (cmd) != 0)
	{
		fprintf (stderr, "Error: installgrub failed\n");
		free (cmd);
		return B_FALSE;
	}

	free (cmd);
	return B_TRUE;
}

boolean_t
config_devfs (char *mnt)
{
	char *cmd;

	if (asprintf (&cmd, "/usr/sbin/devfsadm -r %s", mnt) == -1)
	{
		fprintf (stderr, "Error: out of memory\n");
		return B_FALSE;
	}

	if (system (cmd) != 0)
	{
		fprintf (stderr, "Error: devfsadm failed\n");
		free (cmd);
		return B_FALSE;
	}

	free (cmd);
	return B_TRUE;
}

boolean_t
config_bootadm (char *mnt)
{
	char *cmd;

	if (asprintf (&cmd, "/usr/sbin/bootadm update-archive -R %s\n", mnt) == -1)
	{
		fprintf (stderr, "Error: out of memory\n");
		return B_FALSE;
	}

	if (system (cmd) != 0)
	{
		fprintf (stderr, "Error: bootadm failed\n");
		free (cmd);
		return B_FALSE;
	}

	free (cmd);
	return B_TRUE;
}

