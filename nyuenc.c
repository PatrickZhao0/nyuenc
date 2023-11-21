#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#define MAX_SIZE (1024*1024*1024)
#define CHUNK_SIZE 4096

pthread_mutex_t mutex_queue = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_queue = PTHREAD_COND_INITIALIZER;
pthread_mutex_t mutex_merge = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_merge = PTHREAD_COND_INITIALIZER;

typedef struct task{
   char task[CHUNK_SIZE];
   unsigned int raw_size;
   unsigned int compressed_size;
   unsigned char* compressed;
   struct task* next;
}Task;
Task* taskInnit();

typedef struct{
    Task* head;
    Task* tail;
    Task** trace;
    unsigned int all_submited;
}TaskQueue;
TaskQueue* taskQueueInnit();

void errorHandler(char* message);
void enc(Task* task);
void collect(TaskQueue* taskQueue);
void enqueue(TaskQueue* taskQueue, Task* task);
Task* dequeue(TaskQueue* taskQueue);
unsigned int submitTasks(char* fds[], unsigned int raw_size[], TaskQueue* taskQueue);
void* excuteTasks(void* taskQueue);

int main(int argc, char *argv[]){
    int opt;
    unsigned int multithreads = 1;
    int offset = 1;
    while ((opt = getopt(argc, argv, "j:")) != -1) {
        switch (opt) {
        case 'j':
            if(*optarg > 57 || *optarg < 49)
                errorHandler("Invalid Argument: It should be a int > 0\n");
            multithreads = atoi(optarg);
            offset += 2;
            break;
        case '?':
        default:
            errorHandler("Fail to Parse Options\n");
        }
    }
    char* fds[100];
    unsigned int raw_size[100] = {0};
    for(int i = 0 + offset; i < argc; i++){
        int fd = open(argv[i], O_RDONLY);
        if (fd == -1)
            errorHandler("Fail to Open File\n");
        struct stat sb;
        if (fstat(fd, &sb) == -1)
            errorHandler("Fail to Map File");
        char* addr = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (addr == MAP_FAILED)
            errorHandler("Fail to Map File\n");
        fds[i-offset] = addr;
        raw_size[i-offset] = (unsigned int) sb.st_size;
        fds[i-offset+1] = NULL;
    }
    TaskQueue* taskQueue = taskQueueInnit();
    pthread_t threads[multithreads];
    for(unsigned int i = 0; i < multithreads; i++){
        if(pthread_create(&threads[i], NULL, &excuteTasks, taskQueue) != 0){
            errorHandler("Fail to create thread\n");
        }
    }
    submitTasks(fds, raw_size, taskQueue);
    collect(taskQueue);
    for(unsigned int i = 0; i < multithreads; i++){
         if(pthread_join(threads[i], NULL) != 0){
             errorHandler("Fail to join thread\n");
         }
    }
}

void enc(Task* task){
    unsigned char* compressed = malloc(task->raw_size*2+1);
    unsigned int i_c = 0;
    for(unsigned int i = 0; i < task->raw_size; i++){
        unsigned int count = 0;
        do{
            count++;
            i++;
        }while(i < task->raw_size && task -> task[i] == task -> task[i-1]);
        compressed[i_c ++] = task -> task[--i];
        compressed[i_c ++] = count;
        task->compressed_size += 2;
    }
    task -> compressed = compressed;
}

void collect(TaskQueue* taskQueue){
    char* pending = NULL;
    pthread_mutex_lock(&mutex_merge);
    while(taskQueue -> head != NULL){
        while(taskQueue -> head -> compressed == NULL){
            pthread_cond_wait(&cond_merge, &mutex_merge);
        }
        Task* task = dequeue(taskQueue);
        if(pending == NULL){
            pending = (char*) malloc(2);
        }else if(pending[0] == task -> compressed[0]){
            task -> compressed[1] += pending[1];
        }else{
            fwrite(pending, 1, 2, stdout);
        }
        fwrite(task->compressed, 1, task -> compressed_size - 2, stdout);
        fflush(stdout);
        pending[0] = task -> compressed [task -> compressed_size - 2];
        pending[1] = task -> compressed [task -> compressed_size - 1];
        free(task -> compressed);
        free(task);
    }
    pthread_mutex_unlock(&mutex_merge);
    fwrite(pending, 1, 2, stdout);
    fflush(stdout);
    if(pending != NULL) free(pending);
    free(taskQueue);
}

void errorHandler(char* message){
    fprintf(stderr, "%s", message);
    exit(EXIT_FAILURE);
}

TaskQueue* taskQueueInnit(){
    TaskQueue* taskQueue = (TaskQueue*)malloc(sizeof(TaskQueue));
    taskQueue -> head = NULL;
    taskQueue -> tail = NULL;
    taskQueue -> all_submited = 0;
    taskQueue -> trace = &(taskQueue -> head);
    return taskQueue;
}

Task* taskInnit(){
    Task* task = (Task*) malloc(sizeof(Task));
    memset(task -> task, 0, CHUNK_SIZE);
    task -> raw_size = 0; 
    task -> compressed_size = 0;
    task -> compressed = NULL;
    task -> next = NULL;
    return task;
}

void enqueue(TaskQueue* taskQueue, Task* task){
    if(taskQueue == NULL || task == NULL){
        errorHandler("Unable to Enqueue\n");
    }
    if(taskQueue->head == NULL){
        taskQueue -> head = task;
        taskQueue -> tail = task;
    }else{
        taskQueue -> tail -> next = task;
        taskQueue -> tail = task;
    }
    task -> next = NULL;
}

Task* dequeue(TaskQueue* taskQueue){
    if(taskQueue == NULL){
        errorHandler("Invalid Queue\n");
    }else if(taskQueue -> head == NULL){
        return NULL;
    }else{
        Task* temp = taskQueue -> head;
        taskQueue -> head = taskQueue -> head -> next;
        if(taskQueue -> head == NULL){
            taskQueue -> tail = NULL;
        }
        return temp;
    }
    return NULL;
}

unsigned int submitTasks(char* fds[], unsigned int raw_size[], TaskQueue* taskQueue){
    unsigned int total_size = 0;
    for(int i = 0; fds[i] != NULL; i++)
        total_size += raw_size[i];
    char* fullchunk = (char*) malloc(total_size);
    int i_fc = 0;
    for(int i = 0; fds[i] != NULL; i++){
        for(unsigned int j = 0; j < raw_size[i]; j++){
            fullchunk[i_fc] = fds[i][j];
            i_fc ++;
        }
    }
    for(unsigned int i = 0; i < total_size; ){
        Task* task = taskInnit();
        unsigned int count = 0;
        while(count < CHUNK_SIZE && i < total_size){
            task -> task[count] = fullchunk[i];
            count ++;
            i++;
        }
        task -> raw_size = count;
        pthread_mutex_lock(&mutex_queue);
        enqueue(taskQueue, task);
        pthread_cond_signal(&cond_queue);
        pthread_mutex_unlock(&mutex_queue);
    }
    taskQueue -> all_submited = 1;
    free(fullchunk);
    pthread_mutex_lock(&mutex_queue);
    pthread_cond_signal(&cond_queue);
    pthread_mutex_unlock(&mutex_queue);
    return total_size;
}

void* excuteTasks(void* queue){
    TaskQueue* taskQueue = (TaskQueue*) queue;
    Task* task;
    while(1){
        pthread_mutex_lock(&mutex_queue);
        while (*(taskQueue -> trace) == NULL){
            if(taskQueue -> all_submited == 1){
                pthread_cond_signal(&cond_queue);
                pthread_mutex_unlock(&mutex_queue);
                pthread_exit(NULL);
            }
            pthread_cond_wait(&cond_queue, &mutex_queue);
        }
        task = *(taskQueue -> trace);
        taskQueue -> trace = &((*(taskQueue -> trace)) -> next);
        pthread_mutex_unlock(&mutex_queue);
        pthread_mutex_lock(&mutex_merge);
        enc(task);
        pthread_cond_signal(&cond_merge);
        pthread_mutex_unlock(&mutex_merge);
    }
}

//Citation: Thread pool implementation is inspired by https://code-vault.net/lesson/j62v2novkv:1609958966824