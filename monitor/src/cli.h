/*
 * cli.h
 *
 *  Created on: Feb 17, 2019
 *      Author: alexey.lapshin
 */

#ifndef MONITOR_SRC_CLI_H_
#define MONITOR_SRC_CLI_H_

int init_stdin_noblock(void);
void deinit_stdin_noblock(void);
void *cli_run(void *arg);
int cli_exit(void *arg);

#endif /* MONITOR_SRC_CLI_H_ */
