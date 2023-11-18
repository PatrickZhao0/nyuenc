#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

void enc(unsigned int* data_size, char* text, unsigned char compressed[]){
    unsigned int i_c = 0;
    for(unsigned int i = 0; text[i] != '\0'; i++){
        unsigned int count = 0;
        do{
            count++;
            i++;
        }while(text[i] == text[i-1]);
        compressed[i_c ++] = text[--i];
        compressed[i_c ++] = count;
        data_size += 2;
    }
}

void merge(unsigned int data_size, unsigned int* i_c, char str[], char merged[]){
    if(*i_c == 0 || merged[*i_c - 2] != str[0]){
        for(unsigned int i = 0; i < data_size; i++)
            merged[(*i_c) ++] = str[i];
    }else{
        (*i_c) --;
        unsigned int count = (unsigned int) merged[*i_c] + (unsigned int) str[1];
        merged[(*i_c) ++] = count;
        for(unsigned int i = 2; i < data_size; i++)
            merged[(*i_c) ++] = str[i];
    }
}

int main(){
    char merged[10];
    enc();
}