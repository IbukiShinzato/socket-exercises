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

int main(void)
{
    struct sockaddr_in saddr;
    int fd;
    char buf[BUFSIZE];
    char response[BUFSIZE];

    /* make server's socket */
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
        exit(-1);
    }

    for (;;)
    {
        fputs("> ", stdout);
        fflush(stdout);

        if (fgets(buf, BUFSIZE, stdin) == NULL)
        {
            break;
        }

        send(fd, buf, strlen(buf), 0);
        int ret = recv(fd, response, BUFSIZE, 0);

        if (ret == 0)
        {
            break;
        }

        if (ret < 0)
        {
            perror("recv");
            return -1;
        }

        write(1, response, ret);
    }

    close(fd);

    return 0;
}
