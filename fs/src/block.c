#include "block.h"

#include <string.h>
#include <stdlib.h>
#include "common.h"
#include "log.h"
#include "time.h"

superblock sb;
tcp_client bds_client; 
CacheBlock cache[CACHE_SIZE];

#define MAXBLOCKS 1024
//static uchar diskfile[NCYL * NSEC][BSIZE];

void zero_block(uint bno) {
    uchar buf[BSIZE];
    memset(buf, 0, BSIZE);
    write_block(bno, buf);
}

uint allocate_block() {
    for (int i = 0; i < sb.size; i += BPB) {
        uchar buf[BSIZE];
        read_block(BBLOCK(i), buf);
        for (int j = 0; j < BPB; j++) {
            uint blockno = i + j;
            if (blockno >= sb.size) {
                break;
            }
            int m = 1 << (j % 8);
            if ((buf[j / 8] & m) == 0) {
                buf[j / 8] |= m;
                write_block(BBLOCK(i), buf);
                zero_block(blockno);
                return blockno;
            }
        }
    }
    Warn("allocate_block: Out of blocks");
    return 0;
}

void free_block(uint bno) {
    if(bno < sb.data_start || bno >= sb.size) {
        Warn("Invalid block number");
        return;
    }
    uchar buf[BSIZE];
    read_block(BBLOCK(bno), buf);
    int i = bno % BPB;
    int m = 1 << (i % 8);
    if ((buf[i / 8] & m) == 0) Warn("block is free");
    buf[i / 8] &= ~m;
    write_block(BBLOCK(bno), buf);
}

    void get_disk_info(int *ncyl, int *nsec) {
        char cmd[] = "I";
        char resp[64];
        
        client_send(bds_client, cmd, strlen(cmd)+1);
        client_recv(bds_client, resp, sizeof(resp));
        sscanf(resp, "%d %d", ncyl, nsec);
    }

// void get_disk_info(int *ncyl, int *nsec) {
//     *ncyl = NCYL;
//     *nsec = NSEC;
// }

// void read_block(int blockno, uchar *buf) {
//     memcpy(buf, diskfile[blockno], BSIZE);
// }

// void write_block(int blockno, uchar *buf) {
//     memcpy(diskfile[blockno], buf, BSIZE);
// }


//------------------------------------------

// void read_block(int blockno, uchar *buf) {
//     char cmd[64];
//     int cyl = blockno / NSEC;
//     int sec = blockno % NSEC;
    
//     sprintf(cmd, "R %d %d", cyl, sec);
//     client_send(bds_client, cmd, strlen(cmd)+1);
//     client_recv(bds_client, (char *)buf, BSIZE);
// }

// void write_block(int blockno, uchar *buf) {
//     char cmd[600];
//     int cyl = blockno / NSEC;
//     int sec = blockno % NSEC;
    
//     sprintf(cmd, "W %d %d %d ", cyl, sec, BSIZE);
//     memcpy(cmd + strlen(cmd), buf, BSIZE);
//     client_send(bds_client, cmd, strlen(cmd)+BSIZE);
    
//     char resp[4];
//     client_recv(bds_client, resp, sizeof(resp));
// }

int disk_init(const char *host, int port) {
    bds_client = client_init(host, port);
    return 0;
}

void init_cache() {
    for (int i = 0; i < CACHE_SIZE; i++) {
        cache[i].block_num = -1;
        cache[i].last_used = 0;
        cache[i].dirty = CLEAN;
    }
}

int find_cache_block(int block_num) {
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (cache[i].block_num == block_num) {
            cache[i].last_used = time(NULL);  // 更新最近使用时间
            return i; 
        }
    }
    return -1; 
}

// LRU
int replace_cache_block() {
    int lru_index = 0;
    time_t lru_time = cache[0].last_used;

    for (int i = 1; i < CACHE_SIZE; i++) {
        if (cache[i].last_used < lru_time) {
            lru_time = cache[i].last_used;
            lru_index = i;
        }
    }
    // 如果替换的块是脏块，先将其写回磁盘
    if (cache[lru_index].dirty == DIRTY) {
        char cmd[600];
        int cyl = cache[lru_index].block_num / NSEC;
        int sec = cache[lru_index].block_num % NSEC;
        
        sprintf(cmd, "W %d %d %d ", cyl, sec, BSIZE);
        memcpy(cmd + strlen(cmd), cache[lru_index].data, BSIZE);
        client_send(bds_client, cmd, strlen(cmd) + BSIZE);
        
        char resp[4];
        client_recv(bds_client, resp, sizeof(resp));
        Log("Cache flush: block %d written to disk", cache[lru_index].block_num);
    }

    return lru_index;  // 返回最近最少使用的块的索引
}

void write_block(int blockno, uchar *buf) {
    int cache_index = find_cache_block(blockno);

    if (cache_index != -1) {
        // Cache hit
        memcpy(cache[cache_index].data, buf, BSIZE);
        cache[cache_index].dirty = DIRTY;  // 标记为脏块
        cache[cache_index].last_used = time(NULL);
        Log("write_block_with_cache: Cache hit for block %d", blockno);
    } else {
        // Cache miss
        int replace_index = replace_cache_block();
        Log("write_block_with_cache: Cache miss for block %d, replacing block %d", blockno, cache[replace_index].block_num);

        // 将新数据写入缓存
        cache[replace_index].block_num = blockno;
        memcpy(cache[replace_index].data, buf, BSIZE);
        cache[replace_index].dirty = DIRTY;  // 标记为脏块
        cache[replace_index].last_used = time(NULL);
    }
}

void read_block(int blockno, uchar *buf) {
    int cache_index = find_cache_block(blockno);

    if (cache_index != -1) {
        // Cache hit
        memcpy(buf, cache[cache_index].data, BSIZE);
        cache[cache_index].last_used = time(NULL);
        Log("read_block_with_cache: Cache hit for block %d", blockno);
    } else {
        // Cache miss
        int replace_index = replace_cache_block();
        Log("read_block_with_cache: Cache miss for block %d, replacing block %d", blockno, cache[replace_index].block_num);

        // 从磁盘读取数据
        char cmd[64];
        int cyl = blockno / NSEC;
        int sec = blockno % NSEC;
        
        sprintf(cmd, "R %d %d", cyl, sec);
        client_send(bds_client, cmd, strlen(cmd)+1);
        client_recv(bds_client, (char *)buf, BSIZE);
        
        // 将新数据写入缓存
        cache[replace_index].block_num = blockno;
        memcpy(cache[replace_index].data, buf, BSIZE);
        cache[replace_index].dirty = CLEAN;  // 标记为干净块
        cache[replace_index].last_used = time(NULL);
    }
}

void flush_cache() {
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (cache[i].block_num != -1 && cache[i].dirty == DIRTY) {
            char *cmd=malloc(BSIZE+1024);
            int cyl = cache[i].block_num / NSEC;
            int sec = cache[i].block_num % NSEC;

            sprintf(cmd, "W %d %d %d ", cyl, sec, BSIZE);
            memcpy(cmd + strlen(cmd), cache[i].data, BSIZE);
            client_send(bds_client, cmd, strlen(cmd)+BSIZE);

            char resp[4];
            client_recv(bds_client, resp, sizeof(resp));
            Log("Cache flush: block %d written to disk", cache[i].block_num);

            cache[i].dirty = CLEAN;
        }
    }
}