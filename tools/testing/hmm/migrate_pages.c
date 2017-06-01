/*
 * Test program to test the moving of a processes pages.
 *
 * (C) 2006 Silicon Graphics, Inc.
 *		Christoph Lameter <clameter@sgi.com>
 */
#include <stdio.h>
#include <stdlib.h>
#include <numa.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include "driver/uapi.h"

#define TENMB (10 * 1024 * 1024)

unsigned int pagesize;
unsigned int page_count = 32;

char *page_base;
char *pages;

void **addr;
int *status;
int *nodes;
int errors;
int nr_nodes;

struct bitmask *old_nodes;
struct bitmask *new_nodes;

int cdm_migrate_pages(void *base, unsigned long size)
{
	struct cdm_migrate mig;
	int fd, rc;

	fd = open("/dev/cdm1", O_NONBLOCK);
	if (fd < 0)
		return fd;

	mig.start = (__u64)base;
	mig.end = (__u64)base + size;

	rc = ioctl(fd, CDM_IOC_MIGRATE, &mig);
	close(fd);
	if (rc)
		return -errno;

	return 0;
}

int main(int argc, char **argv)
{
	int i, rc;
	int fflag = 0, nflag = 0;

	while ((i = getopt(argc, argv, "fn")) != -1) {
		if (i == 'f')
			fflag = 1;
		else if (i == 'n')
			nflag = 1;
	}

	pagesize = getpagesize();

	nr_nodes = numa_num_possible_nodes();

	old_nodes = numa_bitmask_alloc(nr_nodes);
	new_nodes = numa_bitmask_alloc(nr_nodes);
	numa_bitmask_setbit(old_nodes, 0);
	numa_bitmask_setbit(new_nodes, 1);

	if (!numa_max_node()) {
		printf("A minimum of 2 nodes is required for this test.\n");
		exit(1);
	}

	setbuf(stdout, NULL);
	if (argc > optind)
		sscanf(argv[optind], "%d", &page_count);

	if (fflag) {
		int fd;

		printf("Migrate test using file backed memory...\n");
		fd = open("/tmp/numa_example_tmp", O_CREAT | O_RDWR,
			  S_IRUSR | S_IWUSR);
		if (fd < 0) {
			perror("open");
			exit(1);
		}

		if (lseek(fd, TENMB, SEEK_SET) != TENMB) {
			perror("lseek");
			exit(1);
		}

		if (write(fd, "X", 1) != 1) {
			perror("write");
			exit(1);
		}

		page_base = mmap(NULL, pagesize * (page_count + 1), O_RDWR,
				 MAP_PRIVATE, fd, 0);
		if (page_base == (char *)-1) {
			perror("mmap");
			exit(1);
		}
	} else {
		printf("Migrate test using anonymous memory...\n");
		page_base = malloc(pagesize * (page_count + 1));
	}

	addr = malloc(sizeof(char *) * page_count);
	status = malloc(sizeof(int *) * page_count);
	nodes = malloc(sizeof(int *) * page_count);
	if (!page_base || !addr || !status || !nodes) {
		printf("Unable to allocate memory\n");
		exit(1);
	}

	pages = (void *) ((((long)page_base) & ~((long)(pagesize - 1))) + pagesize);

	for (i = 0; i < page_count; i++) {
		if (i != 2)
			/* We leave page 2 unallocated */
			pages[ i * pagesize ] = (char) i;
		addr[i] = pages + i * pagesize;
		nodes[i] = 0;
		status[i] = -123;
	}

	if (!fflag) {
		/* Move to starting node */
		rc = numa_move_pages(0, page_count, addr, nodes, status, 0);
		if (rc < 0 && errno != ENOENT) {
			perror("move_pages");
			exit(1);
		}
	}

	/* Verify correct startup locations */
	printf("Page location at the beginning of the test\n");
	printf("------------------------------------------\n");

	numa_move_pages(0, page_count, addr, NULL, status, 0);
	for (i = 0; i < page_count; i++) {
		printf("Page %d vaddr=%p node=%d\n", i, pages + i * pagesize, status[i]);

		if (fflag)
			continue;

		if (i != 2 && !numa_bitmask_isbitset(old_nodes, status[i])) {
			printf("Bad page state before migrate. Page %d status %d\n",i, status[i]);
			exit(1);
		}
	}

	/* Perform migration */
	printf("\nMigrating the current processes pages ...\n");
	if (nflag)
		rc = numa_migrate_pages(0, old_nodes, new_nodes);
	else
		rc = cdm_migrate_pages(pages, pagesize * page_count);

	if (rc < 0) {
		perror("migrate failed");
		errors++;
	}

	/* Get page state after migration */
	numa_move_pages(0, page_count, addr, NULL, status, 0);
	for (i = 0; i < page_count; i++) {
		printf("Page %d vaddr=%lx node=%d\n", i,
			(unsigned long)(pages + i * pagesize), status[i]);
		if (i != 2) {
			if (pages[ i* pagesize ] != (char) i) {
				printf("*** Page contents corrupted.\n");
				errors++;
			} else if (!numa_bitmask_isbitset(new_nodes, status[i])) {
				printf("*** Page on the wrong node\n");
				errors++;
			}
		}
	}

	if (!errors)
		printf("Test successful.\n");
	else
		printf("%d errors.\n", errors);

	return errors > 0 ? 1 : 0;
}

