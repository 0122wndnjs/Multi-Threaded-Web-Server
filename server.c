#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include "util.h"
#include <stdbool.h>
#include <unistd.h>
#include <math.h>

#define MAX_THREADS 100
#define MAX_queue_len 100
#define MAX_CE 100
#define INVALID -1
#define BUFF_SIZE 1024

/*
  THE CODE STRUCTURE GIVEN BELOW IS JUST A SUGESSTION. FEEL FREE TO MODIFY AS NEEDED
*/

// initializing mutex locks
static pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t cache_lock = PTHREAD_MUTEX_INITIALIZER;

// initializing conditional variables
static pthread_cond_t not_empty = PTHREAD_COND_INITIALIZER;
static pthread_cond_t not_full = PTHREAD_COND_INITIALIZER;

static pthread_t dispatchers[MAX_THREADS];
static pthread_t workers[MAX_THREADS];
static FILE* log_file;
static FILE * fp;
static int qlen;
int queue_next_index = 0;
int queue_current = 0;
static int cache_size;
int num_dispatchers;
int num_workers;
static char path[1024];
int cur_index;


// structs:
typedef struct request_queue {
   int fd;
   void *request;
} request_t;

typedef struct cache_entry {
    int len;
    char *request;
    char *content;
} cache_entry_t;

request_t *request_queue;
cache_entry_t** cache_buffer;

/* ************************ Dynamic Pool Code ***********************************/
// This function implements the policy to change the worker thread pool dynamically
// depending on the number of requests
void * dynamic_pool_size_update(void *arg) {
  while(1) {
    // Run at regular intervals
    // Increase / decrease dynamically based on your policy
  }
}
/**********************************************************************************/

/* ************************************ Cache Code ********************************/

// Function to check whether the given request is present in cache
int getCacheIndex(char *request){
  /// return the index if the request is present in the cache
  int k;
  for (int i = 0; i < cache_size; i++) {
       //printf("cache buffer is %s, request is %s\n",cache_buffer[i].request,request);
       if (cache_buffer[i]->request != NULL) {
         k = strcmp(cache_buffer[i]->request,request);
         if(k==0) {
           return i;
         }
       } else {
         return -1;
       }
  }
  return -1;
}

//Function to add the request and its file content into the cache
void addIntoCache(char *mybuf, char *memory , int memory_size){
  // It should add the request at an index according to the cache replacement policy
  // Make sure to allocate/free memeory when adding or replacing cache entries

    free(cache_buffer[cur_index]->content);
    free(cache_buffer[cur_index]->request);
    cache_buffer[cur_index]->request = (char *)malloc(strlen(mybuf) * sizeof(char));
    cache_buffer[cur_index]->content = (char *)malloc(memory_size * sizeof(char));
    strcpy(cache_buffer[cur_index]->request, mybuf);
    memcpy(cache_buffer[cur_index]->content, memory, memory_size);
    cache_buffer[cur_index]->len = memory_size;
    cur_index = (cur_index + 1) % cache_size;
}

// clear the memory allocated to the cache
void deleteCache() {
  // De-allocate/free the cache memory
  for (int i=0; i<cache_size; i++) {
    free(cache_buffer[i]->request);
    free(cache_buffer[i]->content);
    free(cache_buffer[i]);
  }
  free(cache_buffer);
}

// Function to initialize the cache
void initCache(){
  // Allocating memory and initializing the cache array
    cache_buffer = malloc(sizeof(cache_entry_t*) * cache_size);
    for(int i = 0; i < cache_size; i++) {
      cache_buffer[i] = malloc(sizeof(cache_buffer[i]));
      cache_buffer[i]->content = malloc(sizeof(char));
      cache_buffer[i]->request = malloc(sizeof(char));
      cache_buffer[i]->request = NULL;
      cache_buffer[i]->len = 0;
    }
  }


// Function to open and read the file from the disk into the memory
// Add necessary arguments as needed
int readFromDisk(char * path_buf) {
  // Open and read the contents of file given the request
  fp = fopen(path_buf, "r");
  if (fp < 0) {
    printf("Error accessing file.\n");
    return -1;
  }
  return 0;
}

/**********************************************************************************/

/* ************************************ Utilities ********************************/
// Function to get the content type from the request
char* getContentType(char * mybuf) {
  // Should return the content type based on the file type in the request
  int path_len = strlen(mybuf);
  char *content_type = malloc(13*sizeof(char));

  if (path_len > 5 && strcmp(mybuf + path_len - 5, ".html") == 0) {
    strcpy(content_type, "text/html");
  } else if (path_len > 4 && strcmp(mybuf + path_len - 4, ".jpg") == 0) {
    strcpy(content_type, "image/jpeg");
  } else if (path_len > 4 && strcmp(mybuf + path_len - 4, ".gif") == 0) {
    strcpy(content_type, "image_gif");
  } else {
    strcpy(content_type, "text/plain");
  }
  return content_type;
}

// This function returns the current time in microseconds
long getCurrentTimeInMicro() {
  struct timeval curr_time;
  gettimeofday(&curr_time, NULL);
  return curr_time.tv_sec * 1000000 + curr_time.tv_usec;
}

/**********************************************************************************/

// Function to receive the request from the client and add to the queue
void * dispatch(void *arg) {
  char filebuf[1024];
   request_t request;
   int fd = 0;
   while (1) {
     // Accept client connection
     if((fd = accept_connection()) < 0) {
 			perror("accept_connection failed");
 			continue;
 		 }

     // Get request from the client
     if(get_request(fd, filebuf) == 0) {
       if(pthread_mutex_lock(&queue_lock) < 0) {
         printf("Error: Fail to lock\n");
       }

       while(queue_next_index == qlen) {
         pthread_cond_wait(&not_full, &queue_lock);
       }
       pthread_cond_signal(&not_empty);

       if(pthread_mutex_unlock(&queue_lock) < 0) {
         printf("Error: Fail to unlock\n");
       }
     } else {
       printf("Failed!\n");
     }

     // Add the request into the queue
     request.fd = fd;
     request.request = filebuf;
     request_queue[queue_next_index] = request;
     queue_next_index = (queue_next_index + 1) % qlen;

     fd = 0;
   }
   return NULL;
}

/**********************************************************************************/

// Function to retrieve the request from the queue, process it and then return a result to the client
void * worker(void *arg) {
  int micro_time = 0;
  int fd;
  char *request;
  char path_buf[1024];
  char *con;
  char msg_to_log_file[1024];
  int my_thread_id = -1;
  int num_of_requests = 0;
  int hit_miss;
  int cache_index;
  int error_code;

  while(!pthread_equal(workers[++my_thread_id], pthread_self()) && (my_thread_id < MAX_THREADS));
      my_thread_id++;

  while (1) {
    // Start recording time
    // micro_time = getCurrentTimeInMicro();
    // Get the request from the queue
    if(pthread_mutex_lock(&queue_lock) < 0) {
      printf("Error: Fail to lock\n");
    }

    if (num_dispatchers == 0 && queue_next_index == 0) {
      pthread_exit(NULL);
    }

    while(queue_next_index == queue_current) {
      pthread_cond_wait(&not_empty, &queue_lock);
    }

    micro_time = getCurrentTimeInMicro();

    fd = request_queue[queue_current].fd;
    request = request_queue[queue_current].request;
    queue_current = (queue_current+1) % qlen;

    pthread_cond_signal(&not_full);

    if(pthread_mutex_unlock(&queue_lock) < 0) {
      printf("Error: Fail to lock\n");
    }

    if(pthread_mutex_lock(&log_lock) < 0) {
      printf("Error: Fail to lock\n");
    }
    con = getContentType(request);
    int length;
    char* buffer;
    strcpy(path_buf, path);
    strcat(path_buf, request);
    cache_index = getCacheIndex(request);

    if((log_file = fopen("web_server_log", "a")) == NULL) {
      perror("failed to open log file");
    }
    if (cache_index!= -1) {                  // if there is cashe entry respoding to request
      hit_miss = 1;
      if(return_result(fd,con,cache_buffer[cache_index]->content,cache_buffer[cache_index]->len) != 0) {
          if((error_code = return_error(fd,cache_buffer[cache_index]->content))!= 0) {
            printf("fd 3\n: %d  ",fd);
            perror("failed to return result or error \n");
          }
          else {
            micro_time = (getCurrentTimeInMicro() - micro_time);
            sprintf(msg_to_log_file, "[%d][%d][%d][%s][%d][%d us][%s]\n", my_thread_id, ++num_of_requests, fd, request, error_code, micro_time, "HIT");
            int msg_len = strlen(msg_to_log_file);
            if(fwrite(msg_to_log_file, sizeof(char), msg_len, log_file) < msg_len){
              perror("error writing to log file");
            }
          }
      }
      else {
        micro_time = (getCurrentTimeInMicro() - micro_time);
        sprintf(msg_to_log_file, "[%d][%d][%d][%s][%d][%d us][%s]\n", my_thread_id, ++num_of_requests, fd, request, cache_buffer[cache_index]->len, micro_time, "HIT");
        int msg_len = strlen(msg_to_log_file);
        if(fwrite(msg_to_log_file, sizeof(char), msg_len, log_file) < msg_len) {
          perror("error writing to log file");
        }
      }
    } else {   // cache_index == -1
      hit_miss=0;
      if(readFromDisk(path_buf)== 0) {
        fseek(fp,0,SEEK_END);
        length = ftell(fp);
        fseek(fp,0,SEEK_SET);
        buffer = malloc(length);
        if(buffer) {
          fread(buffer,1,length,fp);
        }
        fclose(fp);
      }
      addIntoCache(request,buffer,length);
      if(return_result(fd,con,buffer,sizeof(char)*length)!=0) {
        if((error_code = return_error(fd,buffer))!= 0) {
          printf("fd 7: %d  ",fd);
          perror("failed to return result or error \n");
        } else{
          printf("fd 8: %d  ",fd);
          micro_time = (getCurrentTimeInMicro() - micro_time);
          sprintf(msg_to_log_file, "[%d][%d][%d][%s][%d][%d us][%s]\n", my_thread_id, ++num_of_requests, fd, request, error_code, micro_time, "MISS");
          int msg_len = strlen(msg_to_log_file);
          if(fwrite(msg_to_log_file, sizeof(char), msg_len, log_file) < msg_len) {
            perror("error writing to log file");
          }
        }
      } else {
        micro_time = getCurrentTimeInMicro() - micro_time;
        sprintf(msg_to_log_file, "[%d][%d][%d][%s][%d][%d us][%s]\n", my_thread_id, ++num_of_requests, fd, request, length, micro_time, "MISS");
        int msg_len = strlen(msg_to_log_file);
        if(fwrite(msg_to_log_file, sizeof(char), msg_len, log_file) < msg_len) {
          perror("error writing to log file");
        }
        free(buffer);
      }
    }
  	fclose(log_file);
    if(pthread_mutex_unlock(&log_lock) < 0) {
      printf("Error: Fail to lock\n");
    }

    // return the result
    // close(fd);
    // fd = 0;
  }
  return NULL;
}

/**********************************************************************************/

int main(int argc, char **argv) {

    int port;
    // char *path = NULL;
    int dynamic_flag;

    // Error check on number of arguments
    if(argc != 7 && argc != 8) {
      printf("usage: %s port path num_dispatchers num_workers dynamic_flag qlen cache_size\n", argv[0]);
      return -1;
    }

    port = atoi(argv[1]);
    if (port < 1025 || port > 65535) {
      printf("Port must be between 1025 and 65535.\n");
      return -1;
    }

    strcpy(path, argv[2]);
    //printf("@@@@@@@@@@@@ path is %s\n",path);

    num_dispatchers = atoi(argv[3]);
    if (num_dispatchers <= 0 || num_dispatchers > MAX_THREADS) {
      printf("Invalid number of dispatcher threads.\n");
      return -1;
    }

    num_workers = atoi(argv[4]);
    if (num_workers <= 0 || num_workers > MAX_THREADS) {
      printf("Invalid number of worker threads.\n");
      return -1;
    }

    dynamic_flag = atoi(argv[5]);
    if (dynamic_flag > 1 || dynamic_flag < 0) {
      return -1;
    }

    qlen = atoi(argv[6]);
    if (qlen > MAX_queue_len) {
      printf("Invalid queue length.\n");
      return -1;
    }

    if (argc == 8) {
      cache_size = atoi(argv[7]);
      if (cache_size > MAX_CE) {
        printf("Invalid cache size.\n");
        return -1;
      }

    } else if (cache_size <0) {
      printf("Negative cache size.\n");
      return -1;
    }

    // Start the server and initialize cache
    initCache();

    request_queue = malloc(sizeof(request_t*)*qlen);

    init(port);

    chdir(path);

    // Create the queue array (bounded buffer)
    // Initialize arrays for both thread types

    for (int i = 0; i < num_dispatchers; i++) {
      if (pthread_create(&(dispatchers[i]), NULL, dispatch, NULL) != 0) {
        printf("failed to create dispatcher thread.\n");
        exit(-1);
      }
    }

    for (int i = 0; i < num_workers; i++) {
      if (pthread_create(&(workers[i]), NULL, worker, NULL) != 0) {
        printf("failed to create worker thread.\n");
        exit(-1);
      }
    }

    for (int i = 0; i < num_dispatchers; i++) {
      if (pthread_join(dispatchers[i], NULL) != 0) {
        printf("failed to join dispatcher thread.\n");
      }
    }

    for (int i = 0; i < num_dispatchers; i++) {
      if (pthread_join(workers[i], NULL) != 0) {
        printf("failed to join worker thread.\n");
      }
    }

    deleteCache();
    // for(int i=0;i<qlen;i++){
    //   free(&request_queue[i]);
    // }
    free(request_queue);

    // Clean up
    return 0;
  }
