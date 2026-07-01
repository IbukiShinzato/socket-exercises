#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define PORTNUM    8000 
#define BUFSIZE    4096
#define MAXCLIENTS 20 /* Max # of connections */

static int num_of_threads = 0;
static pthread_mutex_t mt = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  ct = PTHREAD_COND_INITIALIZER;

/* Strings for methods */
const char *get       = "GET";
const char *file      = "FILE";
const char *not_found = "NOT FOUND\n";
const char *error     = "PROTOCOL ERROR\n";

/* Home directory name */
const char *home      = "/home/yamada";

/* Check buf has a carriage return */
int is_line(char *buf, int len)
{
	int i = 0;
	
	while (i != len) {
		if (buf[i] == '\n') return i;
		i++;
	}
	return 0;
}

/* Send a response */
void send_file(int fd, char *filename)
{
	int         fd_file, ret;
	struct stat st;
	char        buf[BUFSIZE], pathname[BUFSIZE];

	sprintf(buf, "%s/%s", home, filename);

	realpath(buf, pathname);
	if (!strstr(pathname, home)) goto error;

	if ((fd_file = open(pathname, O_RDONLY)) < 0) {
		perror("open");
		send(fd, not_found, strlen(not_found), 0);
		return ;
	}

	fstat(fd_file, &st);
	sprintf(buf, "FILE(%ld): ", st.st_size);
	send(fd, buf, strlen(buf), 0);

	while ((ret = read(fd_file, buf, BUFSIZE)) > 0) {
		send(fd, buf, ret, 0);
	}

	send (fd, "\n", 1, 0);

	return ;

error:
	send(fd, error, strlen(error), 0);
}

/* Parse a received message and send its response */
void resp_msg(int fd, char *msg, int len)
{
	int  pos;
	char filename[BUFSIZE];

	if (len <= 4 || strncmp(msg, get, strlen(get)) != 0) goto error;

	pos = strlen(get);

	if (msg[pos++] != '<') goto error;

	/* Extract file name */
	while (msg[pos] != '>') {
		filename[pos-4] = msg[pos];
		if (pos == len) goto error;
		pos++;
	}
	filename[pos-4] = '\0';

	send_file(fd, filename);

	return ;

error:
	send(fd, error, strlen(error), 0);
}

/* Receive a message, parse it, and send its response */
int *recv_and_resp(int *fd_socket)
{
	int  fd, ret, len, pos;
	char *ptr, *dst, buf[BUFSIZE], msg[BUFSIZE];

	fd = (int)fd_socket;
	dst = buf;

        /* Receive a message from a client */
	while ((ret = recv(fd, msg, BUFSIZE, 0)) > 0) {
		ptr = msg;
		len = ret;

		/* Extract a line from the msg and respond to it */
		while ((pos = is_line(ptr, len)) > 0) {
			strncpy(dst, ptr, pos);
			resp_msg(fd, buf, dst - buf + pos);

			dst  = buf;
			ptr += pos+1;
			len -= pos+1;
		}

		/* Preserve the msg and invoke recv() again if it has no line */
		if (len > 0) {
			/* a too long line */
			if (&buf[BUFSIZE-1] < dst+len+strlen(home)) break;

			strncpy(dst, ptr, len);
			dst += len;
		}
	}
	
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
	int                fd1, fd2, len;
	pthread_t          pt;
	
	if ((fd1 = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
		perror("socket");
		return -1;
	}
	
	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family      = AF_INET;
	saddr.sin_port        = htons(PORTNUM);
	saddr.sin_addr.s_addr = htonl(INADDR_ANY);

	if(bind(fd1, (struct sockaddr*)&saddr, sizeof(saddr))) {
		perror("bind");
		return -1;
	}

	if(listen(fd1, 5)) {
		perror("listen");
		return -1;
	}

	for (;;) {
		if((fd2 = accept(fd1, (struct sockaddr*)&caddr, &len)) < 0) {
			perror("accept");
			return -1;
		}
		
		/* Wait if # of threads is MAXCLIENTS */
		pthread_mutex_lock(&mt);
		while(num_of_threads >= MAXCLIENTS) pthread_cond_wait(&ct, &mt);
		num_of_threads++;
		pthread_mutex_unlock(&mt);
		
		if(pthread_create(&pt, NULL, (void*)(recv_and_resp), (void*)fd2) < 0) {
			perror("pthread_create");
			return -1;
		}
		pthread_detach(pt);
		
	}

	return 0;
}
