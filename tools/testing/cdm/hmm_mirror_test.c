#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

#define HMM_DMIRROR_READ                _IOWR('H', 0x00, struct hmm_dmirror_read)
#define HMM_DMIRROR_WRITE               _IOWR('H', 0x01, struct hmm_dmirror_write)
#define HMM_DMIRROR_MIGRATE             _IOWR('H', 0x02, struct hmm_dmirror_migrate)

struct hmm_dmirror_migrate {
unsigned long                addr;
unsigned long                npages;
};

int main()
{
	char *a;
	int fd;
	struct hmm_dmirror_migrate dmigrate;

	fd = open("/dev/hmmc", O_RDONLY, S_IRUSR);
	if (fd < 0)
		return -1;

	a = malloc(100*1024*1024);
	if (!a)
		return -1;

	memset(a, 0, 100*1024*1024);
	getchar();
	dmigrate.addr = (unsigned long)a;
	dmigrate.npages = (100*1024*1024)/(64 * 1024);

	printf("ioctl = %d\n", ioctl(fd, HMM_DMIRROR_MIGRATE, &dmigrate));
	getchar();

	printf("Freeing memory now\n");

	close(fd);
	free(a);

	return 0;
}
