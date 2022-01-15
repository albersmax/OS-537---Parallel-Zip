#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <unistd.h>

int totalFiles;
int pageSize;
int totalPages;
int totalThreads;
int queueHead = 0;
int queueTail = 0;
int queueSize = 0;
int complete = 0;
int* pagesPerFile;
#define queueCap  12 // Cannot have static array (pageBuff[queueCap]) whose size is given as a variable

// Locks and CV's
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t empty = PTHREAD_COND_INITIALIZER, fill = PTHREAD_COND_INITIALIZER;

struct buffer {
	char* address;
	int buffPageSize;
	int fileIndex;
	int pageIndex;
} pageBuff[queueCap];
struct output {
	char* chars;
	int* numChars;
	int size;
} *outInfo;

void insert(struct buffer);
struct buffer pop();

void print() {
	char* output = malloc(totalPages * pageSize * (sizeof(int) + sizeof(char)));
	char* outputStart = output;
	for (int a = 0; a < totalPages; a++) {
		
		if (outInfo[a].size == 0) continue;
		// If not last page, check if it's equal to the next page's first character, combine if true
		if (a < (totalPages - 1)) {
			if (outInfo[a + 1].size != 0 && outInfo[a].chars[outInfo[a].size - 1] == outInfo[a + 1].chars[0]) {
			        outInfo[a + 1].numChars[0] += outInfo[a].numChars[outInfo[a].size - 1];
			        // Remove the last character from the current page by reducing the size
			        outInfo[a].size--;
			}		
		}

		for (int b = 0; b < outInfo[a].size; b++) {
			int count = outInfo[a].numChars[b];
			char character = outInfo[a].chars[b];
			*((int*)output) = count;
			output += sizeof(int);
			*((char*)output) = character;
			output += sizeof(char);
		}
	}
	fwrite(outputStart, output - outputStart, 1, stdout);
}


void* producer(void *arg) {
	char** fileList = (char **)arg;
	struct stat fileInfo;
	char* map;
	int file;

	for (int i = 0; i < totalFiles; i++) {
		file = open(fileList[i], O_RDONLY);
		int pagesInFile = 0;
		int finalPageSize = 0;

		if (file == -1) {
			continue;
		}


		if (fstat(file, &fileInfo) == -1) {
			close(file);
			continue;
		}

		// Generate number of pages and final pages size (if not page-aligned)
		pagesInFile = (fileInfo.st_size / pageSize);
		if (((double)fileInfo.st_size/pageSize) > pagesInFile) {
			pagesInFile += 1;
			finalPageSize = fileInfo.st_size % pageSize;
		}
		else {
			finalPageSize = pageSize;
		}
		totalPages += pagesInFile;
		pagesPerFile[i] = pagesInFile;

		map = mmap(NULL, fileInfo.st_size, PROT_READ, MAP_SHARED, file, 0);
		if (map == MAP_FAILED) {
			close(file);
			exit(1);
		}
		
		for (int j = 0; j < pagesInFile; j++) {
			pthread_mutex_lock(&lock);
			while(queueSize == queueCap) {
				pthread_cond_broadcast(&fill);
				pthread_cond_wait(&empty,&lock);
			}
			pthread_mutex_unlock(&lock);
			struct buffer buff;
			if (j == (pagesInFile - 1)) {
			       buff.buffPageSize = finalPageSize;
			}
			else {
				buff.buffPageSize = pageSize;
			}

			buff.address = map;
			buff.fileIndex = i;
			buff.pageIndex = j;
			map += pageSize;

			pthread_mutex_lock(&lock);
			insert(buff);
			pthread_mutex_unlock(&lock);
			pthread_cond_signal(&fill);
		}
		close(file);
	}
	complete = 1;
	pthread_cond_broadcast(&fill);
	return 0;
}


void insert(struct buffer buff) {
        pageBuff[queueHead] = buff;
        queueHead = (queueHead + 1) % queueCap;
        queueSize++;
}

struct buffer pop() {
        struct buffer buff = pageBuff[queueTail];
        queueTail = (queueTail + 1) % queueCap;
        queueSize--;
        return buff;
}

struct output compress(struct buffer buff) {

	struct output compFile;
	compFile.numChars = malloc(buff.buffPageSize * sizeof(int));
	char* string = malloc(buff.buffPageSize);
	int countVal = 0;
	
	for (int a = 0; a < buff.buffPageSize; a++) {
		if (buff.address[a] == '\0') continue;
		string[countVal] = buff.address[a];
		compFile.numChars[countVal] = 1;
		char temp = buff.address[a];
		while ((a + 1 < buff.buffPageSize) && (temp == buff.address[a + 1] || buff.address[a + 1] == '\0')) {
			if (buff.address[a + 1] == '\0') {
				a++;
				continue;
			}
			compFile.numChars[countVal]++;
			a++;
		}
		countVal++;
	}
	compFile.size = countVal;
	compFile.chars = realloc(string, countVal);
	return compFile;
}

// Returns correct output position for buffer object
int outputIndex(struct buffer buff) {
	int index = 0;
	// Count total number of pages in between start of output and start of current page
	for(int a = 0; a < buff.fileIndex; a++) {
		index += pagesPerFile[a];
	}
	index += buff.pageIndex;
	return index;
}

void* consumer() {
	do {
		pthread_mutex_lock(&lock);
		while (queueSize == 0 && complete == 0) {
			pthread_cond_signal(&empty);
			pthread_cond_wait(&fill, &lock);
		}
		if (complete == 1 && queueSize == 0) {
			pthread_mutex_unlock(&lock);
			return NULL;
		}

		struct buffer buff = pop();
		if (complete == 0) {
			pthread_cond_signal(&empty);
		}
		pthread_mutex_unlock(&lock);

		int positIndex = outputIndex(buff);
		outInfo[positIndex] = compress(buff);
	} while(!(complete == 1 && queueSize == 0));

	return NULL;
}

int main(int argc, char* argv[]) {

	if (argc<2) {
		printf("pzip: file1 [file2 ...]\n");
		exit(1);
	}

	totalFiles = argc - 1; // Total number of files to be compressed
	pageSize = getpagesize();
	totalThreads = get_nprocs();
	pagesPerFile = malloc(sizeof(int) * totalFiles);

	outInfo = malloc(sizeof(struct output) * 1000000);

	pthread_t pid,cid[totalThreads];
	pthread_create(&pid, NULL, producer, argv+1);

	for (int i = 0; i < totalThreads; i++) {
        	pthread_create(&cid[i], NULL, consumer, NULL);
    	}

    	for (int i = 0; i < totalThreads; i++) {
        	pthread_join(cid[i], NULL);
    	}

   	pthread_join(pid,NULL);
	print();
	return 0;
}
