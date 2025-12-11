#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>

#define SERVER_PORT 4221
#define MAXLINE 4096
#define MAX_EVENTS 10
#define MAX_CLIENTS 10

#define HDR_200 "HTTP/1.1 200 OK\r\n"
#define HDR_404 "HTTP/1.1 404 Not Found\r\n"
#define CONTENT_TEXT "Content-Type: text/plain\r\n"
#define CONTENT_LEN "Content-Length: %zu\r\n"

#define success_headers HDR_200 CONTENT_TEXT CONTENT_LEN
#define error_headers HDR_404
#define success_response(buff, body) snprintf((char *)buff, sizeof(buff), success_headers "\r\n" "%s", strlen(body), body)

int err_n_die(const char *fmt, ...)
{
	int 	errno_save;
	va_list ap;

	errno_save = errno;

	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	fprintf(stdout, "\n");
	fflush(stdout);

	if (errno_save != 0)
	{
		fprintf(stdout, "(errno = %d) : %s\n", errno_save,
				strerror(errno_save));
		fprintf(stdout, "\n");
		fflush(stdout);
	}
	va_end(ap);

	exit(1);
}

/**
 * @brief splits text by delimiter, storing pointers to positions of the original text.
 * 
 * @param str text to split
 * @param delim delimiter string
 * @return char** - a newly allocated string array containing the split elements 
 */
char **split(char *str, char *delim)
{
	char **arr = NULL;
	size_t capacity = 10;
	if ((arr = malloc(capacity * sizeof(char *))) == NULL)
		err_n_die("malloc error");

	memset(arr, 0, capacity * sizeof(char *));
	char *tok = strtok(str, delim);
	int i = 0;
	for (; tok; i++)
	{
		if (i >= (int)capacity)
		{
			capacity += 5;
			if ((arr = realloc(arr, capacity * sizeof(char *))) == NULL);
				err_n_die("realloc error");

			memset(arr + capacity - 5, 0, 5 * sizeof(char *));
		}
		arr[i] = tok;
		tok = strtok(NULL, delim);
	}
	
	if (i >= (int)capacity)
		if ((arr = realloc(arr, (capacity + 1) * sizeof(char *))) == NULL)
			err_n_die("realloc error");

	arr[i] = NULL;
	return arr;
}

char *str_array_find(char **a, const char *substr)
{
	for (int i = 0; a[i]; i++) 
		if (strstr(a[i], substr))
			return a[i];
	
	return NULL;
}

int main()
{
	// Disable output buffering
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	// You can use print statements as follows for debugging, they'll be visible when running tests.
	printf("Logs from your program will appear here!\n");

	int 				listenfd, connfd, nfds, epollfd, clients = 0;
	struct epoll_event	ev[MAX_CLIENTS], events[MAX_EVENTS];
	struct sockaddr_in 	servaddr;
	uint8_t 			buff[MAXLINE + 1];
	uint8_t 			recvline[MAXLINE + 1];

	if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		err_n_die("socket error.");

	// // Since the tester restarts your program quite often, setting SO_REUSEADDR
	// // ensures that we don't run into 'Address already in use' errors
	int reuse = 1;
	if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
		err_n_die("SO_REUSEADDR failed.");

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family		 = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port		 = htons(SERVER_PORT);

	if (bind(listenfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0)
		err_n_die("bind error.");
	
	if (listen(listenfd, 10) < 0)
		err_n_die("listen error.");

	printf("Waiting for a client to connect....\n");

	if (-1 == (epollfd = epoll_create1(0)))
		err_n_die("epoll_create error");

	while (1)
	{
		ssize_t 		   n;
		struct sockaddr_in client_addr;
		char 			   s_client_addr[INET_ADDRSTRLEN];
		socklen_t 		   client_addr_len;
		char 			   **req_headers;
		char 			   *get_url, *user_agent, *url;

		if (-1 == (nfds = epoll_wait(epollfd, events, MAX_EVENTS, 1)))
			printf("epoll_wait error\n");

		for (int i = 0; i < nfds; ++i)
		{
			memset(recvline, 0, MAXLINE);
			memset(buff, 0, MAXLINE);

			while ((n = read(events[i].data.fd, recvline, MAXLINE - 1)) > 0)
			{
				if (strstr((char *)recvline, "\r\n\r\n"))
					break;
			}

			if (n <= 0)
				continue;
	
			printf("READ FROM connfd %d %d bytes\n", events[i].data.fd, n);
	
			req_headers = split((char *)recvline, "\r\n");
	
			if (!(get_url = str_array_find(req_headers, "GET /")))
			{
				printf("Not a GET request\n");
				close(events[i].data.fd);
				free(req_headers);
				req_headers = NULL;
				continue;
			}	
			
			
			url = strtok(strstr(get_url, "/"), " ");
			printf("%s requested\n", url);
			if (0 == strncmp(url, "/echo", 5))
				success_response(buff, &url[6]);
	
			else if (0 == strncmp(url, "/user-agent", 11))
			{
				if ((user_agent = str_array_find(req_headers, "User-Agent:")))
					success_response(buff, &user_agent[12]);
				else
					snprintf((char *)buff, sizeof(buff), error_headers "\r\n");
			}		
	
			else if (0 == strcmp(url, "/"))
				snprintf((char *)buff, sizeof(buff), HDR_200 "\r\n");
	
	
			else 
				snprintf((char *)buff, sizeof(buff), error_headers "\r\n");
			
			printf("write to connfd %d\n", events[i].data.fd);
			write(events[i].data.fd, buff, strlen((char *)buff));
			free(req_headers);
			req_headers = NULL;
		}
		
		client_addr_len = sizeof(client_addr);

		fcntl(listenfd, F_SETFL, O_NONBLOCK);
		if (clients < MAX_CLIENTS)
		{
			connfd = accept(listenfd, (struct sockaddr *) &client_addr, &client_addr_len);
			if (connfd <= 0)
				continue;
			
			printf("adding epoll event for connfd: %d\n", connfd);
			ev[clients].events = EPOLLIN;
			ev[clients].data.fd = connfd;
			if (-1 == epoll_ctl(epollfd, EPOLL_CTL_ADD, connfd, &ev[clients]))
			{
				printf("epoll ctl add error\n");
				write(connfd, error_headers "\r\n", 255);
				close(connfd);
				continue;
			}
			clients++;
			inet_ntop(AF_INET, &client_addr.sin_addr, s_client_addr, INET_ADDRSTRLEN);
		}

		// printf("Client %s:%d connected\n", s_client_addr, ntohs(client_addr.sin_port));


	}

	return 0;
}
