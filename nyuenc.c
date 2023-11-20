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
#define CHUNK_SIZE 4096
#define NUM_OF_THREADS 3

pthread_mutex_t mutex_queue = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_queue = PTHREAD_COND_INITIALIZER;

typedef struct task{
   char task[CHUNK_SIZE];
   unsigned int order;
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
void merge(Task* task, unsigned int* i_m, unsigned char* merged);
void enqueue(TaskQueue* taskQueue, Task* task);
Task* dequeue(TaskQueue* taskQueue);
unsigned int submitTasks(char* fds[], unsigned int raw_size[], TaskQueue* taskQueue);
void* excuteTasks(void* taskQueue);

int main(int argc, char *argv[]){
    char* fds[100];
    unsigned int raw_size[100], sizes[100];
    memset(raw_size, 0, sizeof(raw_size));
    memset(sizes, 0, sizeof(sizes));
    unsigned char* merged = (unsigned char*) malloc(MAX_SIZE);
    unsigned int i_m = 0;
    for(int i = 1; i < argc; i++){
        int fd = open(argv[i], O_RDONLY);
        if (fd == -1)
            errorHandler("Fail to Open File\n");
        struct stat sb;
        if (fstat(fd, &sb) == -1)
            errorHandler("Fail to Map File");
        char* addr = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (addr == MAP_FAILED)
            errorHandler("Fail to Map File\n");
        fds[i-1] = addr;
        raw_size[i-1] = (unsigned int) sb.st_size;
        fds[i] = NULL;
    }
    TaskQueue* taskQueue = taskQueueInnit();

    pthread_t threads[NUM_OF_THREADS];
    for(unsigned int i = 0; i < NUM_OF_THREADS; i++){
        if(pthread_create(&threads[i], NULL, &excuteTasks, taskQueue) != 0){
            errorHandler("Fail to create thread\n");
        }
    }

    submitTasks(fds, raw_size, taskQueue);

    for(unsigned int i = 0; i < NUM_OF_THREADS; i++){
        if(pthread_join(threads[i], NULL) != 0){
            errorHandler("Fail to join thread\n");
        }
    }

    while(taskQueue -> head != NULL){
        Task* task = dequeue(taskQueue);
        merge(task, &i_m, merged);
        free(task -> compressed);
        free(task);
    }
    fwrite(merged, 1, i_m, stdout);
    fflush(stdout);
    pthread_mutex_destroy(&mutex_queue);
    pthread_cond_destroy(&cond_queue);
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
    task -> compressed = compressed;
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
    task -> order = 0;
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
        temp -> next = NULL;
        return temp;
    }
    return NULL;
}

unsigned int submitTasks(char* fds[], unsigned int raw_size[], TaskQueue* taskQueue){
    unsigned int total_size = 0;
    for(int i = 0; fds[i] != NULL; i++)
        total_size += raw_size[i];
    char fullChuck[total_size];
    int i_fc = 0;
    for(int i = 0; fds[i] != NULL; i++){
        for(unsigned int j = 0; j < raw_size[i]; j++){
            fullChuck[i_fc] = fds[i][j];
            i_fc ++;
        }
    }
    unsigned int order = 0;
    for(unsigned int i = 0; i < total_size; ){
        Task* task = taskInnit();
        unsigned int count = 0;
        while(count < CHUNK_SIZE && i < total_size){
            task -> task[count] = fullChuck[i];
            count ++;
            i++;
        }
        task -> order = order;
        order ++;
        task -> raw_size = count;
        pthread_mutex_lock(&mutex_queue);
        enqueue(taskQueue, task);
        pthread_cond_signal(&cond_queue);
        pthread_mutex_unlock(&mutex_queue);
    }
    taskQueue -> all_submited = 1;
    pthread_cond_signal(&cond_queue);
    return order + 1;
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
        enc(task);
    }
}