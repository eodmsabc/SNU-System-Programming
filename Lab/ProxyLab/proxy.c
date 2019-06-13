/*
 * proxy.c - A simple, iterative HTTP proxy server
 */
#include "csapp.h"
#include "cache.h"

void doit(int fd);
void print_requesthdrs(rio_t *rp);
int parse_uri_proxy(char* uri, char* host, int *port,char* filename);
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg);

int My_writen(int fd, char *usrbuf, size_t n);

/*
 * Proxy server uses threads to handle multiple request.
 */
void *thread(void *targ) {
	int cfd = *((int*)targ);
	Pthread_detach(pthread_self());
	Free(targ);
	doit(cfd);
	Close(cfd);
	return NULL;
}

//-----------------------------------------------------------------------------
int main(int argc, char **argv)
{
/*
 * main: 
 *  listens for connections on the given port number, handles HTTP requests 
 *  with the doit function then closes the connection 
 */
	int listenfd, port, clientlen;
	int *connfd;
	struct sockaddr_in clientaddr;
	pthread_t tid;

	Sem_init(&mutex, 0, 1);
	Sem_init(&w, 0, 1);

	if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
	}

	if (argv[1][0] < '0' || '9' < argv[1][0]) {
		fprintf(stderr, "port number should be an integer\n");
		exit(1);
	}

	port = atoi(argv[1]);
	listenfd = Open_listenfd(port);
	clientlen = sizeof(clientaddr);

	while(1) {
		connfd = malloc(sizeof(int));
		*connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
		Pthread_create(&tid, NULL, thread, connfd);
	}

  // listen for connections
  // if a client connects, accept the connection, handle the requests
  // (call the do it function), then close the connection
}

//-----------------------------------------------------------------------------
void doit(int fd)
{
/*
 * doit: reads HTTP request from connection socket, forwards the request to the
 *  requested host. Reads the response from the host server, and writes the
 *  response back to the client then save contents in the proxy cache.
 * params:
 *    - fd (int): file descriptor of the connection socket.
 */  
	char line1[MAXLINE], host[MAXLINE], filename[MAXLINE], reqhead[MAXLINE];
	char method[MAXLINE], uri[MAXLINE], version[MAXLINE];
	int serverfd, port, parse_error, retry=0;
	cache_block *cache;

	rio_t rio;

	Rio_readinitb(&rio, fd);

	/* Read request header */
	// Read line1
	// format: method uri version
	Rio_readlineb(&rio, line1, MAXLINE);
	printf("Request headers:\n");
	printf("%s", line1);
	sscanf(line1, "%s %s %s", method, uri, version);

	// Check if the method is GET, if not return a 501 error using clienterror()
	if (strcasecmp(method, "GET")) {
		print_requesthdrs(&rio);
		if (!strcasecmp(method, "POST")) return;
		clienterror(fd, method, "501", "Not implemented",
				"Request method is not supported by proxy server");
		return;
	}

	print_requesthdrs(&rio);
	// Get hostname, port, filename by parse_uri()
	parse_error = parse_uri_proxy(uri, host, &port, filename);
	if (parse_error) {
		clienterror(fd, method, "400", "Bad request",
				"URL parsing error");
		return;
	}

	// If uri is in proxy cache, send the cached content to the client
	cache = find_cache_block(uri);
	if (cache) {
		//printf("\nCached data transferred!\n\n");
		Rio_writen(fd, cache -> response, strlen(cache -> response));
		Rio_writen(fd, cache -> content, cache -> contentLength);
		return;
	}
  
	/* Send the request header of the client to the server */
	// If proxy server fails to connect to server, it retries at most 100 times
	// for stability.
	while ((serverfd = open_clientfd(host, port)) < 0) {
		if (retry > 100) return;
		retry++;
	}

	Rio_writen(serverfd, line1, strlen(line1));
	
	sprintf(reqhead, "Host: %s:%d\r\n\r\n", host, port);
	Rio_writen(serverfd, reqhead, strlen(reqhead));

	char responseBuffer[MAXLINE], response[MAXLINE];
	char chunkbuffer[MAXLINE];
	int isChunked, clen;
	char *contentBuffer = NULL, *tbuf;
	int contentLength;
	rio_t serverResponse;

	responseBuffer[0] = '\0';
	response[0] = '\0';
	isChunked = 0;

	/* Read response header*/
	rio_readinitb(&serverResponse, serverfd);

	// Read the response header from the server and build the proxy's response
	// header by repeatedly adding the responseBuffer (server response)
	Rio_readlineb(&serverResponse, responseBuffer, MAXLINE);
	while (strcmp(responseBuffer, "\r\n")) {
		if (!strncasecmp(responseBuffer, "Transfer-Encoding:", 18)) {
			if (strstr(responseBuffer, "chunked") || strstr(responseBuffer, "Chunked"))
				isChunked = 1;
		}
		else if (!strncasecmp(responseBuffer, "Content-Length:", 15)) {
			sscanf(responseBuffer+15, "%d\r\n", &contentLength);
		}

		strcat(response, responseBuffer);
		Rio_readlineb(&serverResponse, responseBuffer, MAXLINE);
	}

	strcat(response, "\r\n");
	Rio_writen(fd, response, strlen(response));

	/* Read Response Body */
	if (isChunked) {
		Rio_readlineb(&serverResponse, chunkbuffer, MAXLINE);

		contentLength = strlen(chunkbuffer) + 2;
		contentBuffer = (char*)malloc(contentLength * sizeof(char));
		strcpy(contentBuffer, chunkbuffer);
		sscanf(chunkbuffer, "%x\r\n", &clen);
		while (clen) {
			clen += 2;

			tbuf = (char*)malloc((clen+4) * sizeof(char));
			Rio_readnb(&serverResponse, tbuf, clen);
			tbuf[clen] = '\0';

			Rio_readlineb(&serverResponse, chunkbuffer, MAXLINE);
			contentLength += strlen(chunkbuffer) + clen;
			contentBuffer = realloc(contentBuffer, contentLength * sizeof(char));

			strcat(contentBuffer, tbuf);
			strcat(contentBuffer, chunkbuffer);
			sscanf(chunkbuffer, "%x\r\n", &clen);
			free(tbuf);
		}
		strcat(contentBuffer, "\r\n");
	}
	else {
		contentBuffer = (char*)malloc((contentLength) * sizeof(char));
		Rio_readnb(&serverResponse, contentBuffer, contentLength);
	}

	Rio_writen(fd, contentBuffer, contentLength);

	/* Save the proxy cache */
	// Save the contents you get from the http server.
	add_cache_block(uri, response, contentBuffer, contentLength);

	// Close the connection to the server
	Close(serverfd);
}

//-----------------------------------------------------------------------------
void print_requesthdrs(rio_t *rp)
{
/**** DO NOT MODIFY ****/
/**** WARNING: This will read out everything remaining until a line break ****/
/*
 * print_requesthdrs: 
 *        reads out and prints all request lines sent to the server
 * params:
 *    - rp: Rio pointer for reading from file
 *
 */
  char buf[MAXLINE];
  Rio_readlineb(rp, buf, MAXLINE);
  while(strcmp(buf, "\r\n")) {
    printf("%s", buf);
    Rio_readlineb(rp, buf, MAXLINE);
  }
  printf("\n");
  return;
}

int parse_uri_proxy(char* uri, char* host, int* port, char* filename){
/*
 * parse_uri - Get the hostname, port, filename.
 * You have to add further parsing steps in http.c 
 * 
 * Example1: http://www.snu.ac.kr/index.html
 *			 host: www.snu.ac.kr 
 *			 filename: should be /index.html 
 *			 port: 80 (default)
 *
 * Example2: http://www.snu.ac.kr:1234/index.html
 * 			 host: www.snu.ac.kr
 * 			 filename: /index.html 
 * 			 port: 1234
 *
 * Example3: http://127.0.0.1:1234/index.html
 * 			 host: 127.0.0.1
 * 			 filename: /index.html
 * 			 port: 1234
 *
 * 			 
 * Return value for parsing success is 1
 *
 */
	*port = 80;	// default
	char t_hostport[MAXLINE], t_file[MAXLINE];
	char *ppos;
	int errchk = 1;
	strcpy(filename, "/");
	errchk = sscanf(uri, "http://%[^/]/%s", t_hostport, t_file);
	if (errchk <= 0) return -1;
	strcat(filename, t_file);

	// port check
	if ((ppos = strstr(t_hostport, ":"))) {
		*ppos = '\0';
		ppos++;
		if ('0' <= *ppos && *ppos <= '9') {
			*port = atoi(ppos);
		}
		else {
			return -1;
		}
	}
	strcpy(host, t_hostport);
	return 0;
}

void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg)
{
/**** DO NOT MODIFY ****/
/*
 * clienterror: 
 *        Creates appropriate HTML error page and sends to the client 
 * params:
 *    - fd: file descriptor of connection socket.
 *    - cause: what has caused the error: the filename or the method
 *    - errnum: The HTTP status (error) code
 *    - shortmsg: The HTTP status (error) message
 *    - longmsg: A longer description of the error that will be printed 
 *           on the error page
 *
 */  
  char buf[MAXLINE], body[MAXBUF];

  /* Build the HTTP response body */
  sprintf(body, "<html><title>Mini Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s<b>%s: %s</b>\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>Mini Web server</em>\r\n", body);

  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-Length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

int My_writen(int fd, char *usrbuf, size_t n) {
	if (rio_writen(fd, usrbuf, n) != n) {
		return -1;
	}
	return 0;
}
