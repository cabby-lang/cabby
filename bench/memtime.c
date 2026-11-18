/* memtime: run a program and report its wall-clock time and peak memory.
 *
 *     memtime OUTFILE program args...
 *
 * Writes "seconds peak_kb" to stderr; the program's own output goes to
 * OUTFILE. Starting programs from this small launcher (rather than from
 * Python) keeps the launcher's own memory out of the program's peak. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: memtime OUTFILE program args...\n");
		return 2;
	}
	struct timespec t0, t1;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	pid_t pid = fork();
	if (pid == 0) {
		int fd = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd < 0 || dup2(fd, 1) < 0)
			_exit(127);
		execvp(argv[2], argv + 2);
		_exit(127);
	}
	int status;
	struct rusage ru;
	if (wait4(pid, &status, 0, &ru) < 0)
		return 2;
	clock_gettime(CLOCK_MONOTONIC, &t1);
	double secs = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
	fprintf(stderr, "%.6f %ld\n", secs, ru.ru_maxrss);
	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	return 128 + WTERMSIG(status);
}
