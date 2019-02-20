#include <sys/epoll.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <time.h>
#include <syslog.h>
#include "server.h"
#include <device/message_utils.h>
#include <device/messages.h>
#include "cli.h"
#include "threadpool.h"
#include "database.h"
#include <zlog.h>

#define ZLOG_CATEGORY "DATABASE"
#define EPOLL_MAXEVENTS 32
_Atomic int server_running = 1;

static zlog_category_t *z_c = NULL;
#include "log.h" // must be after z_c


int init_serv_address(struct sockaddr_in *addr, char *ip, u16 port)
{
    int ret;
    if(ip == NULL)
    {
        return -1;
    }
    memset(addr, 0x00, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons(port);
    ret = inet_pton(AF_INET, ip, &(addr->sin_addr)); // 1 - on success
    return (ret == 1) ? 0 : -1;
}

int read_wrapper(int fd, u8 *ptr, size_t size)
{
    int ret = READ_STATUS__OK;
    size_t bytes_read = 0, count;
    while (bytes_read != size)
    {
        count = read(fd, ptr + bytes_read, size - bytes_read);
        if (count == -1)
        {
            if(errno == EAGAIN)
            {
                ret = READ_STATUS__NO_DATA;
                goto exit;
            }
            ret = READ_STATUS__ERRNO;
            goto exit;
        }
        if(count == 0) /* EOF - connection closed */
        {
            ret = READ_STATUS__EOF;
            goto exit;
        }

        bytes_read += count;
    }

exit:
    return ret;
}

static read_message_status_t read_message(int fd, messageGeneric_t *msg)
{
    int ret;
    u8 corrupted_magic = 0;
    messageHeader_t *header;

    if(msg == NULL)
    {
        return READ_STATUS__MEMORY_ERROR;
    }
    header = &msg->header;
    DEBUG("Read data from fd %d", fd);

    ret = read_wrapper(fd, (u8 *)header, sizeof(*header));
    if(ret != READ_STATUS__OK)
    {
        return ret;
    }

    while(ntohl(header->magic) != PACKET_MAGIC)
    {
        corrupted_magic = 1;
        memmove(header, ((u8 *) header) + 1, sizeof(*header) - 1);
        ret = read_wrapper(fd, ((u8 *) header) + (sizeof(*header) - 1), sizeof(u8));
        if(ret != READ_STATUS__OK)
        {
            return ret;
        }
    }
    if(corrupted_magic == 1)
    {
        ERROR("Some data was skipped. Troubles with founding magic");
    }

    ret = convert_header(header, CONVERT_TO_HOST);
    if(ret == -1)
    {
        ERROR("Error while convert header");
        return READ_STATUS__MEMORY_ERROR;
    }

    if(header->version != PROTOCOL_VERSION)
    {
        ERROR("Unknown protocol version %d. Server uses %d", header->version, PROTOCOL_VERSION);
        return READ_STATUS__UNCKNOWN_VERSION;
    }

    if(header->type >= MSG_TYPE__MAX)
    {
        ERROR("Message type out of renage (%d)", header->type );
        return READ_STATUS__UNCKNOWN_TYPE;
    }

    if(header->size != message_sizes[header->type])
    {
        ERROR("Corrupted message (type %d), size: received %d expected %ld\n",
                header->type, header->size, message_sizes[header->type]);
        return READ_STATUS__BAD_SIZE;
    }

    ret = read_wrapper(fd, (u8 *)msg->payload, header->size - sizeof(*header));
    return ret;
}

static int make_socket_non_blocking(int sfd)
{
    int ret, flags = fcntl(sfd, F_GETFL, 0);

    if (flags == -1)
    {
        ERROR("fcntl fail (%d:%s)", errno, strerror(errno));
        return -1;
    }

    flags |= O_NONBLOCK;
    ret = fcntl(sfd, F_SETFL, flags);
    if (ret == -1)
    {
        ERROR("fcntl fail (%d:%s)", errno, strerror(errno));
    }

    return ret;
}

int accept_and_add_new(int epoll_fd, int server_fd)
{
    int new_fd = -1, ret = -1;
    struct epoll_event event;
    struct sockaddr in_addr;
    socklen_t in_len = sizeof(in_addr);

    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

    while ((new_fd = accept(server_fd, &in_addr, &in_len)) != -1)
    {

        if (getnameinfo(&in_addr, in_len,
                hbuf, sizeof(hbuf),
                sbuf, sizeof(sbuf),
                NI_NUMERICHOST | NI_NUMERICHOST) == 0) {
            DEBUG("Accepted connection on descriptor %d (host=%s, port=%s)\n",
                    new_fd, hbuf, sbuf);
        }
        /* Make the incoming socket non-block
         * and add it to list of fds to
         * monitor*/
        ret = make_socket_non_blocking(new_fd);
        if (ret != 0) {
            ERROR("Can't make socket non-blocking");
            goto exit;
        }

        event.data.fd = new_fd;
        event.events = EPOLLIN | EPOLLET | EPOLLHUP;
        ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, new_fd, &event);
        if (ret != 0) {
            ERROR("epoll_ctl error (%d:%s)", errno, strerror(errno));
            goto exit;
        }
        in_len = sizeof(in_addr);
    }

    if (errno != EAGAIN && errno != EWOULDBLOCK)
    {
        ERROR("accept error (%d:%s)", errno, strerror(errno));
    }

    ret = 0;

exit:
    return ret;
}

static void process_messages(int fd)
{
    messageBiggest_t msg;
    int ret = 0;
    read_message_status_t ret_val = read_message(fd, (messageGeneric_t *)&msg);
    while(ret == 0)
    {
        ret = check_and_save_result(fd, ret_val, (messageGeneric_t *)&msg);
        if(ret == 0)
        {
            ret_val = read_message(fd, (messageGeneric_t *)&msg);
        }
    }
    return;
}

static int process_epoll_event(struct epoll_event* event, int server_fd, int epoll_fd)
{
    int ret = 0;
    if ((event->events & EPOLLERR) || (event->events & EPOLLHUP)
            || !(event->events & EPOLLIN))
    {
        /* An error on this fd or socket not ready */
        ERROR("Error on socket %d. Event mask 0x%x", event->data.fd, event->events);
        close(event->data.fd);
    }
    else if (event->data.fd == server_fd)
    {
        /* New incoming connection */
        DEBUG("Accept new connection...");
        ret = accept_and_add_new(epoll_fd, server_fd);
        if (ret != 0)
        {
            return ret;
        }
    }
    else
    {
        DEBUG("Process incoming data from fd %d", event->data.fd);
        process_messages(event->data.fd);
    }

    return ret;
}

static int polling(int server_fd)
{
    int epoll_fd = -1, n, i, ret = -1;
    struct epoll_event event, *events = NULL;

    if(server_fd == -1)
    {
        ERROR("Bad server socket descriptor\n");
        goto exit;
    }

    events = calloc(EPOLL_MAXEVENTS, sizeof(event));

    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1)
    {
        ERROR("epoll_create1() error (%d:%s)", errno, strerror(errno));
        goto exit;
    }

    event.data.fd = server_fd;
    event.events = EPOLLIN | EPOLLET;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) == -1) {
        ERROR("epoll_ctl() error (%d:%s)", errno, strerror(errno));
        goto exit;
    }

    while(server_running)
    {
        n = epoll_wait(epoll_fd, events, EPOLL_MAXEVENTS, -1);
        for (i = 0; i < n; i++)
        {
            ret = process_epoll_event(&events[i], server_fd, epoll_fd);
            if(ret != 0 && server_running)
            {
                ERROR("Exiting from server main-loop");
                goto exit;
            }
        }
    }
    close(epoll_fd);
    ret = 0;

exit:
    if(epoll_fd != -1)
    {
        close(epoll_fd);
    }

    free(events);
    return ret;
}

int create_server_socket(struct sockaddr_in *serv_addr)
{
    int server_fd = -1, ret = -1;
    int opt = 1;

    z_c = zlog_get_category(ZLOG_CATEGORY);
    if (z_c == NULL)
    {
        fprintf(stderr, "Can't get zlog category %s\n", ZLOG_CATEGORY);
        goto exit;
    }

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_fd == -1)
    {
        ERROR("Listen failed (%d:%s)", errno, strerror(errno));
        goto error;
    }

    ret = setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (ret != 0) {
        ERROR("setsockopt() failed (%d, %s)", errno, strerror(errno));
        goto error;
    }

    ret = make_socket_non_blocking(server_fd);
    if (ret != 0) {
        ERROR("make_socket_non_blocking()");
        goto error;
    }

    ret = bind(server_fd, (struct sockaddr*)serv_addr, sizeof(*serv_addr));
    if(ret != 0)
    {
        ERROR("Bind failed (%d:%s)", errno, strerror(errno));
        goto error;
    }

    ret = listen(server_fd, EPOLL_MAXEVENTS);
    if (ret != 0)
    {
        ERROR("Listen failed (%d:%s)", errno, strerror(errno));
        goto error;
    }

    goto exit;

error:
    if(server_fd != -1)
    {
        close(server_fd);
        server_fd = -1;
    }

exit:
    return server_fd;
}

void *server_run(void *arg)
{
    int server_fd = *((int *)arg);
    polling(server_fd);

    close(server_fd);
    cli_exit(NULL);
    return NULL;
}

void server_exit(void)
{
    server_running = 0;
}
