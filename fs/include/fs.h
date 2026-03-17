#ifndef __FS_H__
#define __FS_H__

#include "common.h"
#include "inode.h"
#include <pthread.h>

// used for cmd_ls
typedef struct {
    short type;
    short uid;
    short mode;
    char name[MAXNAME];
    uint mtime;
    uint size;
    // ...
    // ...
    // Other fields can be added as needed
} entry;

typedef struct {  
    uint inum;
    char name[MAXNAME];
}dirent;

typedef struct {
    ushort uid;
    ushort islogin;
    uint pwd;
    int client_id;  
} client_session;
#define MAX_CLIENTS 16
extern client_session *sessions;  // 会话数组
extern pthread_mutex_t sessions_mutex;  // 会话互斥锁
extern pthread_mutex_t fs_mutex; // 文件系统互斥锁

void sbinit();

int cmd_f(int ncyl, int nsec, client_session *s);

int cmd_mk(char *name, short mode, client_session *s);
int cmd_mkdir(char *name, short mode, client_session *s);
int cmd_rm(char *name, client_session *s);
int cmd_rmdir(char *name, client_session *s);

int cmd_cd(char *name, client_session *s);
int cmd_ls(entry **entries, int *n, client_session *s);

int cmd_cat(char *name, uchar **buf, uint *len, client_session *s);
int cmd_w(char *name, uint len, const char *data, client_session *s);
int cmd_i(char *name, uint pos, uint len, const char *data, client_session *s);
int cmd_d(char *name, uint pos, uint len, client_session *s);

int cmd_login(int auid, client_session *s);

#endif