/*
 * Loader Implementation
 *
 * 2022, Operating Systems
 */

#include <signal.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <string.h>
#include <unistd.h>

#include "exec_parser.h"

static so_exec_t *exec;
static int fd;
static struct sigaction old_action;
struct sigaction action;

/*
 * Handler-ul va cauta in fiecare segment din exec daca adresa
 * la care a aparut eroarea exista. Daca adresa este invalida
 * se va trata cu handler-ul default de page fault.
 * Daca adresa se afla in segment, se calculeaza numarul de pagini,
 * pagina in care se afla adresa si se aloca memorie in data daca
 * nu a fost alocata deja. Data contine paginile care au fost mapate.
 * Daca nu a fost mapata, se va mapa si se verifica daca este nevoie
 * de zeroizare in pagina.
 * Daca este deja mapata se va trata cu handler-ul default de page fault
 */

static void segv_handler(int signum, siginfo_t *info, void *context)
{
	int i = 0;
	char *fault = info->si_addr;
	int pageSize = getpagesize();

	while (i < exec->segments_no) {
		if ((char *)exec->segments[i].vaddr
				+ exec->segments[i].mem_size > fault)
			break;
		i++;
	}


	if (info->si_code != SEGV_MAPERR || i == exec->segments_no)
		old_action.sa_sigaction(signum, info, context);

	char *vaddr = (char *)exec->segments[i].vaddr;
	char *file_address = vaddr + exec->segments[i].file_size;
	int pageno = (fault - vaddr) / pageSize;

	char *aligned = (char *)ALIGN_DOWN((uintptr_t)fault, pageSize);
	char *addr = mmap(aligned, pageSize, PROT_WRITE,
			MAP_ANONYMOUS | MAP_FIXED | MAP_SHARED, 0, 0);

	if (addr == MAP_FAILED)
		exit(-1);

	int length = pageSize;

	if (aligned + pageSize > file_address) {
		if(aligned >= file_address)
			length = 0;
		else
			length = file_address - aligned;
	}

	lseek(fd, exec->segments[i].offset + pageno * pageSize, SEEK_SET);
	read(fd, addr, length);

	if (mprotect(addr, pageSize, exec->segments[i].perm) == -1)
		exit(-1);
}

int so_init_loader(void)
{
	sigemptyset(&action.sa_mask);
	sigaddset(&action.sa_mask, SIGSEGV);
	action.sa_flags = SA_SIGINFO;
	action.sa_sigaction = segv_handler;

	if (sigaction(SIGSEGV, &action, &old_action) == -1)
		exit(-1);
	return 0;
}

int so_execute(char *path, char *argv[])
{
	exec = so_parse_exec(path);
	if (!exec)
		return -1;

	fd = open(path, O_RDONLY);
	so_start_exec(exec, argv);

	close(fd);
	return 0;
}