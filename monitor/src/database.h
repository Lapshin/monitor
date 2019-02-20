/*
 * database.h
 *
 *  Created on: Feb 17, 2019
 *      Author: alexey.lapshin
 */

#ifndef DATABASE_H_
#define DATABASE_H_

#include <sys/queue.h>
#include <device/types.h>
#include <device/messages.h>


typedef struct session_s
{
    int fd;
    u8  name[DEVICE_NAME_LEN];
//    u32 received_messages;
//    u32 corrupted_messages;
//    u32 magic_fail_messages;
} session_t;

int init_db(char *db_name);
int check_and_save_result(int fd, read_message_status_t ret, messageGeneric_t * msg);
int display_sessions(void *arg);

int database_backup(void *arg);
int display_device_data_by_name(void *arg);

void deinit_db(void);

#endif /* DATABASE_H_ */
