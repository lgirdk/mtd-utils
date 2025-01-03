/* Ported to MTD system.
 * Based on:
 */
/*======================================================================

  Utility to create an FTL partition in a memory region

  ftl_check.c 1.10 1999/10/25 20:01:35

  The contents of this file are subject to the Mozilla Public
  License Version 1.1 (the "License"); you may not use this file
  except in compliance with the License. You may obtain a copy of
  the License at http://www.mozilla.org/MPL/

  Software distributed under the License is distributed on an "AS
  IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
  implied. See the License for the specific language governing
  rights and limitations under the License.

  The initial developer of the original code is David A. Hinds
  <dhinds@pcmcia.sourceforge.org>.  Portions created by David A. Hinds
  are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.

  Alternatively, the contents of this file may be used under the
  terms of the GNU Public License version 2 (the "GPL"), in which
  case the provisions of the GPL are applicable instead of the
  above.  If you wish to allow the use of your version of this file
  only under the terms of the GPL and not to allow others to use
  your version of this file under the MPL, indicate your decision
  by deleting the provisions above and replace them with the notice
  and other provisions required by the GPL.  If you do not delete
  the provisions above, a recipient may use your version of this
  file under either the MPL or the GPL.

  ======================================================================*/

#define PROGRAM_NAME "ftl_check"

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <getopt.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include <mtd/mtd-user.h>
#include <mtd/ftl-user.h>
#include <mtd_swab.h>

#include "common.h"

/*====================================================================*/

static void print_size(u_int s)
{
	if ((s > 0x100000) && ((s % 0x100000) == 0))
		printf("%d mb", s / 0x100000);
	else if ((s > 0x400) && ((s % 0x400) == 0))
		printf("%d kb", s / 0x400);
	else
		printf("%d bytes", s);
}

/*====================================================================*/

static void check_partition(int fd)
{
	mtd_info_t mtd;
	erase_unit_header_t hdr, hdr2;
	off_t i;
	u_int j, nbam, *bam;
	int control, data, blk_free, deleted;

	/* Get partition size, block size */
	if (ioctl(fd, MEMGETINFO, &mtd) != 0) {
		perror("get info failed");
		return;
	}

	printf("Memory region info:\n");
	printf("  Region size = ");
	print_size(mtd.size);
	printf("  Erase block size = ");
	print_size(mtd.erasesize);
	printf("\n\n");

	for (i = 0; i < mtd.size/mtd.erasesize; i++) {
		if (lseek(fd, (i * mtd.erasesize), SEEK_SET) == -1) {
			perror("seek failed");
			break;
		}
		read_nocheck(fd, &hdr, sizeof(hdr));
		if ((le32_to_cpu(hdr.FormattedSize) > 0) &&
				(le32_to_cpu(hdr.FormattedSize) <= mtd.size) &&
				(le16_to_cpu(hdr.NumEraseUnits) > 0) &&
				(le16_to_cpu(hdr.NumEraseUnits) <= mtd.size/mtd.erasesize))
			break;
	}
	if (i == mtd.size/mtd.erasesize) {
		fprintf(stderr, "No valid erase unit headers!\n");
		return;
	}

	printf("Partition header:\n");
	printf("  Formatted size = ");
	print_size(le32_to_cpu(hdr.FormattedSize));
	printf(", erase units = %d, transfer units = %d\n",
			le16_to_cpu(hdr.NumEraseUnits), hdr.NumTransferUnits);
	printf("  Erase unit size = ");
	print_size(1 << hdr.EraseUnitSize);
	printf(", virtual block size = ");
	print_size(1 << hdr.BlockSize);
	printf("\n");

	/* Create basic block allocation table for control blocks */
	nbam = (mtd.erasesize >> hdr.BlockSize);
	bam = malloc(nbam * sizeof(u_int));
	if (!bam) {
		perror("malloc failed");
		return;
	}

	for (i = 0; i < le16_to_cpu(hdr.NumEraseUnits); i++) {
		if (lseek(fd, (i << hdr.EraseUnitSize), SEEK_SET) == -1) {
			perror("seek failed");
			break;
		}
		if (read(fd, &hdr2, sizeof(hdr2)) == -1) {
			perror("read failed");
			break;
		}
		printf("\nErase unit %lld:\n", (long long)i);
		if ((hdr2.FormattedSize != hdr.FormattedSize) ||
				(hdr2.NumEraseUnits != hdr.NumEraseUnits) ||
				(hdr2.SerialNumber != hdr.SerialNumber))
			printf("  Erase unit header is corrupt.\n");
		else if (le16_to_cpu(hdr2.LogicalEUN) == 0xffff)
			printf("  Transfer unit, erase count = %d\n", le32_to_cpu(hdr2.EraseCount));
		else {
			printf("  Logical unit %d, erase count = %d\n",
					le16_to_cpu(hdr2.LogicalEUN), le32_to_cpu(hdr2.EraseCount));
			if (lseek(fd, (i << hdr.EraseUnitSize)+le32_to_cpu(hdr.BAMOffset),
						SEEK_SET) == -1) {
				perror("seek failed");
				break;
			}
			if (read(fd, bam, nbam * sizeof(u_int)) == -1) {
				perror("read failed");
				break;
			}
			blk_free = deleted = control = data = 0;
			for (j = 0; j < nbam; j++) {
				if (BLOCK_FREE(le32_to_cpu(bam[j])))
					blk_free++;
				else if (BLOCK_DELETED(le32_to_cpu(bam[j])))
					deleted++;
				else switch (BLOCK_TYPE(le32_to_cpu(bam[j]))) {
					case BLOCK_CONTROL: control++; break;
					case BLOCK_DATA: data++; break;
					default: break;
				}
			}
			printf("  Block allocation: %d control, %d data, %d free,"
					" %d deleted\n", control, data, blk_free, deleted);
		}
	}

	free(bam);
} /* format_partition */

/* Show usage information */
static void showusage(void)
{
	fprintf(stderr, "usage: %s device\n", PROGRAM_NAME);
}

/*====================================================================*/

static const struct option long_opts[] = {
	{"help", no_argument, 0, 'h'},
	{"version", no_argument, 0, 'V'},
	{0, 0, 0, 0},
};

int main(int argc, char *argv[])
{
	int c, optch, errflg, fd;
	struct stat buf;

	errflg = 0;
	while ((optch = getopt_long(argc, argv, "hV", long_opts, &c)) != -1) {
		switch (optch) {
			case 'V':
				common_print_version();
				exit(EXIT_SUCCESS);
			case 'h':
				errflg = 1; break;
			default:
				errflg = -1; break;
		}
	}
	if (errflg || (optind != argc-1)) {
		showusage();
		exit(errflg > 0 ? 0 : EXIT_FAILURE);
	}

	fd = open(argv[optind], O_RDONLY);
	if (fd == -1) {
		perror("open failed");
		exit(EXIT_FAILURE);
	}
	if (fstat(fd, &buf) != 0) {
		perror("status check failed");
		close(fd);
		exit(EXIT_FAILURE);
	}
	if (!(buf.st_mode & S_IFCHR)) {
		fprintf(stderr, "%s is not a character special device\n",
				argv[optind]);
		close(fd);
		exit(EXIT_FAILURE);
	}

	check_partition(fd);
	close(fd);

	exit(EXIT_SUCCESS);
	return 0;
}
