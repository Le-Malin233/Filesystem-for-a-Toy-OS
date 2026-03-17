#ifndef __COMMAN_H__
#define __COMMAN_H__

typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;

// block size in bytes
#define BSIZE 512

// bits per block
#define BPB (BSIZE * 8)

// block of free map containing bit for block b
#define BBLOCK(b) ((b) / BPB + sb.bmapstart)

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

// max number of files in a directory
#define NCYL 1024
#define NSEC 63

#define MAGIC 0x12345678
// error codes
enum {
    E_SUCCESS = 0,
    E_ERROR = 1,
     E_CANNOT_WRITE = 2,
    E_CANNOT_DELETE = 3,
    E_CANNOT_MAKE = 4,
};

#endif
