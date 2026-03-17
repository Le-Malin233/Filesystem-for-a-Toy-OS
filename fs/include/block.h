#ifndef __BLOCK_H__
#define __BLOCK_H__

#include "common.h"
#include "tcp_utils.h"
#include "time.h"

typedef struct {
    uint magic;      // Magic number, used to identify the file system
    uint size;       // Size in blocks
    uint bmapstart;  // Block number of first free map block
    uint inodes;      // number of inodes
    uint blocks;   // number of data blocks
    uint free;       // number of free blocks
    uint block_bitmap; // Block number of the block bitmap
    uint inode_bitmap; // Block number of the inode bitmap
    uint data_start; // Block number of the first data block
    uint inode_start; // Block number of the first inode block
} superblock;

#define CACHE_SIZE 16  
#define DIRTY 1       
#define CLEAN 0     

typedef struct {
    int block_num;    
    uchar data[BSIZE];
    time_t last_used; 
    int dirty;        // 1 if dirty, 0 if clean
} CacheBlock;

extern CacheBlock cache[CACHE_SIZE];

// sb is defined in block.c
extern superblock sb;
extern tcp_client bds_client;

void zero_block(uint bno);
uint allocate_block();
void free_block(uint bno);

void get_disk_info(int *ncyl, int *nsec);
void read_block(int blockno, uchar *buf);
void write_block(int blockno, uchar *buf);

int disk_init(const char *host, int port);

void init_cache();
void flush_cache();
#endif