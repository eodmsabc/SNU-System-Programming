#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
	if (argc < 3) {
		perror("2 files required");
		return 1;
	}

	int outfd;
	if ((outfd = open(argv[2], O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR)) == -1) {
		perror("Cannot open output file");
		return 1;
	}

	dup2(outfd, STDOUT_FILENO);
	execl(argv[1], argv[1], NULL);
/*
	pid_t pid;
	if ((pid = fork()) == 0) {
		// child
		dup2(outfd, STDOUT_FILENO);
		if (execl(argv[1], argv[1], NULL) == -1) {
			perror("Cannot execute file");
		}
	}
	*/

	close(outfd);

	return 0;
}
