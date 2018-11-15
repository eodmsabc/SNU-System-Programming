#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>

#define MIN(x,y) ((x)>(y)?(y):(x))
#define MAXLINE 256

typedef struct {
	int fd;
	int cnt;
	char *ptr;
	char buffer[MAXLINE];
} IO_STRUCT;

void init_iostr(IO_STRUCT *ios, int fd);
ssize_t myread(IO_STRUCT *ios, char *usrbuf, size_t n);
ssize_t readline(IO_STRUCT *ios, char *usrbuf, size_t maxlen);
ssize_t mywrite(int fd, char *usrbuf, size_t n);
int open_clientfd(char *host, char *port);

int main(int argc, char **argv) {
	int clientfd;
	char *host, *port, buf[MAXLINE];
	IO_STRUCT client, mystdin;

	if (argc != 3) {
		write(STDERR_FILENO, "usage: echo_client <host> <port>\n", 33);
		exit(0);
	}

	host = argv[1];
	port = argv[2];

	clientfd = open_clientfd(host, port);
	
	init_iostr(&client, clientfd);
	init_iostr(&mystdin, STDIN_FILENO);

	while(1) {
		write(STDOUT_FILENO, "type:", 5);
		//if(readline(&mystdin, buf, MAXLINE) == 0) break;
		if (fgets(buf, MAXLINE, stdin) <= 0) break;
		//printf("client write %d\n", strlen(buf));
		mywrite(clientfd, buf, strlen(buf));

		//printf("client read start\n");
		readline(&client, buf, MAXLINE);

		write(STDOUT_FILENO, "echo:", 5);
		write(STDOUT_FILENO, buf, strlen(buf));
	}

	exit(0);
}

void init_iostr(IO_STRUCT *ios, int fd) {
	ios->fd = fd;
	ios->cnt = 0;
	ios->ptr = ios->buffer;
}

ssize_t myread(IO_STRUCT *ios, char *usrbuf, size_t n) {
	int cnt = 0;
	while(ios->cnt <= 0) {
		ios->cnt = read(ios->fd, ios->buffer, MAXLINE);
		if (ios->cnt < 0) {
			if (errno != EINTR)
				return -1;
		}
		else if (ios->cnt == 0) {
			return 0;
		}
		else ios->ptr = ios->buffer;
	}
	cnt = MIN(n, ios->cnt);
	memcpy(usrbuf, ios->buffer, cnt);
	ios->cnt -= cnt;
	ios->ptr += cnt;
	return cnt;
}

ssize_t readline(IO_STRUCT *ios, char *usrbuf, size_t maxlen) {
	int i, cnt;
	char c, *bufp = usrbuf;

	for (i = 1; i < maxlen; i++) {
		if ((cnt = myread(ios, &c, 1)) == 1) {
			*bufp++ = c;
			if (c == '\n') break;
		}
		else if (cnt == 0) {
			if (i == 1)
				return 0;
			else {
				i--;
				break;
			}
		}
		else return -1;
	}
	return i;
}

ssize_t mywrite(int fd, char *usrbuf, size_t n) {
	int left=n;
	int written;
	char *bufp = usrbuf;

	while(left > 0) {
		if ((written = write(fd, bufp, left)) <= 0) {
			if (errno == EINTR)
				written = 0;
			else
				return -1;
		}
		left -= written;
		bufp += written;
	}
	return n;
}

int open_clientfd(char *host, char *port) {
	struct addrinfo hints, *listp, *p;
	int clientfd;

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_ADDRCONFIG | AI_NUMERICSERV;
	getaddrinfo(host, port, &hints, &listp);

	for (p = listp; p; p = p->ai_next) {
		if ((clientfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0)
			continue;
		if (connect(clientfd, p->ai_addr, p->ai_addrlen) != -1)
			break;
		close(clientfd);
	}

	freeaddrinfo(listp);
	if (p == NULL)
		return -1;

	return clientfd;
}

