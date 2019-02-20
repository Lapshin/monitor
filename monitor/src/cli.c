/*
 * cli.c
 *
 *  Created on: Feb 17, 2019
 *      Author: alexey.lapshin
 */

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <ctype.h>

#include "database.h"
#include "cli.h"

#define ALIGNMENT_STRING 16
#define MAX_TERMINAL_LEN 80
#define PROMPT "\nmonitor> "

_Atomic int cli_running = 1;
int cli_pos = 0;
char cli_buf[MAX_TERMINAL_LEN];
char last_command[MAX_TERMINAL_LEN]; //TODO Could be full history, designed on single-liked list

static int print_help(void *arg);

typedef struct command_s
{
    char *name;
    char *help;
    int argc;
    int (*fn) (void *);
} command_t;

static command_t commands[] =
{
    {"help", "Show help", 0, print_help},
    {"show-all", "Show all registered devices and counter of received messages", 0, display_sessions},
    {"show-device", "Show chosen device received data", 1, display_device_data_by_name},
    {"backup", "Make backup to chosen file. (Example: backup /home/user/backup.db)", 1, database_backup},
    {"exit", "Stop monitor and exit", 0, cli_exit},
};
#define COMMANDS_COUNT (sizeof(commands)/sizeof(commands[0]))


static int wait_for_data(void)
{
    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(STDIN_FILENO, &read_set);
    return select(STDIN_FILENO + 1, &read_set, NULL, NULL, NULL);
}

static void print_prompt(void)
{
    printf("%s", PROMPT);
}

int cli_exit(void *arg)
{
    cli_running = 0;
    return 0;
}

static int print_help(void *arg)
{
    u32 i;
    printf("List of commands:\n");
    for(i = 0; i < COMMANDS_COUNT; i++)
    {
        printf("    %s %*s- %s\n", commands[i].name, ALIGNMENT_STRING - (int)strlen(commands[i].name), " ", commands[i].help);
    }
    printf("\nArrows:\n");
    printf("    'UP' %*s- %s\n", ALIGNMENT_STRING - (int)strlen("'UP'"), " ", "Move to the latest command line held in history");
    printf("    'DOWN' %*s- %s\n", ALIGNMENT_STRING - (int)strlen("'DOWN'"), " ", "Clear command line");

    return 0;
}

static void process_command(void)
{
    char str[sizeof(cli_buf)];
    int i;
    char *rest = str;
    char *command_str;
    if(strlen(cli_buf) == 0)
    {
        return;
    }

    memcpy(str, cli_buf, sizeof(str));

    command_str = strtok_r(rest, " ", &rest);
    if(command_str == NULL)
    {
        return;
    }

    for(i = 0; i < COMMANDS_COUNT; i++)
    {
        if(strncmp(command_str, commands[i].name, strlen(commands[i].name)) == 0)
        {
            break;
        }
    }
    if(i == COMMANDS_COUNT)
    {
        printf("  ##Unknown command\n");
        print_help(NULL);
        return;
    }

    if(commands[i].fn == NULL)
    {
        printf("No action for command %s", commands[i].name);
        return;
    }

    if(commands[i].argc > 1) //TODO rethink about big number of args
    {
        printf("  ##Unsupported command: Count of arguments too big!\n");
        return;
    }

    command_str = strtok_r(rest, " ", &rest);
    commands[i].fn(command_str);
    memcpy(last_command, cli_buf, sizeof(last_command));
}

int init_stdin_noblock(void)
{
    int ret;
    struct termios newt;
    ret = tcgetattr(STDIN_FILENO, &newt);
    if(ret != 0)
    {
        goto exit;
    }
    newt.c_lflag &= ~(ICANON | ECHO);
    ret = tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    if(ret != 0)
    {
        goto exit;
    }
    ret = fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
    if(ret != 0)
    {
        goto exit;
    }

    ret = setvbuf(stdout, NULL, _IONBF, 0);
    if(ret != 0)
    {
        goto exit;
    }

exit:
    return ret;
}

void deinit_stdin_noblock(void) //at exit
{
    struct termios newt;
    tcgetattr(STDIN_FILENO, &newt);
    newt.c_lflag |= (ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    fcntl(0, F_SETFL, fcntl(0, F_GETFL) & ~(O_NONBLOCK));
}


static void remove_line(void)
{
    while(cli_pos != 0)
    {
        printf("\b \b");
        cli_pos--;
    }
}

void escape_sequence(void)
{
    //TODO it is poor processing only for arrows
    char tmp;
    tmp = getchar();
    if(tmp != '[')
    {
        return;
    }

    while(!((tmp >= 'a' && tmp <= 'z') || (tmp >= 'A' && tmp <= 'Z')))
    {
        tmp = getchar(); //value
    }
    if(tmp == 'A')
    {
        remove_line();
        memcpy(cli_buf, last_command, sizeof(cli_buf));
        cli_pos = strlen(cli_buf);
        printf("%s", cli_buf);
    }
    else if(tmp == 'B')
    {
        remove_line();
        memset(cli_buf, 0, sizeof(cli_buf));
    }
    return;
}

void *cli_run(void *arg)
{
    char tmp;

    printf("\nWelcome to devices monitor app!\n\n");
    print_help(NULL);

    print_prompt();
    while(cli_running)
    {
        if(wait_for_data() == -1)
        {
            continue;
        }
//        printf("%d\n", tmp);
        tmp = getchar();
        if(tmp == 13)
        {
            continue;
        }
        if(tmp == 27)
        {
            escape_sequence();
            continue;
        }

        if(tmp == 127)
        {
            if(cli_pos == 0)
            {
                continue;
            }
            cli_buf[cli_pos] = 0;
            cli_pos--;
            printf("\b \b");
            continue;
        }

        if(iscntrl(tmp))
        {
            if(tmp == '\n')
            {
                if(cli_pos == 0)
                {
                    print_prompt();
                    continue;
                }
                printf("\n");
                process_command();
                cli_pos = 0;
                memset(cli_buf, 0, sizeof(cli_buf));
                print_prompt();
            }
            continue;
        }

        cli_buf[cli_pos] = tmp;
        cli_pos++;
        printf("%c", tmp);
        if(cli_pos >= MAX_TERMINAL_LEN)
        {
            printf("  ##Too long command!");
            cli_pos = 0;
            memset(cli_buf, 0, sizeof(cli_buf));
        }
    }
    printf("\n");
    fflush(stdout);
    return NULL;
}
