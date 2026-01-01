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
#define HDR_CONTENT_TYPE "Content-Type: "
#define CONTENT_TYPE_TEXT "text/plain\r\n"
#define CONTENT_TYPE_OCT_STREAM "application/octet-stream\r\n"
#define CONTENT_LEN "Content-Length: %zu\r\n"

#define success_headers HDR_200 HDR_CONTENT_TYPE CONTENT_TYPE_TEXT CONTENT_LEN "\r\n"
#define error_headers HDR_404 "\r\n"
// #define success_response(buff, body) snprintf((char *)buff, MAXLINE, success_headers "%s", strlen(body), body)

#define ERROR_404_LEN 28
#define SUCCESS_200_LEN 20

enum CONTENT_TYPE {
	CONT_TYPE_TEXT,
	CONT_TYPE_OCT_STREAM,
	CONT_TYPE_JSON
};

static inline int success_response(char *buff, enum CONTENT_TYPE type, char *body){
	char content_type[50];
	switch (type)
	{
		case CONT_TYPE_TEXT:
			memcpy(content_type, CONTENT_TYPE_TEXT, sizeof(content_type));
			break;
		case CONT_TYPE_OCT_STREAM:
			memcpy(content_type, CONTENT_TYPE_OCT_STREAM, sizeof(content_type));
			break;
		case CONT_TYPE_JSON:
			// todo
			break;
		
		default:
			break;
	}

	return snprintf(buff, 8092, HDR_200 HDR_CONTENT_TYPE "%s" CONTENT_LEN "\r\n%s", content_type, strlen(body), body);
}

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
 * 
 * TODO this uses a dynamic string allocator implementation that needs to be decoupled into a seperate function
 */
char **split(char *str, char *delim)
{
	char 	**arr = NULL;
	size_t 	capacity = 0;
	int 	count = 0;

	char *tok = strtok(str, delim);
	while (tok)
	{
		// Ensure room for tokens + final NULL
		if (count + 1 >= (int)capacity)
		{
			capacity += 20;
			if ((arr = realloc(arr, capacity * sizeof(char *))) == NULL)
				err_n_die("realloc error 1");
		}
		
		arr[count++] = tok;
		tok = strtok(NULL, delim);
	}
	
	arr[count] = NULL;
	return arr;
}

/**
 * @brief find text element substr in array a
 * 
 * @returns pointer to the matching element inside a
 */
char *str_array_find(char **a, const char *substr)
{
	for (int i = 0; a[i]; i++) 
		if (strstr(a[i], substr))
			return a[i];
	
	return NULL;
}


/**
 * @brief examines http request provided in req_url and req_headers, writes http response into buff
 */
void handle_get_request(char *buff, char *req_url, char **req_headers, int argc, char **argv)
{
	char *user_agent, *url;

	url = strtok(strstr(req_url, "/"), " ");
	printf("%s requested\n", url);
	
	if (0 == strncmp(url, "/echo", 5))
		success_response(buff, CONT_TYPE_TEXT, &url[6]);

	else if (0 == strncmp(url, "/user-agent", 11))
		if ((user_agent = str_array_find(req_headers, "User-Agent:")))
			success_response(buff, CONT_TYPE_TEXT, &user_agent[12]);
		else
			snprintf(buff, ERROR_404_LEN, error_headers);

	else if(0 == strncmp(url, "/files", 6))
	{
		if (argc < 3 && strncmp(argv[1], "--directory", 11))
			snprintf(buff, ERROR_404_LEN, error_headers);
		else
		{
			char 	filename[255], data[8092];
			int 	c = 0;
			size_t 	sz;

			snprintf(filename, 255, "%s%s", argv[2], &url[6]);
			FILE *f = fopen(filename, "r");
			
			if (!f)
				snprintf(buff, ERROR_404_LEN, error_headers);
			
			else if (0 > (sz = fread(data, 1, MAXLINE, f)))
				snprintf(buff, ERROR_404_LEN, error_headers);
			
			else success_response(buff, CONT_TYPE_OCT_STREAM, data);
		}
	}
	else if (0 == strcmp(url, "/"))
		snprintf(buff, sizeof(buff), HDR_200 "\r\n");
	else 
		snprintf(buff, ERROR_404_LEN, error_headers);
}

/**
 * @brief accepts a new connection and creates an epoll event.
 * 
 * @param listenfd socket file descriptor
 * @param epollfd epoll file descriptor
 * @param ev epoll event
 * 
 * @return 0 for success, -1 for failure
 */
int accept_connection(int listenfd, int epollfd, struct epoll_event ev)
{
	struct sockaddr_in client_addr;
	socklen_t 		   client_addr_len;
	int 			   connfd;
	char 			   s_client_addr[INET_ADDRSTRLEN];

	
	client_addr_len = sizeof(client_addr);
	
	connfd = accept(listenfd, (struct sockaddr *) &client_addr, &client_addr_len);
	if (connfd <= 0)
		return -1;
	
	printf("adding epoll event for connfd: %d\n", connfd);
	ev.events = EPOLLIN;
	ev.data.fd = connfd;
	if (-1 == epoll_ctl(epollfd, EPOLL_CTL_ADD, connfd, &ev))
	{
		printf("epoll ctl add error\n");
		write(connfd, error_headers, 255);
		close(connfd);
		return -1;
	}
	inet_ntop(AF_INET, &client_addr.sin_addr, s_client_addr, INET_ADDRSTRLEN);
	
	// printf("Client %s:%d connected\n", s_client_addr, ntohs(client_addr.sin_port));
	return 0;
}



int main(int argc, char **argv)
{
	// Disable output buffering
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	int 				listenfd, nfds, epollfd, clients = 0;
	struct epoll_event	ev[MAX_CLIENTS], events[MAX_EVENTS];
	struct sockaddr_in 	servaddr;
	uint8_t 			buff[MAXLINE + 1];
	uint8_t 			recvline[MAXLINE + 1];

	if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		err_n_die("socket error.");

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
		char 			   **req_headers;
		char 			   *get_url;

		// wait for http request events, then read request from each events connection fd
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
	
			printf("READ FROM connfd %d %d bytes\n", events[i].data.fd, (int)n);
			req_headers = split((char *)recvline, "\r\n");
	
			if (!(get_url = str_array_find(req_headers, "GET /")))
			{
				printf("Not a GET request\n");
				close(events[i].data.fd);
				free(req_headers);
				req_headers = NULL;
				continue;
			}	
			
			handle_get_request((char *)buff, get_url, req_headers, argc, argv);
			
			printf("write to connfd %d\n", events[i].data.fd);
			write(events[i].data.fd, buff, strlen((char *)buff));
			free(req_headers);
			req_headers = NULL;
		}


		fcntl(listenfd, F_SETFL, O_NONBLOCK);
		if (clients < MAX_CLIENTS)
			if (accept_connection(listenfd, epollfd, ev[clients]) == 0)
				clients++;
	}
	return 0;
}
