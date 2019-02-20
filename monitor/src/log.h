/*
 * log.h
 *
 *  Created on: 20 февр. 2019 г.
 *      Author: alexey.lapshin
 */

#ifndef MONITOR_SRC_LOG_H_
#define MONITOR_SRC_LOG_H_

#define ERROR(...) zlog_error(z_c, ##__VA_ARGS__)
#define NOTICE(...) zlog_notice(z_c, ##__VA_ARGS__)
#define INFO(...) zlog_info(z_c, ##__VA_ARGS__)
#define DEBUG(...) zlog_debug(z_c, ##__VA_ARGS__)

#endif /* MONITOR_SRC_LOG_H_ */
