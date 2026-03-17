#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "log.h"
#include "tcp_utils.h"
#include "fs.h"
#include "block.h"
#include "common.h"
#include <pthread.h>

int ncyl, nsec;
extern tcp_client bds_client;
int login[MAX_CLIENTS+1];  // 登录状态数组
// extern ushort uid;
// extern ushort islogin;
// extern uint pwd;

extern client_session *sessions;  // 会话数组
extern pthread_mutex_t sessions_mutex;  // 会话互斥锁
extern pthread_mutex_t fs_mutex; // 文件系统互斥锁

// 初始化会话系统
void init_sessions() {
    sessions = calloc(MAX_CLIENTS, sizeof(client_session));
    pthread_mutex_init(&sessions_mutex, NULL);
}

// 清理会话系统
void cleanup_sessions() {
    free(sessions);
    pthread_mutex_destroy(&sessions_mutex);
}

// 获取当前会话
client_session *get_session(int id) {
    pthread_mutex_lock(&sessions_mutex);
    client_session *s = &sessions[id % MAX_CLIENTS];
    pthread_mutex_unlock(&sessions_mutex);
    return s;
}

int handle_f(tcp_buffer *wb, char *args, int len, int client_id) {
    client_session *s = get_session(client_id);

    if(!s->islogin){
        reply_with_no(wb, "Please login first.\n", 20);
        return 0;
    }
    if(s->uid!=1){
        reply_with_no(wb, "You have no authority.\n", 23);
        return 0;
    }

    if (cmd_f(ncyl, nsec,s) == E_SUCCESS) {
        reply_with_yes(wb, NULL, 0);
    } else {
        reply_with_no(wb, NULL, 0);
    }
    return 0;
}

int handle_mk(tcp_buffer *wb, char *args, int len, int client_id) {
    client_session *s = get_session(client_id);
    if(!s->islogin){
        reply_with_no(wb, "Please login first.\n", 20);
        return 0;
    }

    char *name = args;
    short mode = 0;
    if (cmd_mk(name, mode,s) == E_SUCCESS) {
        reply_with_yes(wb, NULL, 0);
        return 0;
    } else if(cmd_mk(name, mode,s) == E_CANNOT_MAKE){
        reply_with_no(wb, "You have no authority to create file in this directory!\n", 56);
        return 0;
    } else {
        reply_with_no(wb, NULL, 0);
        return 0;
    }
}

int handle_mkdir(tcp_buffer *wb, char *args, int len, int client_id) {
    client_session *s = get_session(client_id);
    if(!s->islogin){
        reply_with_no(wb, "Please login first.\n", 20);
        return 0;
    }
    
    char *name = args;
    short mode = 0;
    if (cmd_mkdir(name, mode,s) == E_SUCCESS) {
        reply_with_yes(wb, NULL, 0);
        return 0;
    } if(cmd_mkdir(name, mode,s) == E_CANNOT_MAKE){
        reply_with_no(wb, "You have no authority to create directory in this directory!\n", 61);
        return 0;
    } else {
        reply_with_no(wb, NULL, 0);
        return 0;
    }
    return 0;
}

int handle_rm(tcp_buffer *wb, char *args, int len, int client_id) {
    client_session *s = get_session(client_id);
    if(!s->islogin){
        reply_with_no(wb, "Please login first.\n", 20);
        return 0;
    }
    
    char *name = args;
    if (cmd_rm(name,s) == E_SUCCESS) {
        reply_with_yes(wb, NULL, 0);
        return 0;
    } if(cmd_rm(name,s) == E_CANNOT_DELETE){
        reply_with_no(wb, "You have no authority to delete file in this directory!\n", 56);
        return 0;
    } else {
        reply_with_no(wb, NULL, 0);
        return 0;
    }
    return 0;
}

int handle_cd(tcp_buffer *wb, char *args, int len, int client_id) {
    client_session *s = get_session(client_id);
    if(!s->islogin){
        reply_with_no(wb, "Please login first.\n", 20);
        return 0;
    }
    
    char *name = args;
    if (cmd_cd(name,s) == E_SUCCESS) {
        reply_with_yes(wb, NULL, 0);
    } else {
        reply_with_no(wb, NULL, 0);
    }
    return 0;
}

int handle_rmdir(tcp_buffer *wb, char *args, int len, int client_id) {
    client_session *s = get_session(client_id);
    if(!s->islogin){
        reply_with_no(wb, "Please login first.\n", 20);
        return 0;
    }
    
    char *name = args;
    if (cmd_rmdir(name,s) == E_SUCCESS) {
        reply_with_yes(wb, NULL, 0);
        return 0;
    } if(cmd_rmdir(name,s) == E_CANNOT_DELETE){
        reply_with_no(wb, "You have no authority to delete directory in this directory!\n", 61);
        return 0;
    } else {
        reply_with_no(wb, NULL, 0);
        return 0;
    }
    return 0;
}

int handle_ls(tcp_buffer *wb, char *args, int len, int client_id) {
    client_session *s = get_session(client_id);
    if(!s->islogin){
        reply_with_no(wb, "Please login first.\n", 20);
        return 0;
    }
    
    entry *entries = NULL;
    int n = 0;
    if (cmd_ls(&entries, &n,s) != E_SUCCESS) {
        reply_with_no(wb, NULL, 0);
        return 0;
    }
    
    // Format the directory listing for network transmission
    char *buf = malloc(4096);
    if (!buf) {
        free(entries);
        reply_with_no(wb, NULL, 0);
        return 0;
    }
    
    char *p = buf;
    p += sprintf(p, "\nType\tOwner\tUpdate time\tSize\tName\n");
    
    for (int i = 0; i < n; i++) {
        time_t mtime = entries[i].mtime;
        struct tm *tmptr = localtime(&mtime);
        char str[100];
        strftime(str, sizeof(str), "%m-%d %H:%M", tmptr);

        short d = entries[i].type == T_DIR;
        short m = (d << 4) | entries[i].mode;
        char a[] = "drwr-";

        for (int j = 0; j <= 4; j++) {
            p += sprintf(p, "%c", m & (1 << (4 - j)) ? a[j] : '-');
        }

        p += sprintf(p, "\t%u\t%s\t%d\t", entries[i].uid, str, entries[i].size);
        p += sprintf(p, "%s\n", entries[i].name);
    }
    
    reply_with_yes(wb, buf, p - buf);
    free(buf);
    free(entries);
    return 0;
}

int handle_cat(tcp_buffer *wb, char *args, int len, int client_id) {
    client_session *s = get_session(client_id);
    if(!s->islogin){
        reply_with_no(wb, "Please login first.\n", 20);
        return 0;
    }
    
    char *name = args;
    uchar *buf = NULL;
    uint file_len;
    
    if (cmd_cat(name, &buf, &file_len,s) == E_SUCCESS) {
        char *response = malloc(file_len); //file content
        if (!response) {
            free(buf);
            reply_with_no(wb, NULL, 0);
            return 0;
        }
        
        memcpy(response, buf, file_len);
        reply_with_yes(wb, response, file_len);
        free(response);
        free(buf);
    } else {
        reply_with_no(wb, NULL, 0);
    }
    return 0;
}

int handle_w(tcp_buffer *wb, char *args, int len, int client_id) {
    client_session *s = get_session(client_id);
    if(!s->islogin){
        reply_with_no(wb, "Please login first.\n", 20);
        return 0;
    }
    
    char name[MAXNAME];
    uint data_len;
    char *data_start;
    
    char *p = args;
    while (*p && *p != ' ') p++; 
    if (!*p) {
        reply_with_no(wb, NULL, 0);
        return 0;
    }
    *p = '\0'; 
    strncpy(name, args, MAXNAME);
    *p = ' '; 
    
    p++; 
    char *len_str = p;
    while (*p && *p != ' ') p++; 
    if (!*p) {
        reply_with_no(wb, NULL, 0);
        return 0;
    }
    *p = '\0';
    data_len = atoi(len_str);
    *p = ' ';
    
    p++;
    data_start = p;
    
    if (strlen(data_start) > MAXFILEB - 1) {
        reply_with_no(wb, NULL, 0);
        return 0;
    }
    
    if (cmd_w(name, data_len, data_start,s) == E_SUCCESS) {
        reply_with_yes(wb, NULL, 0);
        return 0;
    } if(cmd_w(name, data_len, data_start,s) == E_CANNOT_WRITE){
        reply_with_no(wb, "You have no authority to write file in this directory!\n", 55);
        return 0;
    } else {
        reply_with_no(wb, NULL, 0);
        return 0;
    }
    return 0;
}

int handle_i(tcp_buffer *wb, char *args, int len, int client_id) {
    client_session *s = get_session(client_id);
    if(!s->islogin){
        reply_with_no(wb, "Please login first.\n", 20);
        return 0;
    }
    
    char name[MAXNAME];
    uint pos;
    uint data_len;
        char *data_start;

    char *p = args;
    while (*p && *p != ' ') p++; 
    if (!*p) {
        reply_with_no(wb, NULL, 0);
        return 0;
    }
    *p = '\0';
    strncpy(name, args, MAXNAME);
    *p = ' '; 

    p++;
    char *pos_str = p;
    while (*p && *p != ' ') p++;
    if (!*p) {
        reply_with_no(wb, NULL, 0);
        return 0;
    }
    *p = '\0'; 
    pos = atoi(pos_str);
    *p = ' '; 

    p++; 
    char *len_str = p;
    while (*p && *p != ' ') p++;
    if (!*p) {
        reply_with_no(wb, NULL, 0);
        return 0;
    }
    *p = '\0';
    data_len = atoi(len_str);
    *p = ' ';

    p++;
    data_start = p;


    if (strlen(data_start) > MAXFILEB - 1) {
        reply_with_no(wb, NULL, 0);
        return 0;
    }

    if (cmd_i(name, pos, data_len, data_start,s) == E_SUCCESS) {
        reply_with_yes(wb, NULL, 0);
        return 0;
    } if(cmd_i(name, pos, data_len, data_start,s) == E_CANNOT_WRITE){
        reply_with_no(wb, "You have no authority to write file in this directory!\n", 55);
        return 0;
    } else {
        reply_with_no(wb, NULL, 0);
        return 0;
    }
    return 0;
}

int handle_d(tcp_buffer *wb, char *args, int len, int client_id) {
    client_session *s = get_session(client_id);
    if(!s->islogin){
        reply_with_no(wb, "Please login first.\n", 20);
        return 0;
    }
    
    char name[MAXNAME];
    uint pos;
    uint del_len;
    
    if (sscanf(args, "%s %u %u", name, &pos, &del_len) != 3) {
        reply_with_no(wb, NULL, 0);
        return 0;
    }
    
    if (cmd_d(name, pos, del_len,s) == E_SUCCESS) {
        reply_with_yes(wb, NULL, 0);
        return 0;
    } if(cmd_d(name, pos, del_len,s) == E_CANNOT_WRITE){
        reply_with_no(wb, "You have no authority to write file in this directory!\n", 55);
        return 0;
    } else {
        reply_with_no(wb, NULL, 0);
        return 0;
    }
    return 0;
}

int handle_login(tcp_buffer *wb, char *args, int len, int client_id) {
    int auid;
    if (sscanf(args, "%d", &auid) != 1) {
        reply_with_no(wb, NULL, 0);
        return 0;
    }

    client_session *s = get_session(client_id);
    if(login[auid]) {
        char *msg = malloc(64);
        sprintf(msg, "User %d has logged in!\n", auid);
        reply_with_no(wb, msg, strlen(msg));
        free(msg);
        return 0;
    }
    if (cmd_login(auid,s) == E_SUCCESS) {
            char *msg = malloc(64);
            login[auid] = 1;  // Mark user as logged in
            sprintf(msg, "Hello, uid=%d!\n", s->uid);
            reply_with_yes(wb, msg, strlen(msg));
            free(msg);
    } else {
        reply_with_no(wb, NULL, 0);
    }
    return 0;
}

int handle_e(tcp_buffer *wb, char *args, int len, int client_id) {
    const char *msg = "Bye!\n";
    reply_with_yes(wb, msg, strlen(msg) + 1);
    return -1;
}

static struct {
    const char *name;
    int (*handler)(tcp_buffer *wb, char *, int, int);
} cmd_table[] = {
    {"f", handle_f},
    {"mk", handle_mk},
    {"mkdir", handle_mkdir},
    {"rm", handle_rm},
    {"cd", handle_cd},
    {"rmdir", handle_rmdir},
    {"ls", handle_ls},
    {"cat", handle_cat},
    {"w", handle_w},
    {"i", handle_i},
    {"d", handle_d},
    {"login", handle_login},
    {"e", handle_e}
};

#define NCMD (sizeof(cmd_table) / sizeof(cmd_table[0]))

void on_connection(int id) {
    sbinit();
    client_session *s = get_session(id);
    s->uid = 0;
    s->islogin = 0;
    s->pwd = 0;
    s->client_id = id;
}

int on_recv(int id, tcp_buffer *wb, char *msg, int len) {
    //client_session *s = get_session(id);
    msg[len] = '\0';
    for (int i = 0; i < len; i++) {
        Log("on_recv: msg[%d] = %c", i, msg[i]);
    }

    char *p = strtok(msg, " \r\n");
    int ret = 1;
    for (int i = 0; i < NCMD; i++) {
        if (p && strcmp(p, cmd_table[i].name) == 0) {
            ret = cmd_table[i].handler(wb, p + strlen(p) + 1, len - strlen(p) - 1, id);
            break;
        }
    }
    
    if (ret == 1) {
        reply_with_no(wb, NULL, 0);
    }
    
    if (ret < 0) {
        return -1;
    }
    return 0;
}

void cleanup(int id) {
    client_session *s = get_session(id);
    login[s->uid] = 0;// Mark user as logged out
    s->uid = 0;
    s->islogin = 0;
    s->pwd = 0;
      
     flush_cache();
}

FILE *log_file;

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <DiskServerAddress> <DiskPort> <FSPort>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    log_init("fs.log");

    char *bds_host = argv[1];
    int bds_port = atoi(argv[2]);
    int fs_port = atoi(argv[3]);

    init_sessions();
    pthread_mutex_init(&fs_mutex, NULL);

    disk_init(bds_host, bds_port);
    Log("Disk server initialized: %s:%d", bds_host, bds_port);

    // Get disk info and store in global variables
    get_disk_info(&ncyl, &nsec);
    Log("Disk info: %d cylinders, %d sectors", ncyl, nsec);

    // Read the superblock
    sbinit();
    Log("Superblock initialized");

    // Initialize cache
    init_cache();
    Log("Cache initialized");

    // Initialize TCP server
    tcp_server server = server_init(fs_port, MAX_CLIENTS, on_connection, on_recv, cleanup);
    Log("File system server running on port %d", fs_port);
    server_run(server);



    // Cleanup (normally unreachable)
    client_destroy(bds_client);
    cleanup_sessions();
    pthread_mutex_destroy(&fs_mutex);
    log_close();
    return 0;
}