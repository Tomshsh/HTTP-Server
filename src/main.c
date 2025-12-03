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

#define success_headers HDR_200 CONTENT_TEXT CONTENT_LEN "\r\n"
#define success_response(buff, body) snprintf(buff, sizeof(buff), success_headers "%s", strlen(body), body)

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
		
		client_addr_len = sizeof(client_addr);
		
		connfd = accept(listenfd, (struct sockaddr *) &client_addr, &client_addr_len);
		printf("Client connected\n");
		inet_ntop(AF_INET, &client_addr.sin_addr, s_client_addr, INET_ADDRSTRLEN);

		// printf("Client %s:%d connected\n", s_client_addr, ntohs(client_addr.sin_port));

		memset(recvline, 0, MAXLINE);

		while ((n = read(connfd, recvline, MAXLINE - 1)) > 0)
		{
			// printf("%s\n", recvline);
			
			if (strstr((char *)&recvline, "\r\n\r\n"))
				break;
		}


		if (n < 0)
			err_n_die("read error.");

		char *rqst_hdr;
		if (!(rqst_hdr = strstr((char *)&recvline, "GET /")))
		{
			close(connfd);
			continue;
		}

		
		char *path = strtok(rqst_hdr + 4, " ");
		if (0 == strncmp(path, "/echo/", 6))
		{
			char *body = strtok(&path[6], " ");
			success_response((char *)buff, body);
		}

		else if (0 == strcmp(path, "/"))
			snprintf((char *)buff, sizeof(buff), HDR_200);


		else 
			snprintf((char *)buff, sizeof(buff), HDR_404);
		
		

		// snprintf((char *)buff, sizeof(buff), 
		// 	"HTTP/1.1 200 OK\r\n"
			// "Content-Type: text/plain\r\n"
			// "Content-Length: 5\r\n"
			// "\r\n"
			// "Hello"
		// );


		write(connfd, buff, strlen((char *)buff));
		close(connfd);
	}

	return 0;
}
