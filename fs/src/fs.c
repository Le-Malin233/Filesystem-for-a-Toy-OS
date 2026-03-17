#include "fs.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "block.h"
#include "log.h"

int fsize;
int nblocks;
int ninodesblocks = (NINODES / IPB) + 1;
int nbitmap;
int meta;

// uint pwd;
// ushort uid;
// ushort islogin;

client_session *sessions;  // 会话数组
pthread_mutex_t sessions_mutex;  // 会话互斥锁
pthread_mutex_t fs_mutex; // 文件系统互斥锁


void sbinit() {
    uchar buf[BSIZE];
    read_block(0, buf);
    memcpy(&sb, buf, sizeof(sb));
}

int icreate(short type, char *name, uint pinum, ushort uid, ushort perm) {
    // Allocate new inode
    inode *ip = ialloc(type);
    if (ip == NULL) {
        Error("icreate: failed to allocate inode");
        return -1;
    }
    // Initialize inode properties
    ip->size = 0;
    ip->blocks = 0;
    ip->type = type;
    ip->uid = uid;
    Log("icreate: uid=%d", uid);
    uint inum = ip->inum;

    // Special handling for directories
    if (type == T_DIR) {
        // Create "." and ".." entries
        dirent entries[2] = {
            {.inum = inum, .name = "."},
            {.inum = pinum, .name = ".."}
        };
        
        // Write directory entries
        if (writei(ip, (uchar *)entries, ip->size, sizeof(entries)) == sizeof(entries)) {
            Log("icreate: Create directory %s with inum %d", name, inum);
        }else{
        Error("icreate: failed to write directory entries");
            free(ip);
            return E_ERROR;
        }
    }

    // Update inode on disk
    iupdate(ip);

    Log("icreate: Create %s inode %d, inside directory inode %d",
        type == T_DIR ? "dir" : "file", inum, pinum);

    // Add to parent directory if not root
    if (pinum != inum) {
        inode *parent_ip = iget(pinum);
        if (parent_ip == NULL) {
            Error("icreate: failed to get parent directory");
            free(ip);
            return -1;
        }

        // Create directory entry
        dirent de = {
            .inum = inum,
            .name = ""
        };
        strncpy(de.name, name, MAXNAME);

        // Add to parent directory
        if (writei(parent_ip, (uchar *)&de, parent_ip->size, sizeof(de)) == sizeof(de)) {                 
            iupdate(parent_ip);
            Log("icreate: Add %s to parent directory %d", name, pinum);
        }else{
            Error("icreate: failed to add to parent directory");
            free(parent_ip);
            free(ip);
            return E_ERROR;
        }

        iupdate(ip);

        free(parent_ip);
    }

    free(ip);
    return E_SUCCESS;
}


int cmd_f(int ncyl, int nsec, client_session *s) {
    pthread_mutex_lock(&fs_mutex);
    Log("cmd_f: ncyl=%d nsec=%d", ncyl, nsec);
    uchar buf[BSIZE];

    // calculate args and write superblock
    fsize = ncyl * nsec;
    Log("ncyl=%d nsec=%d fsize=%d", ncyl, nsec, fsize);
    nbitmap = (fsize / BPB) + 1;
    meta = 1 + ninodesblocks + nbitmap;
    nblocks = fsize - meta;
    Log("ninodeblocks=%d nbitmap=%d nblocks=%d", fsize, meta, nblocks);

    sb.magic = MAGIC;
    sb.size = fsize;
    sb.blocks = fsize - meta;
    sb.inodes = NINODES;
    sb.inode_start = 1;  // 0 for superblock
    sb.bmapstart = 1 + ninodesblocks;
    Log("sb: magic=0x%x size=%d nblocks=%d ninodes=%d inodestart=%d "
        "bmapstart=%d",
        sb.magic, sb.size, sb.blocks, sb.inodes, sb.inode_start, sb.bmapstart);

    memset(buf, 0, BSIZE);
    memcpy(buf, &sb, sizeof(sb));
    write_block(0, buf);

    // bitmap init
    memset(buf, 0, BSIZE);
    for (int i = 0; i < sb.size; i += BPB) write_block(BBLOCK(i), buf);
    for (int i = 0; i < NINODES; i += IPB) write_block(IBLOCK(i), buf);

    // mark meta blocks as in use
    for (int i = 0; i < meta; i += BPB) {
        memset(buf, 0, BSIZE);
        for (int j = 0; j < BPB; j++)
            if (i + j < meta) buf[j / 8] |= 1 << (j % 8);
        write_block(BBLOCK(i), buf);
    }
    s->pwd = 0;
    // make root dir
    if (!icreate(T_DIR, NULL, 0, s->uid, 0b1111)) {
        //printf("Done\n");
        Log("cmd_f: Success");
    }

    icreate(T_DIR, "1", s->pwd, s->uid, 0);
    pthread_mutex_unlock(&fs_mutex);
    return E_SUCCESS;
}

uint findinum(char *name, client_session *s) {
    inode *ip = iget(s->pwd);
    if (!ip) {
        Error("findinum: failed to get inode for pwd=%d", s->pwd);
        return NINODES;
    }
    uchar *buf = malloc(ip->size);
    if (!buf) {
        Error("findinum: malloc failed");
        free(ip);
        return NINODES;
    }
    readi(ip, buf, 0, ip->size);
    dirent *de = (dirent *)buf;

    int result = NINODES;
    int nfile = ip->size / sizeof(dirent);
    for (int i = 0; i < nfile; i++) {
        if (de[i].inum == NINODES) continue;  // deleted
        if (strcmp(de[i].name, name) == 0) {
            result = de[i].inum;
            break;
        }
    }
    free(buf);
    free(ip);
    return result;
}

int cmd_mk(char *name, short mode, client_session *s) {
    pthread_mutex_lock(&fs_mutex);
    Log("cmd_mk: name=%s, pwd=%d", name, s->pwd);
    if (findinum(name,s) != NINODES) {
        printf("The same name already exists!\n");
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }

    inode *ip = iget(s->pwd);

    if (s->uid != ip->uid) {
        printf("You have no authority to create file in this directory!\n");
        pthread_mutex_unlock(&fs_mutex);
        return E_CANNOT_MAKE;
    }

    if (icreate(T_FILE, name, s->pwd, s->uid, mode) == 0){
        Log("cmd_mk: Create file %s", name);
        pthread_mutex_unlock(&fs_mutex);
        return E_SUCCESS;
    }
    pthread_mutex_unlock(&fs_mutex);
    return E_ERROR;
}

int cmd_mkdir(char *name, short mode, client_session *s) {
    pthread_mutex_lock(&fs_mutex);
    Log("cmd_mkdir: name=%s, pwd=%d", name, s->pwd);
    if (findinum(name,s) != NINODES) {
        printf("The same name already exists!\n");
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }

    inode *ip = iget(s->pwd);

    if (s->uid != ip->uid) {
        printf("You have no authority to create file in this directory!\n");
        pthread_mutex_unlock(&fs_mutex);
        return E_CANNOT_MAKE;
    }

    if (icreate(T_DIR, name, s->pwd, s->uid, mode) == 0){
        Log("cmd_mkdir: Create directory %s", name);
        pthread_mutex_unlock(&fs_mutex);
        return E_SUCCESS;
    }
    pthread_mutex_unlock(&fs_mutex);
    return E_ERROR;
}

int cmd_rm(char *name, client_session *s) {
    pthread_mutex_lock(&fs_mutex);
    Log("cmd_rm: name=%s", name);
    //find the file
    uint inum = findinum(name,s);
    if (inum == NINODES) {
        printf("File not found: %s\n", name);
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }
    //get inode
    inode *ip = iget(inum);
    if (ip == NULL) {
        Error("cmd_rm: failed to get inode for %s (inum=%d)", name, inum);
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }
    //check if it is a directory
    if (ip->type == T_DIR) {
        printf("Cannot remove directory with rm: %s\n", name);
        free(ip);
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }

    if (s->uid != ip->uid) {
        printf("You have no authority to remove this file!\n");
        free(ip);
        pthread_mutex_unlock(&fs_mutex);
        return E_CANNOT_DELETE;
    }

    //get parent directory inode
    inode *parent_ip = iget(s->pwd);
    if (parent_ip == NULL) {
        Error("cmd_rm: failed to get parent directory (pwd=%d)", s->pwd);
        free(ip);
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }
    //delete the file
    uchar *buf = malloc(parent_ip->size);
    readi(parent_ip, buf, 0, parent_ip->size);
    dirent *de = (dirent *)buf;
    int found = 0;
    int entry_index = -1;
    for (int i = 0; i < parent_ip->size / sizeof(dirent); i++) {
        if (de[i].inum == inum && strcmp(de[i].name, name) == 0) {
            de[i].inum = NINODES;  // mark as deleted
            found = 1;
            entry_index = i;
            break;
        }
    }
    if (!found) {
        Error("cmd_rm: entry not found in parent directory");
        free(buf);
        free(parent_ip);
        free(ip);
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }
    
    uint new_parent_size = parent_ip->size;
    if (entry_index == (parent_ip->size / sizeof(dirent)) - 1) {
        new_parent_size -= sizeof(dirent);
    }
    // write back to parent directory
    if (writei(parent_ip, buf, 0, parent_ip->size) != parent_ip->size) {
        Error("cmd_rm: failed to update parent directory");
        free(buf);
        free(parent_ip);
        free(ip);
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }

    parent_ip->size = new_parent_size;
    iupdate(parent_ip);

    free(buf);
    free(parent_ip);
    free(ip);

    Log("Removed file: %s", name);
    pthread_mutex_unlock(&fs_mutex);
    return E_SUCCESS;
}

int cmd_rmdir(char *name, client_session *s) {
    pthread_mutex_lock(&fs_mutex);
    Log("cmd_rmdir: name=%s", name);
    //find the directory
    uint inum = findinum(name,s);
    if (inum == NINODES) {
        printf("Directory not found: %s\n", name);
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }
    //get inode
    inode *ip = iget(inum);
    if (ip == NULL) {
        Error("cmd_rmdir: failed to get inode for %s (inum=%d)", name, inum);
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }
    //check if it is a file
    if (ip->type != T_DIR) {
        printf("Not a directory: %s\n", name);
        free(ip);
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }

    if (s->uid != ip->uid) {
        printf("You have no authority to remove this file!\n");
        free(ip);
        pthread_mutex_unlock(&fs_mutex);
        return E_CANNOT_DELETE;
    }

    //check if it is empty
    uchar *dir_buf = malloc(ip->size);
    readi(ip, dir_buf, 0, ip->size);
    dirent *de = (dirent *)dir_buf;
    int empty = 1;
    for (int i = 0; i < ip->size / sizeof(dirent); i++) {
        if (de[i].inum != NINODES && 
            strcmp(de[i].name, ".") != 0 && 
            strcmp(de[i].name, "..") != 0) {
            empty = 0;
            break;
        }
    }
    if (!empty) {
        printf("Directory not empty: %s\n", name);
        free(dir_buf);
        free(ip);
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }
    //get parent directory inode
    inode *parent_ip = iget(s->pwd);
    if (parent_ip == NULL) {
        Error("cmd_rmdir: failed to get parent directory (pwd=%d)", s->pwd);
        free(dir_buf);
        free(ip);
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }
    //delete the directory
    uchar *parent_buf = malloc(parent_ip->size);
    readi(parent_ip, parent_buf, 0, parent_ip->size);
    dirent *parent_de = (dirent *)parent_buf;
    int found = 0;
    int entry_index = -1;
    for (int i = 0; i < parent_ip->size / sizeof(dirent); i++) {
        if (parent_de[i].inum == inum && strcmp(parent_de[i].name, name) == 0) {
            parent_de[i].inum = NINODES;  // mark as deleted
            entry_index = i;
            found = 1;
            break;
        }
    }
    if (!found) {
        Error("cmd_rmdir: entry not found in parent directory");
        free(parent_buf);
        free(dir_buf);
        free(parent_ip);
        free(ip);
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }

    uint new_parent_size = parent_ip->size;
    if (entry_index == (parent_ip->size / sizeof(dirent)) - 1) {
        new_parent_size -= sizeof(dirent);
    }

    // write back to parent directory
    if (writei(parent_ip, parent_buf, 0, parent_ip->size) != parent_ip->size) {
        Error("cmd_rmdir: failed to update parent directory");
        free(parent_buf);
        free(dir_buf);
        free(parent_ip);
        free(ip);
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }

    parent_ip->size = new_parent_size;
    iupdate(parent_ip);
    free(parent_buf);
    free(dir_buf);
    free(parent_ip);
    free(ip);

    Log("Removed directory: %s", name);
    pthread_mutex_unlock(&fs_mutex);
    return E_SUCCESS;
}

int cmd_cd(char *path, client_session *s) {
    pthread_mutex_lock(&fs_mutex);
    Log("cmd_cd: path=%s", path);
    if (path == NULL || path[0] == '\0') {
        Error("cd: path is empty");
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }

    uint original_pwd = s->pwd;
    int ret = E_SUCCESS;

    //absolute path
    if (path[0] == '/') {
        s->pwd = 0; 
        path++;  
    }

    char *token = strtok(path, "/");
    while (token != NULL) {
        if (strcmp(token, ".") == 0) {
            token = strtok(NULL, "/");
            continue;
        }

        if (strcmp(token, "..") == 0) {
            inode *current_ip = iget(s->pwd);
            if (current_ip == NULL) {
                Error("cd: failed to get current directory inode");
                ret = E_ERROR;
                break;
            }

            uchar *buf = malloc(current_ip->size);
            readi(current_ip, buf, 0, current_ip->size);
            dirent *de = (dirent *)buf;

            uint parent_inum = NINODES;
            for (int i = 0; i < current_ip->size / sizeof(dirent); i++) {
                if (strcmp(de[i].name, "..") == 0) {
                    parent_inum = de[i].inum;
                    break;
                }
            }

            free(buf);
            free(current_ip);

            if (parent_inum == NINODES) {
                Error("cd: failed to find parent directory");
                ret = E_ERROR;
                break;
            }

            s->pwd = parent_inum;
            Log("cd: changed to parent directory (pwd=%d)", s->pwd);
            token = strtok(NULL, "/");
            continue;
        }


        uint target_inum = findinum(token,s);
        if (target_inum == NINODES) {
            Error("cd: directory '%s' not found", token);
            ret = E_ERROR;
            break;
        }

        inode *target_ip = iget(target_inum);
        if (target_ip == NULL) {
            Error("cd: failed to get target inode");
            ret = E_ERROR;
            break;
        }

        if (target_ip->type != T_DIR) {
            Error("cd: '%s' is not a directory", token);
            free(target_ip);
            ret = E_ERROR;
            break;
        }

        s->pwd = target_inum;
        free(target_ip);
        token = strtok(NULL, "/");
    }

    if (ret == E_ERROR) {
        s->pwd = original_pwd;
    } else {
        Log("cd: changed to directory (inum=%d)", s->pwd);
    }

    pthread_mutex_unlock(&fs_mutex);
    return ret;
}


int cmp_ls(const void *a, const void *b) {
    const entry *ea = (const entry *)a;
    const entry *eb = (const entry *)b;
    return strcmp(ea->name, eb->name);
}

int cmd_ls(entry **entries, int *n, client_session *s) {
    pthread_mutex_lock(&fs_mutex);
    Log("cmd_ls: pwd=%d", s->pwd);
    inode *ip = iget(s->pwd);
    if (ip == NULL) {
        Error("cmd_ls: failed to get inode for pwd=%d", s->pwd);
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }

    uchar *buf = malloc(ip->size);
    if (buf == NULL) {
        Error("cmd_ls: failed to allocate buffer");
        free(ip);
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }

    readi(ip, buf, 0, ip->size);
    dirent *de = (dirent *)buf;

    int nfile = 0;
    for (int i = 0; i < ip->size / sizeof(dirent); i++) {
        if (de[i].inum == NINODES) continue;
        if (strcmp(de[i].name, ".") == 0 || strcmp(de[i].name, "..") == 0) continue;
        nfile++;
    }

    *entries = malloc(nfile * sizeof(entry));
    if (*entries == NULL) {
        Error("cmd_ls: failed to allocate entries");
        free(buf);
        free(ip);
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }

    int count = 0;
    for (int i = 0; i < ip->size / sizeof(dirent); i++) {
        if (de[i].inum == NINODES) continue;
        if (strcmp(de[i].name, ".") == 0 || strcmp(de[i].name, "..") == 0) continue;

        inode *sub = iget(de[i].inum);
        if (sub == NULL) {
            Warn("cmd_ls: failed to get inode for %s (inum=%d)", de[i].name, de[i].inum);
            continue;
        }

        (*entries)[count].type = sub->type;
        strncpy((*entries)[count].name, de[i].name, MAXNAME);
        (*entries)[count].name[MAXNAME - 1] = '\0';
        (*entries)[count].mtime = sub->mtime;
        (*entries)[count].uid = sub->uid;
        (*entries)[count].mode = sub->mode;
        (*entries)[count].size = sub->size;
        count++;

        free(sub);
    }

    qsort(*entries, count, sizeof(entry), cmp_ls);
    static char str[100];
    static char logbuf[4096], *logtmp;
    printf("\33[1mType \tOwner\tUpdate time\tSize\tName\033[0m\n");

    logtmp = logbuf;
    logtmp += sprintf(logtmp, "List files\nType \tOwner\tUpdate time\tSize\tName\n");
    for (int i = 0; i < count; i++) {
        time_t mtime = (*entries)[i].mtime;
        struct tm *tmptr = localtime(&mtime);
        strftime(str, sizeof(str), "%m-%d %H:%M", tmptr);

        short d = (*entries)[i].type == T_DIR;
        short m = (d << 4) | (*entries)[i].mode;
        static char a[] = "drwr-";

        for (int j = 0; j <= 4; j++) {
            printf("%c", m & (1 << (4 - j)) ? a[j] : '-');
            logtmp += sprintf(logtmp, "%c", m & (1 << (4 - j)) ? a[j] : '-');
        }

        printf("\t%u\t%s\t%d\t", (*entries)[i].uid, str, (*entries)[i].size);
        printf(d ? "\033[34m\33[1m%s\033[0m\n" : "%s\n", (*entries)[i].name);
        logtmp += sprintf(logtmp, "\t%u\t%s\t%d\t%s\n", (*entries)[i].uid, str,
                         (*entries)[i].size, (*entries)[i].name);
    }

    Log("%s", logbuf);
    free(buf);
    free(ip);
    *n = count;
    pthread_mutex_unlock(&fs_mutex);
    return E_SUCCESS;
}

int cmd_cat(char *name, uchar **buf, uint *len, client_session *s) {
    pthread_mutex_lock(&fs_mutex);
    Log("cmd_cat: name=%s", name);
    uint inum = findinum(name,s);
    if (inum == NINODES) {
        printf("File not found: %s\n", name);
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }
    inode *ip = iget(inum);
    if (!ip) {
        Error("cmd_cat: failed to get inode for %s", name);
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }
    if (ip->type == T_DIR) {
        printf("Cannot cat a directory: %s\n", name);
        free(ip);
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }
    *len = ip->size;
    *buf = malloc(*len);
    if (!*buf) {
        Error("cmd_cat: malloc failed");
        free(ip);
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }
    if (readi(ip, *buf, 0, *len) != *len) {
        Error("cmd_cat: readi failed");
        free(*buf);
        free(ip);
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }
    free(ip);
    pthread_mutex_unlock(&fs_mutex);
    return E_SUCCESS;
}

int cmd_w(char *name, uint len, const char *data, client_session *s) {
    pthread_mutex_lock(&fs_mutex);
    Log("cmd_w: name=%s len=%d data=%s", name, len, data);
    uint inum = findinum(name,s);
    if (inum == NINODES) {
        printf("File not found: %s\n", name);
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }
    inode *ip = iget(inum);
    if (!ip) {
        Error("cmd_w: failed to get inode for %s", name);
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }
    if (ip->type == T_DIR) {
        printf("Cannot write to a directory: %s\n", name);
        free(ip);
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }
    if (s->uid != ip->uid) {
        printf("You have no authority to write to this file!\n");
        free(ip);
        pthread_mutex_unlock(&fs_mutex);
        return E_CANNOT_WRITE;
    }
    if (writei(ip, (uchar *)data, 0, len) != (int)len) {
        Error("cmd_w: writei failed");
        free(ip);
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }

    if (len < ip->size) {
        // if the new data is shorter, truncate
        ip->size = len;
        iupdate(ip);
    }

    iupdate(ip);
    free(ip);
    pthread_mutex_unlock(&fs_mutex);
    return E_SUCCESS;
}

int cmd_i(char *name, uint pos, uint len, const char *data, client_session *s) {
    pthread_mutex_lock(&fs_mutex);
    Log("cmd_i: name=%s pos=%d len=%d data=%s", name, pos, len, data);
    uint inum = findinum(name,s);
    if (inum == NINODES) {
        printf("File not found: %s\n", name);
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }
    inode *ip = iget(inum);
    if (!ip) {
        Error("cmd_i: failed to get inode for %s", name);
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }
    if (ip->type == T_DIR) {
        printf("Cannot insert to a directory: %s\n", name);
        free(ip);
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }
    if (s->uid != ip->uid) {
        printf("You have no authority to write to this file!\n");
        free(ip);
        pthread_mutex_unlock(&fs_mutex);
        return E_CANNOT_WRITE;
    }
    if (pos > ip->size) {
        pos = ip->size;  
    }
    uint new_size = ip->size + len;
    if (new_size < ip->size) {  
        Error("cmd_i: file size overflow");
        free(ip);
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }

    uchar *buf = malloc(new_size);
    if (!buf) {
        Error("cmd_i: malloc failed");
        free(ip);
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }
    //read the original data
    if (readi(ip, buf, 0, ip->size) != (int)ip->size) {
        Error("cmd_i: readi failed");
        free(buf);
        free(ip);
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }

    //move the data to make space for the new data
    memmove(buf + pos + len, buf + pos, ip->size - pos);


    memcpy(buf + pos, data, len);
    ip->size = new_size;
    //write the new data back to the inode
    if (writei(ip, buf, 0, new_size) != (int)new_size) {
        Error("cmd_i: writei failed");
        free(buf);
        free(ip);
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }
    iupdate(ip);

    free(buf);
    free(ip);

    pthread_mutex_unlock(&fs_mutex);
    return E_SUCCESS;
}

int cmd_d(char *name, uint pos, uint len, client_session *s) {
    pthread_mutex_lock(&fs_mutex);
    Log("cmd_d: name=%s pos=%d len=%d", name, pos, len);
    
    uint inum = findinum(name,s);
    if (inum == NINODES) {
        printf("File not found: %s\n", name);
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }

    inode *ip = iget(inum);
    if (!ip) {
        Error("cmd_d: failed to get inode for %s", name);
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }

    if (ip->type == T_DIR) {
        printf("Cannot delete from a directory: %s\n", name);
        free(ip);
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }

    if (s->uid != ip->uid) {
        printf("You have no authority to write to this file!\n");
        free(ip);
        pthread_mutex_unlock(&fs_mutex);
        return E_CANNOT_WRITE;
    }

    if (pos >= ip->size) {  
        free(ip);
        pthread_mutex_unlock(&fs_mutex);
        return E_SUCCESS;
    }
    if (len == 0) {  
        free(ip);
        pthread_mutex_unlock(&fs_mutex);
        return E_SUCCESS;
    }
    if (pos + len > ip->size) {
        len = ip->size - pos;
    }

    uint new_size = ip->size - len;

    uchar *buf = malloc(new_size);
    if (!buf) {
        Error("cmd_d: malloc failed");
        free(ip);
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }

    if (readi(ip, buf, 0, pos) != (int)pos) {
        Error("cmd_d: readi failed (first part)");
        free(buf);
        free(ip);
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }

    if (readi(ip, buf + pos, pos + len, new_size - pos) != (int)(new_size - pos)) {
        Error("cmd_d: readi failed (second part)");
        free(buf);
        free(ip);
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }

    if (writei(ip, buf, 0, new_size) != (int)new_size) {
        Error("cmd_d: writei failed");
        free(buf);
        free(ip);
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }

    ip->size = new_size;
    iupdate(ip);

    free(buf);
    free(ip);
    pthread_mutex_unlock(&fs_mutex);
    return E_SUCCESS;
}

int cmd_login(int auid, client_session *s) {
    pthread_mutex_lock(&fs_mutex);
    if (auid < 0 || auid > 1024) {
        Error("Invalid uid");
        pthread_mutex_unlock(&fs_mutex);
        return E_ERROR;
    }

    s->uid = auid;
    s->pwd = 0;
    Log("cmd_login: uid=%d", s->uid);
    
    if(s->uid==1){
        uchar buf[BSIZE];
        read_block(0, buf);
        memcpy(&sb, buf, sizeof(sb));
        if (sb.magic == MAGIC) {
            char *struid = malloc(12);
            sprintf(struid, "%d", s->uid);
            if (findinum(struid,s) == NINODES) icreate(T_DIR, struid, s->pwd, s->uid, 0);
            free(struid);
        }
    }else{
        char *struid = malloc(12);
        sprintf(struid, "%d", s->uid);
        if (findinum(struid,s) == NINODES) icreate(T_DIR, struid, s->pwd, s->uid, 0);
        free(struid);
    }
    s->islogin=1;
    printf("Hello, uid=%u!\n", s->uid);
    pthread_mutex_unlock(&fs_mutex);
    return E_SUCCESS;
}
