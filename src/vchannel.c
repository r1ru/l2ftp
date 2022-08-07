#include "vchannel.h"
#include "threads_v3.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <pthread.h>

/* 内部データ */
static unsigned char vchflag; /* 受信側か送信側か */
static unsigned int vchnum; /* channelの数 */
static char fpath_base[20]; 

/* 公開データ */
struct vchannel_r *vch_head;
uint16_t fid_recent;

static int setup_fdata(void){
    int i, fd[vchnum];
    char fpath[30];
    struct vchannel_r * pvch;   

    if(vchflag == VCH_R){
        for(i = 0; i < vchnum; i++){
            pvch = (struct vchannel_r*)((uint8_t*)vch_head + sizeof(struct vchannel_r) * i);
            pvch->fdata = calloc(1, FDATALEN);
            if(pvch->fdata == NULL){
                perror("calloc");
                return -1;
            }
        }
    }else{
        for(i = 0; i < vchnum; i++){
            pvch = (struct vchannel_r*)((uint8_t*)vch_head + sizeof(struct vchannel_s) * i);
            snprintf(fpath, 30, "%s%d", fpath_base, i);
            
            fd[i] = open(fpath, O_RDONLY);
            if(fd[i] == -1){
                perror("open");
                return -1;
            }

            pvch->fdata = mmap(NULL, FDATALEN, PROT_READ, MAP_SHARED, fd[i] ,0);
            if(pvch->fdata == MAP_FAILED){
                perror("mmap");
                return -1;
            }

            if(close(fd[i]) == -1){
                perror("close");
                return -1;
            }
        }
    }

    return 0;
}

static void teardown_fdata(void){
    int i;
    struct vchannel_r * pvch;

    if(vchflag == VCH_R){
        for(i = 0; i < vchnum; i++){
            pvch = (struct vchannel_r*)((uint8_t*)vch_head + sizeof(struct vchannel_r) * i);
            free(pvch->fdata);
        }
    }else{
        for(i = 0; i < vchnum; i++){
            pvch = (struct vchannel_r*)((uint8_t*)vch_head + sizeof(struct vchannel_s) * i);
            munmap(pvch->fdata, FDATALEN);
        }
    }
}

static int setup_vchannel(const unsigned char flag, const uint16_t fno, const char* base){
    /* パラメータを設定 */
    vchflag = flag;
    vchnum = fno;
    strcpy(fpath_base, base);

    /* vchannelnum分のvchannelを作成 */
    if(vchflag == VCH_R)
        vch_head = calloc(vchnum, sizeof(struct vchannel_r));
    else 
         vch_head = calloc(vchnum, sizeof(struct vchannel_s));

    if(vch_head == NULL){
        perror("calloc");
        return -1;
    }   

    /* file dataを準備 */
    if(setup_fdata() == -1){
        printf("setup_fdata() Failed\n");
        return -1;
    }
    
    return 0;
}

static void teardown_vchannel(void){
    teardown_fdata();
    free(vch_head);
}

int activate_vchannel(const unsigned char flag, const uint16_t fno, const char* base){
    pthread_t master_t, handler_t, checker_t;
    int err;

    err = setup_vchannel(flag, fno, base);
    if(err == -1){
        printf("vchannle setup failed\n");
        return -1;
    }
    printf("vchannel activated! starting transmission\n");

    setup_threads_v3();

    /* threadを作成 */
    if(pthread_create(&master_t, NULL, (void *(*)(void *))master, NULL) != 0){
        perror("pthread_create");
        return -1;
    }
    if(pthread_create(&handler_t, NULL, (void *(*)(void *))blk_handler_r, NULL) != 0){
        perror("pthread_create");
        return -1;
    }
    if(pthread_create(&checker_t, NULL, (void *(*)(void *))fdata_checker, NULL) != 0){
        perror("pthread_create");
        return -1;
    }
    pthread_join(master_t, (void*)&err);

    if(err != 0){
        printf("Oops something went wrong sorry ^^;\n");
    }
    printf("Congrats! file transmisson maybe complited ^^\n");

    /* 後片付け */
    teardown_threads_v3();
    teardown_vchannel();
    
    return 0;
}

/* ファイルデータを保存(受信スレッド用)*/
int save_fdata(uint16_t fid){
    int fd;
    char fpath[30];
    struct vchannel_r *pvch;
    void *fdata;

    pvch = (struct vchannel_r*)((uint8_t*)vch_head + sizeof(struct vchannel_r) * fid);
    fdata = (void*)pvch->fdata;

    snprintf(fpath, 30, "%s%d", fpath_base, fid);
    fd = open(fpath, O_CREAT|O_RDWR,S_IRUSR|S_IWUSR);
    if(fd == -1){
        perror("open");
        return -1;
    }
    if(write(fd, fdata, FDATALEN) == -1){
        perror("write");
        return -1;
    }
    if(close(fd) == -1){
        perror("close");
        return -1;
    }    
    return 0;
}