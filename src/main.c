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

#define SERVER_PORT 4221
#define MAXLINE 4096

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
 * @brief splits text by delimiter
 * 
 * @param str text to split
 * @param delim delimiter string
 * @return char** - a newly allocated string array containing the split resulting elements 
 */
char **split(char *str, char *delim)
{
	size_t capacity = 10;
	char **arr = malloc(capacity * sizeof(char *));
	char *tok = strtok(str, delim);
	int i = 0;
	for (; tok; i++)
	{
		if (i >= capacity)
		{
			capacity += 5;
			arr = realloc(arr, capacity * sizeof(char *));
		}
		arr[i] = tok;
		tok = strtok(NULL, delim);
	}
	
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

	int 				listenfd, connfd;
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

	printf("Waiting for a client to connect...\n");
	
	for (;;)
	{
		ssize_t 		   n;
		struct sockaddr_in client_addr;
		char 			   s_client_addr[INET_ADDRSTRLEN];
		socklen_t 		   client_addr_len;

		char **req_headers;
		char *get_url, *user_agent, *url;
		
		client_addr_len = sizeof(client_addr);
		
		connfd = accept(listenfd, (struct sockaddr *) &client_addr, &client_addr_len);
		printf("Client connected\n");
		inet_ntop(AF_INET, &client_addr.sin_addr, s_client_addr, INET_ADDRSTRLEN);

		// printf("Client %s:%d connected\n", s_client_addr, ntohs(client_addr.sin_port));

		memset(recvline, 0, MAXLINE);

		while ((n = read(connfd, recvline, MAXLINE - 1)) > 0)
			if (strstr((char *)recvline, "\r\n\r\n"))
				break;


		if (n < 0)
			err_n_die("read error.");

		req_headers = split((char *)recvline, "\r\n");
		if (!(get_url = str_array_find(req_headers, "GET /")))
		{
			printf("Not a GET request\n");
			close(connfd);
			continue;
		}	
		
		url = strstr(get_url, "/");
		if (0 == strncmp(url, "/echo", 5))
		{
			char *body = strtok(&url[6], " ");
			success_response(buff, body);
		}

		if (0 == strncmp(url, "/user-agent", 11))
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
		
		

		write(connfd, buff, strlen((char *)buff));
		close(connfd);
	}

	return 0;
}
