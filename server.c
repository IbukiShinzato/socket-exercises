#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define PORTNUM 8000
#define BUFSIZE 4096
#define MAXCLIENTS 20 /* Max # of connections */

static int num_of_threads = 0;
static pthread_mutex_t mt = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ct = PTHREAD_COND_INITIALIZER;

/* Strings for methods */
const char* get = "GET";
const char* put = "PUT";
const char* file = "FILE";
const char* not_found = "NOT FOUND\n";
const char* error = "PROTOCOL ERROR\n";

/* Home directory name */
const char* home = "/Users/shinzatoibuki";

typedef enum
{
    PROTOCOL_GET,
    PROTOCOL_PUT,
    PROTOCOL_ERROR
} protocol_t;

/* Check buf has a carriage return */
int is_line(char* buf, int len)
{
    int i = 0;

    while (i != len)
    {
        if (buf[i] == '\n') return i;
        i++;
    }
    return 0;
}

/* Send a response */
void send_file(int fd, char* filename)
{
    int fd_file, ret;
    struct stat st;
    char buf[BUFSIZE], pathname[BUFSIZE];

    snprintf(buf, sizeof(buf), "%s/%s", home, filename);

    if (realpath(buf, pathname) == NULL)
    {
        perror("realpath");
        send(fd, not_found, strlen(not_found), 0);
        return;
    }

    if (strncmp(pathname, home, strlen(home)) != 0) goto error;

    if ((fd_file = open(pathname, O_RDONLY)) < 0)
    {
        perror("open");
        send(fd, not_found, strlen(not_found), 0);
        return;
    }

    if (fstat(fd_file, &st) < 0)
    {
        perror("fstat");
        close(fd_file);
        goto error;
    }

    snprintf(buf, sizeof(buf), "FILE(%lld): ", (long long)st.st_size);
    send(fd, buf, strlen(buf), 0);

    while ((ret = read(fd_file, buf, BUFSIZE)) > 0)
    {
        send(fd, buf, ret, 0);
    }

    close(fd_file);
    send(fd, "\n", 1, 0);

    return;

error:
    send(fd, error, strlen(error), 0);
}

/* Save received content to a file */
void save_file(int fd, char* filename, char* content)
{
    int fd_file;
    char buf[BUFSIZE], pathname[BUFSIZE];

    if (filename[0] == '/' || strstr(filename, "..") != NULL) goto error;

    snprintf(pathname, sizeof(pathname), "%s/%s", home, filename);

    fd_file = open(pathname, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_file < 0)
    {
        perror("open");
        goto error;
    }

    size_t left = strlen(content);
    char* p = content;

    while (left > 0)
    {
        ssize_t ret = write(fd_file, p, left);
        if (ret < 0)
        {
            perror("write");
            close(fd_file);
            goto error;
        }

        p += ret;
        left -= ret;
    }

    close(fd_file);

    snprintf(buf, sizeof(buf), "PUT: %s created", pathname);
    send(fd, buf, strlen(buf), 0);
    send(fd, "\n", 1, 0);

    return;

error:
    send(fd, error, strlen(error), 0);
}

protocol_t parse_protocol(char* msg, int* pos)
{
    if (strncmp(msg, get, strlen(get)) == 0)
    {
        *pos = strlen(get);
        return PROTOCOL_GET;
    }

    if (strncmp(msg, put, strlen(put)) == 0)
    {
        *pos = strlen(put);
        return PROTOCOL_PUT;
    }

    return PROTOCOL_ERROR;
}

/* Parse a received message and send its response */
void resp_msg(int fd, char* msg, int len)
{
    int pos;
    char filename[BUFSIZE];
    char content[BUFSIZE];

    filename[0] = '\0';
    content[0] = '\0';

    protocol_t state = parse_protocol(msg, &pos);

    if (state == PROTOCOL_ERROR) goto error;

    if (pos >= len || msg[pos++] != '<') goto error;

    /* Extract file name */
    int i = 0;
    while (pos < len && msg[pos] != '>')
    {
        if (i >= BUFSIZE - 1) goto error;
        filename[i++] = msg[pos++];
    }

    if (pos >= len || msg[pos] != '>') goto error;
    filename[i] = '\0';

    pos++;

    if (state == PROTOCOL_PUT)
    {
        if (pos >= len || msg[pos++] != '<') goto error;

        /* Extract file content */
        i = 0;
        while (pos < len && msg[pos] != '>')
        {
            if (i >= BUFSIZE - 1) goto error;
            content[i++] = msg[pos++];
        }

        if (pos >= len || msg[pos] != '>') goto error;
        content[i] = '\0';

        pos++;
    }

    switch (state)
    {
        case PROTOCOL_GET:
            send_file(fd, filename);
            break;

        case PROTOCOL_PUT:
            save_file(fd, filename, content);
            break;

        default:
            goto error;
    }

    return;

error:
    send(fd, error, strlen(error), 0);
}

/* Receive a message, parse it, and send its response */
void* recv_and_resp(void* arg)
{
    int fd, ret, len, pos;
    char *ptr, *dst;
    char buf[BUFSIZE], msg[BUFSIZE];

    fd = *(int*)arg;
    free(arg);

    dst = buf;
    buf[0] = '\0';

    /* Receive a message from a client */
    while ((ret = recv(fd, msg, BUFSIZE - 1, 0)) > 0)
    {
        msg[ret] = '\0';

        ptr = msg;
        len = ret;

        /* Extract a line from the msg and respond to it */
        while ((pos = is_line(ptr, len)) > 0)
        {
            int stored_len = dst - buf;

            if (stored_len + pos >= BUFSIZE) goto finish;

            memcpy(dst, ptr, pos);
            dst += pos;
            *dst = '\0';

            resp_msg(fd, buf, dst - buf);

            dst = buf;
            buf[0] = '\0';

            ptr += pos + 1;
            len -= pos + 1;
        }

        /* Preserve the msg and invoke recv() again if it has no line */
        if (len > 0)
        {
            int stored_len = dst - buf;

            if (stored_len + len >= BUFSIZE) goto finish;

            memcpy(dst, ptr, len);
            dst += len;
            *dst = '\0';
        }
    }

finish:
    pthread_mutex_lock(&mt);
    num_of_threads--;
    pthread_mutex_unlock(&mt);
    pthread_cond_signal(&ct);

    close(fd);

    return NULL;
}

int main(void)
{
    struct sockaddr_in saddr, caddr;
    int fd1, fd2;
    socklen_t len;
    pthread_t pt;

    if ((fd1 = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
        perror("socket");
        return -1;
    }

    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(PORTNUM);
    saddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd1, (struct sockaddr*)&saddr, sizeof(saddr)))
    {
        perror("bind");
        return -1;
    }

    if (listen(fd1, 5))
    {
        perror("listen");
        return -1;
    }

    for (;;)
    {
        len = sizeof(caddr);

        if ((fd2 = accept(fd1, (struct sockaddr*)&caddr, &len)) < 0)
        {
            perror("accept");
            return -1;
        }

        /* Wait if # of threads is MAXCLIENTS */
        pthread_mutex_lock(&mt);
        while (num_of_threads >= MAXCLIENTS) pthread_cond_wait(&ct, &mt);
        num_of_threads++;
        pthread_mutex_unlock(&mt);

        int* fd_socket = malloc(sizeof(*fd_socket));
        if (fd_socket == NULL)
        {
            perror("malloc");
            close(fd2);
            return -1;
        }

        *fd_socket = fd2;

        if (pthread_create(&pt, NULL, recv_and_resp, fd_socket) != 0)
        {
            perror("pthread_create");
            free(fd_socket);
            close(fd2);
            return -1;
        }

        pthread_detach(pt);
    }

    return 0;
}
