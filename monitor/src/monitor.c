/*
 * monitor.c
 *
 *  Created on: Feb 17, 2019
 *      Author: alexey.lapshin
 */


#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <syslog.h>
#include <pthread.h>
#include <argp.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <netinet/in.h>
#include <zlog.h>
#include "database.h"
#include "server.h"
#include "cli.h"

const char *argp_program_version = "1.0";
const char *argp_program_bug_address = "<alexeyfonlapshin@gmail.com>";

#define DEFAULT_DB_PATH ":memory:"

struct arguments
{
    char *ip;
    u16 port;
    char *db_path;
    int silent;
    u8 daemonize;
    char *zlog_conf;
};

static const int handle_signals[] =
{
    SIGHUP, SIGTERM, SIGINT, SIGQUIT
};

static int is_number(char *str)
{
    int i;
    for(i = 0; i < strlen(str); i++)
    {
        if(!isdigit(str[i]))
        {
            return -1;
        }
    }
    return 0;
}

static error_t parse_opt (int key, char *arg, struct argp_state *state)
{
  struct arguments *arguments = state->input;
  char *tmp;

  switch (key)
    {
    case 'q':
      arguments->silent = 1;
      break;
    case 'i':
        arguments->ip = arg;
        break;
    case 'p':
      if(is_number(arg) != 0)
      {
          printf("Input port value not a number! (%s)\n", arg);
          return -1;
      }
      arguments->port = strtol(arg, &tmp, 10);
      if((arguments->port < 1024 || arguments->port > 65535))
      {
          printf("Port must be number in range [1024..65535] (%s)\n", arg);
          return -1;
      }
      break;
    case 's':
      arguments->db_path = arg;
      break;
    case 'd':
      arguments->daemonize = 1;
      break;
    case 'z':
      arguments->zlog_conf = arg;
      break;


    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static int parse_parameters(int argc, char **argv, struct arguments *arguments)
{
    struct argp_option options[] = {
      {"quiet",     'q', NULL, 0,  "Don't produce any output" },
      {"ip",        'i', "address", 0,  "Server ip address (default: localhost)" },
      {"port",      'p', "port", 0,  "Server TCP port (default: 5000)" },
      {"storage",   's', "path", 0,  "Path to file where received data will store (default: in-memory)" },
      {"daemonize", 'd',  NULL, 0,  "Run as a daemon" },
      {"zlog-config", 'z',  "path", 0,  "zlog config path" },
      { 0 }
    };
    char *doc = "This is a device emulator which connects to the device monitor"
            " server and sending random data with random period";
    struct argp argp = {options, parse_opt, NULL, doc};

    return argp_parse(&argp, argc, argv, 0, 0, arguments);
}

static void signal_handler(int signal)
{
    server_exit();
    deinit_db();
    cli_exit(NULL);
    fflush(stdout);
}

static int signals_handle_init(void)
{
    int i;
    int ret = 0;
    struct sigaction act = {0};
    sigemptyset(&act.sa_mask);
    act.sa_handler = signal_handler;
    for (i = 0; i < sizeof(handle_signals)/sizeof(handle_signals[0]); i++)
    {
        ret = sigaction(handle_signals[i], &act, NULL);
        if(ret != 0)
        {
            return ret;
        }
    }
    return ret;
}

int main(int argc, char **argv) {
    struct arguments arguments = {DEFAILT_IP, DEFAULT_PORT, DEFAULT_DB_PATH, 0, 0, "../config/zlog.conf"};
    struct sockaddr_in serv_addr = {0};
    int server_fd = -1;
    pthread_t thread;
    int operation = -1, ret;

    ret = parse_parameters(argc, argv, &arguments);
    if(ret != 0)
    {
        perror("Parse arguments failed\n");
        goto exit;
    }

    ret = zlog_init(arguments.zlog_conf);
    if(ret != 0)
    {
        fprintf(stderr, "Error while init zlog (%d). Check config file at path \"%s\"\n",
                ret, arguments.zlog_conf);
        goto exit;
    }

    operation--;
    ret = init_serv_address(&serv_addr, arguments.ip, arguments.port);
    if(ret != 0)
    {
        perror("Can't init server ip address\n");
        goto exit;
    }

    operation--;
    ret = signals_handle_init();
    if(ret != 0)
    {
        perror("Signals handler failed while setting\n");
        goto exit;
    }

    operation--;
    if(arguments.daemonize == 1)
    {
        ret = daemon(0, 0); //don't change working dir. Silent
        if(ret != 0)
        {
            perror("Can't init daemon\n");
            goto exit;
        }
    }


    operation--;
    ret = create_server_socket(&serv_addr);
    if(ret == -1)
    {
        perror("Failed to create server socket\n");
        goto exit;
    }
    server_fd = ret;

    operation--;
    ret = init_db(arguments.db_path);
    if(ret != 0)
    {
        goto exit;
    }

    operation--;
    if(arguments.daemonize == 1)
    {
        server_run(&server_fd);
        goto exit; //We are exited from a loop. exit
    }

    ret = pthread_create(&thread, NULL, &server_run, &server_fd);
    if(ret != 0)
    {
        perror("Can't init thread");
        goto exit;
    }

    ret = init_stdin_noblock();
    if(ret != 0)
    {
        perror("Can't init cli enviroment");
        goto exit;
    }
    cli_run(NULL);
    deinit_stdin_noblock();

exit:
    return ret == 0 ? ret : operation;
}
