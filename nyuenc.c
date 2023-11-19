#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#define MAX_SIZE (1024*1024*1024)
#define CHUNK_SIZE 4

typedef struct task{
   char task[CHUNK_SIZE];
   unsigned int order;
   unsigned int raw_size;
   unsigned int compressed_size;
   unsigned char* compressed;
   struct task* next;
}Task;

typedef struct{
    Task* head;
    Task* tail;
}TaskQueue;

void errorHandler(char* message);
void enc(Task* task);
void merge(Task* task, unsigned int* i_m, unsigned char* merged);
void taskQueueInnit(TaskQueue* taskQueue);
void enqueue(TaskQueue* taskQueue, Task* task);
Task* dequeue(TaskQueue* taskQueue);
unsigned int assignTask(char* fds[], unsigned int raw_size[], TaskQueue* taskQueue);

int main(int argc, char *argv[]){
    //Mapping of all Files
    char* fds[100];
    unsigned int raw_size[100], sizes[100];
    memset(raw_size, 0, sizeof(raw_size));
    memset(sizes, 0, sizeof(sizes));
    unsigned char* merged = (unsigned char*) malloc(MAX_SIZE);
    unsigned int i_m = 0;
    for(int i = 1; i < argc; i++){
        int fd = open(argv[i], O_RDONLY);
        if (fd == -1)
            errorHandler("Fail to Open File");
        struct stat sb;
        if (fstat(fd, &sb) == -1)
            errorHandler("Fail to Map File");
        char* addr = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (addr == MAP_FAILED)
            errorHandler("Fail to Map File");
        fds[i-1] = addr;
        raw_size[i-1] = (unsigned int) sb.st_size;
        fds[i] = NULL;
    }
    TaskQueue* taskQueue = (TaskQueue*)malloc(sizeof(TaskQueue));
    taskQueueInnit(taskQueue);
    assignTask(fds, raw_size, taskQueue);
    while(taskQueue->tail != NULL){
        Task* task = dequeue(taskQueue);
        enc(task);
        merge(task, &i_m, merged);
        free(task -> compressed);
        free(task);
    }
    fwrite(merged, 1, i_m, stdout);
    fflush(stdout);
    free(taskQueue);
    free(merged);
}

void enc(Task* task){
    unsigned char* compressed = malloc(task->raw_size*2);
    unsigned int i_c = 0;
    for(unsigned int i = 0; i < task->raw_size; i++){
        unsigned int count = 0;
        do{
            count++;
            i++;
        }while(task -> task[i] == task -> task[i-1] && i < task->raw_size);
        compressed[i_c ++] = task -> task[--i];
        compressed[i_c ++] = count;
        task->compressed_size += 2;
    }
    task -> compressed =  compressed;
}

void merge(Task* task, unsigned int* i_m, unsigned char* merged){
    if(*i_m == 0 || (*i_m >= 2 && merged[*i_m - 2] != task->compressed[0]) ){
        for(unsigned int i = 0; i < task->compressed_size; i++){
            merged[(*i_m) ++] = task->compressed[i];
        }
    }else{
        (*i_m) --;
        unsigned int count = (unsigned int) merged[*i_m] + (unsigned int) task->compressed[1];
        merged[(*i_m) ++] = count;
        for(unsigned int i = 2; i < task->compressed_size; i++)
            merged[(*i_m) ++] = task->compressed[i];
    }
}

void errorHandler(char* message){
    fprintf(stderr, "%s", message);
    exit(EXIT_FAILURE);
}

void taskQueueInnit(TaskQueue* taskQueue){
    taskQueue -> head = NULL;
    taskQueue -> tail = NULL;
}

void enqueue(TaskQueue* taskQueue, Task* task){
    if(taskQueue == NULL || task == NULL){
        errorHandler("Unable to Enqueue");
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
        errorHandler("Invalid Queue");
    }else if(taskQueue -> head == NULL){
        printf("Empty Queue");
        return NULL;
    }else{
        Task* temp = taskQueue -> head;
        taskQueue -> head = taskQueue -> head -> next;
        if(taskQueue -> head == NULL){
            taskQueue -> tail = NULL;
        }
        temp -> next = NULL;
        return temp;
    }
    return NULL;
}

unsigned int assignTask(char* fds[], unsigned int raw_size[], TaskQueue* taskQueue){
    unsigned int totalsize = 0;
    for(int i = 0; fds[i] != NULL; i++)
        totalsize += raw_size[i];
    char fullChuck[totalsize];
    int i_fc = 0;
    for(int i = 0; fds[i] != NULL; i++){
        for(unsigned int j = 0; j < raw_size[i]; j++){
            fullChuck[i_fc] = fds[i][j];
            i_fc ++;
        }
    }
    unsigned int order = 0;
    for(unsigned int i = 0; i < totalsize; ){
        Task* task = malloc(sizeof(Task));
        unsigned int count = 0;
        while(count < CHUNK_SIZE && i < totalsize){
            task -> task[count] = fullChuck[i];
            count ++;
            i++;
        }
        task -> order = order;
        order ++;
        task -> raw_size = count;
        task -> compressed_size = 0;
        task -> compressed = NULL;
        task -> next = NULL;
        enqueue(taskQueue, task);
    }
    return order;
}
