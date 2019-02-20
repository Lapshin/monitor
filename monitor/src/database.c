/*
 * database.c
 *
 *  Created on: Feb 17, 2019
 *      Author: alexey.lapshin
 */

#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <limits.h>
#include <zlog.h>
#include <sqlite/sqlite3.h>

#include "database.h"

#ifndef SQL_WAL_MODE
#define DB_PRAGMA "PRAGMA synchronous=NORMAL;PRAGMA journal_mode=WAL;"
#else
#define DB_PRAGMA "PRAGMA synchronous=NORMAL"
#endif

#define __STRINGIFY(x) #x
#define STRINGIFY(x) __STRINGIFY(x)
#define CREATE_TABLE__DEVICES_DATA "CREATE TABLE IF NOT EXISTS DEVICES_DATA ( \
_id INTEGER PRIMARY KEY, \
NAME TEXT NOT NULL, \
DATA INT  NOT NULL, \
TIME INT  NOT NULL );"

/* Use only the hex values, because depends on the calculation of string buffer size */
#define TABLE_ADD_ROW__STRING_DD "INSERT INTO DEVICES_DATA (NAME,DATA,TIME) VALUES ('%s', 0x%x, 0x%lx);"
#define TABLE_ADD_ROW__STRING_DD_SIZE (sizeof(TABLE_ADD_ROW__STRING_DD) + DEVICE_NAME_LEN \
        + sizeof(STRINGIFY(INT_MAX)) + sizeof(STRINGIFY(LONG_MAX)))

#define GET_TABLE__MSG_COUNTS "SELECT NAME, count(*) FROM DEVICES_DATA GROUP BY NAME;"

#define GET_TABLE__MSG_DATA_FOR_NAME "SELECT * FROM DEVICES_DATA WHERE NAME = '%s';"
#define GET_TABLE__MSG_DATA_FOR_NAME_SIZE (sizeof(GET_TABLE__MSG_DATA_FOR_NAME) + DEVICE_NAME_LEN)

#define GET_TABLE__MSG_DATA_FOR_NAME_COUNT "SELECT count(*) FROM DEVICES_DATA WHERE NAME = '%s';"
#define GET_TABLE__MSG_DATA_FOR_NAME_COUNT_SIZE (sizeof(GET_TABLE__MSG_DATA_FOR_NAME_COUNT) + DEVICE_NAME_LEN)

#define GET_MAX_SIZE(x, y) (((x)>(y))?(x):(y))

#define EMERGENCY_EXIT_BACKUP "/tmp/monitor_emergency"

static sqlite3 *sql_db = NULL;
static int in_memory = 0;

#define ALLOC_SESSION_MAX_FD 32 // It can be tuned for better allocation behavior
#define GET_ALLOCATION_BLOCKS(count) ((count) / ALLOC_SESSION_MAX_FD)
#define GET_SESSIONS_MEMORY_SIZE(count) ((((count) / ALLOC_SESSION_MAX_FD) + 1) * ALLOC_SESSION_MAX_FD)


#define ZLOG_CATEGORY "DATABASE"

static session_t *sessions = NULL;
static size_t session_max_fd = 0;
static zlog_category_t *z_c = NULL;
#include "log.h"

#ifndef SQL_WAL_MODE
pthread_mutex_t sessions_mtx = PTHREAD_MUTEX_INITIALIZER;
#endif

static int display_device_data_by_name_callback(void *NotUsed, int argc, char **argv, char **azColName) {
   time_t time;
   struct tm * time_info;
   char *tmp = NULL;
   char timeString[16];

   time = strtol(argv[3], &tmp, 10);
   time_info = localtime(&time);
   strftime(timeString, sizeof(timeString), "%d.%b-%H:%M:%S", time_info);
   printf("%s measurement: %ld\n", timeString, strtol(argv[2], &tmp, 10));
   return 0;
}

static int display_device_data_by_name_count_callback(void *data, int argc, char **argv, char **azColName) {
    char *tmp = NULL;
    if(argc != 1)
    {
       return -1;
    }
    *((int *) data) = strtol(argv[0], &tmp, 10);

   return 0;
}

static int display_sessions_callback(void *NotUsed, int argc, char **argv, char **azColName) {
   printf("%s %s\n", argv[0], argv[1]);
   return 0;
}

int init_db(char *db_name)
{
    int ret = -1;
    char *zErrMsg = NULL;
    z_c = zlog_get_category(ZLOG_CATEGORY);
    if (z_c == NULL)
    {
        fprintf(stderr, "Can't get zlog category %s\n", ZLOG_CATEGORY);
        goto exit;
    }

    ret = sqlite3_open(db_name, &sql_db);
    if(ret != SQLITE_OK)
    {
      ERROR("Can't open database");
      goto exit;
    }

    in_memory = !strcmp(db_name, ":memory:");
//    ret = sqlite3_exec(sql_db, DB_PRAGMA, NULL, NULL, NULL);
//    if(ret != SQLITE_OK)
//    {
//        goto exit;
//    }

    ret = sqlite3_exec(sql_db, CREATE_TABLE__DEVICES_DATA, NULL, NULL, &zErrMsg);
    if(ret != SQLITE_OK)
    {
        ERROR("Can't open database: %s", sqlite3_errmsg(sql_db));
        goto exit;
    }

exit:
    if(ret != SQLITE_OK)
    {
        ERROR("Error %s", zErrMsg == NULL ? "" : zErrMsg);
        sqlite3_close(sql_db);
    }
    return ret;
}


static void save_data_to_base(session_t *session, messageDeviceData_t * data_msg)
{
   int ret;
   char *zErrMsg = NULL;
   char sql[TABLE_ADD_ROW__STRING_DD_SIZE];

   sprintf(sql, TABLE_ADD_ROW__STRING_DD, session->name, ntohl(data_msg->data), time(NULL));

   ret = sqlite3_exec(sql_db, sql, NULL, 0, &zErrMsg);
   if( ret != SQLITE_OK )
   {
       ERROR("Can't insert values %s\n", zErrMsg);
   }
}

static int check_sessions_memory(int fd)
{
    size_t i;
    size_t old_count = session_max_fd;

    if(fd < 0)
    {
        return -1;
    }

    if(fd <= session_max_fd)
    {
        return 0;
    }


    session_max_fd = fd;

    if(GET_ALLOCATION_BLOCKS(old_count) > GET_ALLOCATION_BLOCKS(session_max_fd))
    {
        return 0;
    }

    sessions = realloc(sessions, GET_SESSIONS_MEMORY_SIZE(session_max_fd) * sizeof(*sessions));
    if(sessions == NULL)
    {
        ERROR("Error while allocating memory!\n");
        exit(-1);
    }

    INFO("Allocated new chunk of memory for sessions. Size of array is %lu", GET_SESSIONS_MEMORY_SIZE(session_max_fd));

    for(i = old_count + 1; i < GET_SESSIONS_MEMORY_SIZE(session_max_fd); i++) //Init with default values
    {
        memset(&sessions[i], 0, sizeof(*sessions));
        sessions[i].fd = -1;
    }
    return 0;
}

static void process_message_device_name(int fd, session_t* session, messageGeneric_t* msg)
{
    if (session->fd == -1)
    {
        u32 ip;
        struct sockaddr_in addr = {0};
        session->fd = fd;
        getpeername(fd, (struct sockaddr *)&addr, &ip);
        NOTICE("Registered name %.*s from ip %s (fd=%d)\n",
                (int)sizeof(session->name),
                (char*) ((messageDeviceName_t*) msg)->name,
                inet_ntoa(addr.sin_addr), fd);
    }
    else if(strncmp((char*) session->name, (char*) ((messageDeviceName_t*) msg)->name, sizeof(session->name)) == 0)
    {
        return;
    }
    else
    {
        NOTICE("Device name was changed! Was %.*s now %.*s\n",
                (int)sizeof(session->name), session->name,
                (int)sizeof(session->name),
                (char*) ((messageDeviceName_t*) msg)->name);
    }
    strncpy((char*) session->name, (char*) ((messageDeviceName_t*) msg)->name, sizeof(session->name));
}

static int check_and_save_result_internal(int fd, read_message_status_t status, messageGeneric_t *msg)
{
    session_t *session = NULL;

    if(fd < 0)
    {
        return -1;
    }

    check_sessions_memory(fd);
    session = &sessions[fd];

    switch(status)
    {
        case READ_STATUS__OK:
//            session->received_messages++;
            if( fd == -1 && msg->header.type != MSG_TYPE__NAME) // First message from device must be a name
            {
                ERROR("First message from devise MUST BE a name");
                return -1;
            }
            else if(msg->header.type == MSG_TYPE__NAME)
            {
                process_message_device_name(fd, session, msg);
            }
            else if(msg->header.type == MSG_TYPE__DATA)
            {
                save_data_to_base(session, (messageDeviceData_t *)msg);
            }
            break;

        case READ_STATUS__BAD_MAGIC:
//            session->magic_fail_messages++;
//            break;
        case READ_STATUS__UNCKNOWN_TYPE:
        case READ_STATUS__BAD_SIZE:
        case READ_STATUS__ERRNO:
            ERROR("Bad packet received (error %d)", status);
//            session->corrupted_messages++;
//            break;
        case READ_STATUS__NO_DATA:
            return -1;
        case READ_STATUS__MEMORY_ERROR:
        case READ_STATUS__UNCKNOWN_VERSION:
        case READ_STATUS__EOF:
        default:
            DEBUG("Closing socket %d", fd);
            session->fd = -1;
            close(fd); /* fd will automatically removed from epoll list */
            memset(session->name, 0 ,sizeof(session->name));
            return -1;
    };

    return 0;
}

int check_and_save_result(int fd, read_message_status_t status, messageGeneric_t *msg)
{
    int ret = -1;

    if(fd < 0 || msg == NULL)
    {
        ERROR("fd %d msg %p", fd, (void *)msg);
        return ret;
    }

#ifndef SQL_WAL_MODE
    pthread_mutex_lock(&sessions_mtx);
#endif
    ret = check_and_save_result_internal(fd, status, msg);
#ifndef SQL_WAL_MODE
    pthread_mutex_unlock(&sessions_mtx);
#endif

    return ret;
}

int display_sessions_internal(void)
{
    int ret;
    char *zErrMsg = 0;

    ret = sqlite3_exec(sql_db, GET_TABLE__MSG_COUNTS, display_sessions_callback, NULL, &zErrMsg);
    if( ret != SQLITE_OK )
    {
        ERROR("Can't read values %s", zErrMsg);
    }
    return ret;

}

int display_sessions(void *arg)
{
    int ret;
#ifndef SQL_WAL_MODE
    pthread_mutex_lock(&sessions_mtx);
#endif
    ret = display_sessions_internal();
#ifndef SQL_WAL_MODE
    pthread_mutex_unlock(&sessions_mtx);
#endif
    return ret;
}

static int display_device_data_by_name_internal(char *name)
{
    int ret, count = 0;
    char *zErrMsg = 0;
    char select[GET_MAX_SIZE(GET_TABLE__MSG_DATA_FOR_NAME_SIZE, GET_TABLE__MSG_DATA_FOR_NAME_COUNT_SIZE)];

    if(name == NULL)
    {
        return -1;
    }

    sprintf(select, GET_TABLE__MSG_DATA_FOR_NAME_COUNT, name);
    ret = sqlite3_exec(sql_db, select, display_device_data_by_name_count_callback, &count, &zErrMsg);
    if(ret != SQLITE_OK)
    {
        ERROR("Can't read values %s\n", zErrMsg);
    }
    if(count == 0)
    {
        printf("  ##No data");
        return count; //TODO analyze result in cli
    }

    sprintf(select, GET_TABLE__MSG_DATA_FOR_NAME, name);
    ret = sqlite3_exec(sql_db, select, display_device_data_by_name_callback, NULL, &zErrMsg);
    if(ret != SQLITE_OK)
    {
        ERROR("Can't read values %s", zErrMsg);
    }
    printf("\nTotal count: %d", count);
    return count;
}

int display_device_data_by_name(void *arg)
{
    int ret;
#ifndef SQL_WAL_MODE
    pthread_mutex_lock(&sessions_mtx);
#endif
    ret = display_device_data_by_name_internal(arg);
#ifndef SQL_WAL_MODE
    pthread_mutex_unlock(&sessions_mtx);
#endif
    return ret;
}

static int database_backup_internal(const char *file_path)
{
    int ret;
    sqlite3 *sql_bacup_db;
    sqlite3_backup *sql_backup;

    ret = sqlite3_open(file_path, &sql_bacup_db);
    if(ret != SQLITE_OK)
    {
        ERROR("sqlite3_open() %d", ret);
        goto exit;
    }

    sql_backup = sqlite3_backup_init(sql_bacup_db, "main", sql_db, "main");
    if(sql_backup == NULL)
    {
        ERROR("sql_backup() %d", ret);
        goto exit;
    }
#if 0
      /* Each iteration of this loop copies 5 database pages from database
      ** pDb to the backup database. If the return value of backup_step()
      ** indicates that there are still further pages to copy, sleep for
      ** 250 ms before repeating. */
    do {
        ret = sqlite3_backup_step(sql_backup, 5);
        if(ret == SQLITE_OK || ret == SQLITE_BUSY || ret == SQLITE_LOCKED)
        {
            sqlite3_sleep(250);
        }
    } while(ret == SQLITE_OK || ret == SQLITE_BUSY || ret == SQLITE_LOCKED);
#endif

    ret = sqlite3_backup_step(sql_backup, -1);
    if(ret != SQLITE_DONE)
    {
        ERROR("sqlite3_backup_step() %d", ret);
        goto exit;
    }

    ret = sqlite3_backup_finish(sql_backup);
    if(ret != SQLITE_OK)
    {
        ERROR("sqlite3_backup_finish() %d", ret);
        goto exit;
    }

exit:
    if(ret != SQLITE_DONE && ret != SQLITE_OK)
    {
      ERROR("sql: Error while backup %d", ret);
    }
    else
    {
        NOTICE("Database successfully backuped to %s\n", file_path); //move code analyze to cli
    }
    while(sqlite3_close(sql_bacup_db) != SQLITE_OK) // It could return SQLITE_BUSY. Need retry
    {
      ERROR("sql backup db busy. Waiting ...");
      sleep(1);
    }
    return ret;
}

int database_backup(void *arg)
{
    int ret;

#ifndef SQL_WAL_MODE
    pthread_mutex_lock(&sessions_mtx);
#endif
    ret = database_backup_internal(arg);
#ifndef SQL_WAL_MODE
    pthread_mutex_unlock(&sessions_mtx);
#endif
    return ret;
}

void deinit_db(void)
{
    int i;
    char file_path[PATH_MAX] = {0};
    if(in_memory)
    {
        sprintf(file_path, "%s_%d.db", EMERGENCY_EXIT_BACKUP, getpid());
        database_backup(file_path);
    }
    sqlite3_close(sql_db);

    pthread_mutex_lock(&sessions_mtx);
    for(i = 0; i < session_max_fd; i++)
    {
        if(sessions[i].fd == -1)
        {
            continue;
        }
        close(sessions[i].fd);
        sessions[i].fd = -1;
    }
    pthread_mutex_unlock(&sessions_mtx);
}
