/*
 * server.h
 *
 *  Created on: Feb 15, 2019
 *      Author: alexey.lapshin
 */

#ifndef SERVER_H_
#define SERVER_H_
#include <device/types.h>

int init_serv_address(struct sockaddr_in *addr, char *ip, u16 port);
int create_server_socket(struct sockaddr_in *serv_addr);
void *server_run(void *arg);
void server_exit(void);

#endif /* SERVER_H_ */
