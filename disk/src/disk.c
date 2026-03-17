#include "disk.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "log.h"

// global variables
int _ncyl, _nsec, _ttd;
int _lastcyl = 0;
const int BLOCK_SIZE = 512;
char* _diskfile;
int _fd;
int _filesize;

int init_disk(char *filename, int ncyl, int nsec, int ttd) {
    _ncyl = ncyl;
    _nsec = nsec;
    _ttd = ttd;
    // do some initialization...
    _filesize = BLOCK_SIZE * _nsec * _ncyl;
    // open file
    _fd = open(filename, O_RDWR | O_CREAT, 0);
    if (_fd < 0) {
        printf("Error: Could not open file '%s'.\n", filename);
        exit(-1);
    }

    // stretch the file 
    int result = lseek(_fd, _filesize-1, SEEK_SET);
    if (result == -1){
        perror ("Error calling lseek() to 'stretch' the file");
        close (_fd);
        exit(-1); 
    }
    
    result = write(_fd, "", 1);
    if (result != 1){
        perror("Error writing last byte of the file");
        close(_fd);
        exit(-1);
    }

    // mmap
    _diskfile = (char *) mmap(NULL, _filesize, PROT_READ | PROT_WRITE, MAP_SHARED, _fd, 0);
    if (_diskfile == MAP_FAILED){
        close(_fd);
        printf("Error: Could not map file.\n");
        exit(-1);
    }

    Log("Disk initialized: %s, %d Cylinders, %d Sectors per cylinder", filename, ncyl, nsec);
    return 0;
}

// all cmd functions return 0 on success
int cmd_i(int *ncyl, int *nsec) {
    // get the disk info
    *ncyl = _ncyl;
    *nsec = _nsec;
    return 0;
}

int cmd_r(int cyl, int sec, char *buf) {
    // read data from disk, store it in buf
    if (cyl >= _ncyl || sec >= _nsec || cyl < 0 || sec < 0) {
        Log("Invalid cylinder or sector");
        return 1;
    }
    usleep(_ttd * abs(cyl - _lastcyl)); 
    memcpy(buf, &_diskfile[BLOCK_SIZE * (cyl * _nsec + sec)], BLOCK_SIZE);
    _lastcyl = cyl;
    return 0;
}

int cmd_w(int cyl, int sec, int len, char *data) {
    // write data to disk
    if (cyl >= _ncyl || sec >= _nsec || cyl < 0 || sec < 0) {
        Log("Invalid cylinder or sector");
        return 1;
    }
    if (len > BLOCK_SIZE) {
        Log("Write length exceeds block size");
        return 1;
    }
    usleep(_ttd * abs(cyl - _lastcyl)); 
    if (len < BLOCK_SIZE) {
        memset(&_diskfile[BLOCK_SIZE * (cyl * _nsec + sec)], 0, BLOCK_SIZE);
    }
    memcpy(&_diskfile[BLOCK_SIZE * (cyl * _nsec + sec)], data, len);    
    _lastcyl = cyl;
    return 0;
}

void close_disk() {
    // close the file
    if (_diskfile != MAP_FAILED) {
        munmap(_diskfile, BLOCK_SIZE * _nsec * _ncyl);
    }
    if (_fd >= 0) {
        close(_fd);
    }
}
