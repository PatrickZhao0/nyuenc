#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#define MAX_SIZE (1024*1024*1024)

unsigned char* enc(unsigned int* data_size, unsigned int raw_size, char* text);
void merge(unsigned int data_size, unsigned int* i_c, unsigned char str[], unsigned char merged[]);
void errorHandler(char* message);

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

    for(unsigned int i = 0; fds[i] != NULL; i++){
        unsigned char* compressed = enc(&sizes[i], raw_size[i], fds[i]);
        merge(sizes[i], &i_m, compressed, merged);
        free(compressed);
    }
    fwrite(merged, 1, i_m, stdout);
    fflush(stdout);
    free(merged);
    for (unsigned int i = 0; fds[i] != NULL; i++){
        munmap(fds[i], raw_size[i]);
    }
}

unsigned char* enc(unsigned int* data_size, unsigned int raw_size, char* text){
    unsigned char* compressed = malloc(raw_size*2);
    unsigned int i_c = 0;
    for(unsigned int i = 0; i < raw_size; i++){
        unsigned int count = 0;
        do{
            count++;
            i++;
        }while(text[i] == text[i-1]);
        compressed[i_c ++] = text[--i];
        compressed[i_c ++] = count;
        *data_size += 2;
    }
    return compressed;
}

void merge(unsigned int data_size, unsigned int* i_m, unsigned char str[], unsigned char merged[]){
    if(*i_m == 0 || (*i_m >= 2 && merged[*i_m - 2] != str[0]) ){
        for(unsigned int i = 0; i < data_size; i++){
            merged[(*i_m) ++] = str[i];
        }
    }else{
        (*i_m) --;
        unsigned int count = (unsigned int) merged[*i_m] + (unsigned int) str[1];
        merged[(*i_m) ++] = count;
        for(unsigned int i = 2; i < data_size; i++)
            merged[(*i_m) ++] = str[i];
    }
}

void errorHandler(char* message){
    fprintf(stderr, "%s", message);
    exit(EXIT_FAILURE);
}