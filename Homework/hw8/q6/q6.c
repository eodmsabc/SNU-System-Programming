#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

#define MAX_LEN 100

int main(void) {
	pid_t parent = getpid();
	pid_t child;
	int readcheck = 0;
	char readc;
	char buf[MAX_LEN+1];

	int p[2];
	if (pipe(p) == -1) {
		perror("Cannot create pipe");
		return 1;
	}

	if ((child = fork()) == 0) {
		close(p[1]);
		FILE *rdf = fdopen(p[0], "r");
		while(fscanf(rdf, "%c", &readc) != EOF) {
			if (readcheck == 0) {
				printf("Child pid = %d, received from parent %d : ", getpid(), getppid());
				readcheck = 1;
			}
			printf("%c", readc);
		}
		fclose(rdf);
	}
	else {
		close(p[0]);
		printf("Input string to send to child : ");
		fgets(buf, MAX_LEN, stdin);
		printf("Parent pid = %d: sending to child %d : %s", parent, child, buf);
		FILE *wrf = fdopen(p[1], "w");
		fprintf(wrf, "%s", buf);
		fclose(wrf);
		wait(NULL);
	}
	return 0;
}
