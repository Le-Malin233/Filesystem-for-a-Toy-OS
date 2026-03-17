#include "inode.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "block.h"
#include "log.h"

inode *iget(uint inum) {
    if(inum<0||inum>sb.inodes) {
        Warn("Invalid inode number");
        return NULL;
    }
    uchar buf[BSIZE];
    uint blockno = IBLOCK(inum);
    read_block(blockno, buf);
    inode *ip = malloc(sizeof(inode));
    dinode *dip = (dinode *)buf+inum%IPB;
    if(dip->type==0) {
        Warn("Inode not allocated");
        free(ip);
        return NULL;
    }
    ip->inum = inum;
    ip->type = dip->type;
    ip->size = dip->size;
    ip->blocks = dip->blocks;
    ip->uid = dip->uid;
    ip->mode = dip->mode;
    ip->nlink = dip->nlink;
    ip->mtime = dip->mtime;
    memcpy(ip->addrs, dip->addrs, sizeof(ip->addrs));
    return ip;
}

void iput(inode *ip) { free(ip); }

inode *ialloc(short type) {
    uchar buf[BSIZE];
    for(int i=0;i<sb.inodes;i++) {
        uint blockno=IBLOCK(i);
        read_block(blockno, buf);
        dinode *dip = (dinode *)buf+i%IPB;
        if(dip->type==0) {
            memset(dip, 0, sizeof(dinode));
            dip->type = type;
            write_block(blockno, buf);
            inode *ip = malloc(sizeof(inode));
            ip->inum = i;
            ip->type = type;
            ip->size = 0;
            ip->blocks = 0;
            ip->uid = 0;
            ip->mode = 0b1111;
            ip->nlink = 1;
            ip->mtime = time(NULL);
            memset(ip->addrs, 0, sizeof(ip->addrs));
            return ip;
        }
    }
    Error("ialloc: no inodes");
    return NULL;
}

void iupdate(inode *ip) {
    uchar buf[BSIZE];
    uint blockno=IBLOCK(ip->inum);
    read_block(blockno, buf);
    dinode *dip = (dinode *)buf+ip->inum%IPB;
    dip->type = ip->type;
    dip->size = ip->size;
    dip->blocks = ip->blocks;
    dip->uid = ip->uid;
    dip->mode = ip->mode;
    dip->nlink = ip->nlink;
    dip->mtime = time(NULL);
    memcpy(dip->addrs, ip->addrs, sizeof(ip->addrs));
    write_block(blockno, buf);
}

//bmap for indirect blocks
int bmap(inode *ip, uint bn) {
    uchar buf[BSIZE];
    uint *addrs;
    uint addr;

    // Direct blocks
    if (bn < NDIRECT) {
        if (!ip->addrs[bn]) {
            ip->addrs[bn] = allocate_block();
        }
        return ip->addrs[bn];
    }

    // Single indirect blocks
    if (bn < NDIRECT + APB) {
        bn -= NDIRECT;
        if (!ip->addrs[NDIRECT]) {
            ip->addrs[NDIRECT] = allocate_block();
        }
        read_block(ip->addrs[NDIRECT], buf);
        addrs = (uint *)buf;
        if (!addrs[bn]) {
            addrs[bn] = allocate_block();
            write_block(ip->addrs[NDIRECT], buf);
        }
        return addrs[bn];
    }

    // Double indirect blocks
    if (bn < MAXFILEB) {
        bn -= NDIRECT + APB;
        uint a = bn / APB;  // First level index
        uint b = bn % APB;  // Second level index
        if (!ip->addrs[NDIRECT + 1]) {
            ip->addrs[NDIRECT + 1] = allocate_block();
        }
        read_block(ip->addrs[NDIRECT + 1], buf);
        addrs = (uint *)buf;
        if (!addrs[a]) {
            addrs[a] = allocate_block();
            write_block(ip->addrs[NDIRECT + 1], buf);
        }
        read_block(addrs[a], buf);
        addrs = (uint *)buf;
        if (!addrs[b]) {
            addrs[b] = allocate_block();
            write_block(addrs[a], buf);
        }
        return addrs[b];
    }

    Warn("bmap: block number %d too large (max is %d)", bn, MAXFILEB - 1);
    return 0;
}


int readi(inode *ip, uchar *dst, uint off, uint n) {
    uchar buf[BSIZE];
    if(off>ip->size || n<0) return -1;
    if(off+n>ip->size) n=ip->size-off;

    for(uint i=0,j;i<n;i+=j,off+=j,dst+=j) {
        uint bno=bmap(ip, off/BSIZE);
        if(bno<0){
            Warn("readi: bno=%d is invalid", bno);
            return -1;
        }
        Log("readi: bno=%d", bno);
        read_block(bno, buf);
        j=min(n-i, BSIZE-off%BSIZE);
        memcpy(dst, buf+off%BSIZE, j);
    }
    Log("readi: n=%d", n);
    return n;
}

int writei(inode *ip, uchar *src, uint off, uint n) {
    uchar buf[BSIZE];
    if(off>ip->size || n<0) return -1;
    if(off+n>MAXFILEB*BSIZE) return -1;

    for(uint i=0,j;i<n;i+=j,off+=j,src+=j) {
        uint bno=bmap(ip, off/BSIZE);
        if(bno<=0){
            Warn("writei: bno=%d is invalid", bno);
        }
        Log("writei: bno=%d", bno);
        read_block(bno, buf);
        j=min(n-i, BSIZE-off%BSIZE);
        memcpy(buf+off%BSIZE, src, j);
        write_block(bno, buf);
    }

    if(n>0&&off>ip->size) {
        ip->size=off;
        ip->blocks= max(1 + (off - 1) / BSIZE, ip->blocks);
    }

    iupdate(ip);
    Log("writei: n=%d", n);
    return n;
}
