#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <argp.h>
#include <device/message_utils.h>
#include <device/messages.h>

const char *argp_program_version = "1.0";
const char *argp_program_bug_address = "<alexeyfonlapshin@gmail.com>";

struct arguments
{
    char *ip;
    u16 port;
    u32 sleep;
    u32 count;
    char *name;
    u8 silent;
};

static error_t parse_opt (int key, char *arg, struct argp_state *state)
{
  struct arguments *arguments = state->input;

  switch(key)
    {
    case 'c':
      arguments->count = atoi(arg);
      break;
    case 'n':
      arguments->name = arg;
      break;
    case 's':
      arguments->sleep = atoi(arg);
      break;
    case 'p':
      arguments->port = atoi(arg);
      break;
    case 'i':
      arguments->ip = arg;
      break;
    case 'q':
      arguments->silent = 1;
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static int parse_parameters(struct arguments *arguments, int argc, char **argv)
{
    struct argp_option options[] = {
      {"name", 'n', "name", 0,  "Device name" },
      {"count", 'c', "count", 0,  "Number of packets to send" },
      {"sleep",     's', "seconds", 0,  "Sleep after" },
      {"server-ip", 'i', "ip", 0,  "Server ip address (default: localhost)" },
      {"port",      'p', "port", 0,  "Server TCP port (default: 5000)" },
      { 0 }
    };

    char *doc = "This is a device emulator which connects to the device monitor"
            " server and sending random data with random period";

    struct argp argp = { options, parse_opt, NULL, doc };

    return argp_parse(&argp, argc, argv, 0, 0, arguments);
}

static void fill_header(messageHeader_t *header, message_type_t type, size_t size)
{
    header->magic = PACKET_MAGIC;
    header->type = type;
    header->size = size;
    header->version = PROTOCOL_VERSION;
}

int main(int argc, char **argv)
{
    struct arguments arguments = {DEFAILT_IP, DEFAULT_PORT, 0, 1, "EATON", 0};
    int sockfd, ret, i;
    struct sockaddr_in servaddr;

    ret = parse_parameters(&arguments, argc, argv);
    if(ret != 0)
    {
        printf("Parse arguments fail (%d %s)...\n", errno, strerror(errno));
        exit(1);
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        printf("socket creation failed...\n");
        exit(1);
    }

    // assign IP, PORT
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr(arguments.ip);
    servaddr.sin_port = htons(arguments.port);

    // connect the client socket to server socket
    if (connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) != 0) {
        printf("connection with the server failed...\nSeems that monitor application is not running\n");
        exit(1);
    }
    if(arguments.silent == 0)
    {
        printf("connected to the server..\n");
    }

    messageDeviceName_t device_name;
    fill_header(&device_name.header, MSG_TYPE__NAME, sizeof(device_name));

    ret = convert_header(&device_name.header, CONVERT_TO_NETWORK);
    if(ret == -1)
    {
        printf("Error converting header\n");
        exit(1);
    }

    snprintf((char *)&device_name.name[0], sizeof(device_name.name), arguments.name);
    ret = send(sockfd, &device_name, sizeof(device_name), 0);
    if(ret == -1)
    {
        printf("Error sending message\n");
        exit(1);
    }

    messageDeviceData_t device_data;
    fill_header(&device_data.header, MSG_TYPE__DATA, sizeof(device_data));

    ret = convert_header(&device_data.header, CONVERT_TO_NETWORK);
    if(ret == -1)
    {
        printf("Error converting header\n");
        exit(1);
    }

    srand(time(NULL));
    for(i = 0; i < arguments.count; i++)
    {
        device_data.data = htonl(rand());
        ret = send(sockfd, &device_data, sizeof(device_data), 0);
        if(ret <= 0)
        {
            printf("Send error: %d %s\n", errno, strerror(errno));
        }
        if(arguments.silent == 0)
        {
            printf("Send data %d - %s\n", ntohl(device_data.data), ret > 0 ? "success" : "fail");
        }
        usleep(rand()%40);
    }

    if(arguments.silent == 0)
    {
        printf("sleeep %d seconds\n", arguments.sleep);
    }
    sleep(arguments.sleep);

    close(sockfd);
}
