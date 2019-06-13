#include "cache.h"

cache_block *head, *tail;
int readcnt;
int cache_size = 0;

// Data structures like linkedlist, tree, hash table, you can use anything.
void enqueue(char* uri, char* response, char* content, int contentLength);
int dequeue();

cache_block* find_cache_block(char* uri){
	sem_wait(&mutex);
	readcnt++;
	if (readcnt == 1)
		sem_wait(&w);
	sem_post(&mutex);

	cache_block *probe = head;
	while (probe != NULL) {
		if (!strcmp(probe -> url, uri)) {
			return probe;
		}
		else {
			probe = probe -> next;
		}
	}

	sem_wait(&mutex);
	readcnt--;
	if (readcnt == 0)
		sem_post(&w);
	sem_post(&mutex);
	return NULL;
}

void cache_replacement_policy(){
	sem_wait(&w);
	int reduce = dequeue();
	cache_size -= reduce;
	sem_post(&w);
}

void add_cache_block(char* uri,char* response,char* content,int contentLength){

	/* 1.Check the size of the content is over MAX_CONTENT_SIZE */
	if (contentLength > MAX_CONTENT_SIZE) {
		free(content);
	}

	/* 2.Find first whether the following uri is already cached */
	else if (find_cache_block(uri)) {
		free(content);
	}

	/* 3.Check the cache is full, then you have to execute cache replacement  policy.*/
	else {
		sem_wait(&w);
		cache_size += contentLength;
		while (cache_size > MAX_OBJECT_SIZE) {
			cache_replacement_policy();
		}
		/* 4.Add new cache block into proxy cache */
		enqueue(uri, response, content, contentLength);

		sem_post(&w);
	}
	return;
}

void enqueue(char* uri, char* response, char* content, int contentLength) {
	cache_block *newnode = (cache_block*)malloc(sizeof(cache_block));
	newnode -> url = malloc((strlen(uri)+1) * sizeof(char));
	newnode -> response = malloc((strlen(response)+1) * sizeof(char));

	strcpy(newnode->url, uri);
	strcpy(newnode->response, response);
	newnode -> content = content;
	newnode -> contentLength = contentLength;

	newnode -> next = head;
	newnode -> prev = NULL;

	if (head != NULL) {
		head -> prev = newnode;
	}
	else {
		tail = newnode;
	}
	head = newnode;
}

int dequeue() {
	int delsize;
	if (tail == NULL) return 0;
	delsize = tail -> contentLength;
	free(tail -> url);
	free(tail -> response);
	free(tail -> content);
	if (tail -> prev == NULL) {
		free(tail);
		tail = NULL;
	}
	else {
		tail = tail -> prev;
		free(tail -> next);
	}
	return delsize;
}


