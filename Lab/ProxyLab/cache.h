/*
 * cache.h - Cache functionalities for proxy server of SNU proxy lab
 * Author: Jiwong Ko
 * Email: jiwong@csap.snu.ac.kr
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>

#define MAX_CONTENT_SIZE 200000 // MAX content size for proxy server: 200kb
#define MAX_OBJECT_SIZE 1000000 // MAX cache size for proxy server: 1MB
#define MAX_URL_SIZE 1024
#define MAX_RESP_SIZE 1024

sem_t mutex, w;

typedef struct cache_block{
	char *url;
	char *response;
	char *content;
	int contentLength;
	struct cache_block *next;
	struct cache_block *prev;
} cache_block;

/* Cache function prototypes*/
cache_block* find_cache_block(char* uri);
void cache_replacement_policy();
void add_cache_block(char* uri, char* response, char* content, int contentLength);

