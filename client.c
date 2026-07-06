#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define PORTNUM 8000
#define BUFSIZE 4096

int recv_until_newline(int fd)
{
    char response[BUFSIZE];
    int ret;

    while ((ret = recv(fd, response, BUFSIZE, 0)) > 0)
    {
        write(1, response, ret);

        if (memchr(response, '\n', ret) != NULL)
        {
            return 0;
        }
    }

    if (ret < 0)
    {
        perror("recv");
        return -1;
    }

    return 0;
}

int main(void)
{
    struct sockaddr_in saddr;
    int fd;
    char buf[BUFSIZE];

    if ((fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
        perror("socket");
        return -1;
    }

    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(PORTNUM);
    saddr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(fd, (struct sockaddr*)&saddr, sizeof(saddr)) < 0)
    {
        perror("connect");
        close(fd);
        return -1;
    }

    for (;;)
    {
        fputs("> ", stdout);
        fflush(stdout);

        if (fgets(buf, BUFSIZE, stdin) == NULL)
        {
            break;
        }

        if (send(fd, buf, strlen(buf), 0) < 0)
        {
            perror("send");
            close(fd);
            return -1;
        }

        if (recv_until_newline(fd) < 0)
        {
            close(fd);
            return -1;
        }
    }

    close(fd);

    return 0;
}
