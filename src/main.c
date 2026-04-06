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

#define HDR_200 "HTTP/1.1 200 OK"
#define HDR_201 "HTTP/1.1 201 Created"
#define HDR_202 "HTTP/1.1 202 Accepted"
#define HDR_404 "HTTP/1.1 404 Not Found"
#define HDR_MAX 4

#define HDR_CONTENT_TYPE "Content-Type: "
#define HDR_CONTENT_LEN "Content-Length: %zu"
#define HDR_CONTENT_ENCODING "Content-Encoding: "

#define CONTENT_TYPE_TEXT "text/plain"
#define CONTENT_TYPE_OCT_STREAM "application/octet-stream"
#define CONTENT_TYPE_JSON "application/json"
#define CONT_TYPES_MAX 3

#define NO_ENCODING ""
#define ENCODING_GZIP "gzip"
#define ENCODING_START 1
#define ENCODING_MAX 2

#define RN "\r\n"

#define error_headers HDR_404 RN RN
// #define success_response(buff, body) snprintf((char *)buff, MAXLINE, success_headers "%s", strlen(body), body)

enum CONTENT_TYPE {
	CONT_TYPE_TEXT,
	CONT_TYPE_OCT_STREAM,
	CONT_TYPE_JSON,
};

enum RES_CODE {
	H200, H201, H202, H404
};

enum CONTENT_ENCODING {
	NO_ENCOD,
	ENCOD_GZIP
};

static const char *const response_codes[HDR_MAX] = {
	[H200]	= HDR_200,
	[H201]	= HDR_201,
	[H202]	= HDR_202,
	[H404]	= HDR_404
};

static const char *const content_types[CONT_TYPES_MAX] = {
	[CONT_TYPE_TEXT]		= CONTENT_TYPE_TEXT,
	[CONT_TYPE_JSON]		= CONTENT_TYPE_JSON,
	[CONT_TYPE_OCT_STREAM]	= CONTENT_TYPE_OCT_STREAM,
};

static const char *const content_encodings[ENCODING_MAX] = {
	[NO_ENCOD]		= NO_ENCODING,
	[ENCOD_GZIP]	= ENCODING_GZIP,
};

static inline int success_response(char *buff, enum CONTENT_TYPE type, enum CONTENT_ENCODING encod, enum RES_CODE res_code, char *body)
{
	char temp = '\0';
	if (!body) body = (void *)&temp;

	char optional_cont_encod[100] = "";
	if (encod != NO_ENCOD) snprintf(optional_cont_encod, 100, HDR_CONTENT_ENCODING "%s" RN, content_encodings[encod]);

	char optional_cont_type[100] = "";
	if (body && strlen(body) > 0) snprintf(optional_cont_type, 100, HDR_CONTENT_TYPE "%s" RN, content_types[type]);

	char optional_cont_length[100] = "";
	if (body && strlen(body) > 0) snprintf(optional_cont_length, 100, HDR_CONTENT_LEN RN, strlen(body));

	int sz = snprintf(buff, 8092, "%s" RN "%s%s%s" RN "%s", 
						response_codes[res_code], optional_cont_type, optional_cont_encod, optional_cont_length, body
					);

	return sz;
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
 * @brief splits str by provided delim string. 
 * unlilke strtok, the delimiter is evaluated as the entire string instead its individual characters.
 */
char *strtok2(char *str, char *delim)
{
	static char *str_p;
	char *delim_pos;
	char *start_pos;
	size_t delim_len;

	if (str != NULL) str_p = str;

	if (str_p == NULL || *str_p == '\0')
		return NULL;

	if (delim == NULL || *delim == '\0')
	{
		start_pos = str_p;
		str_p = NULL;
		return start_pos;
	}

	start_pos = str_p;

	delim_pos = strstr(str_p, delim);
	delim_len = strlen(delim);

	if (delim_pos != NULL)
	{
		*delim_pos = '\0';
		str_p = delim_pos + delim_len;
	}
	else str_p = NULL;

	return start_pos;
}

/**
 * @brief splits text by delimiter, storing pointers to positions of the original text.
 * 
 * @param dest string array containing the split elements - must keep space for final NULL item
 * @param str text to split, gets menipulated
 * @param delim delimiter string
 * @return ssize_t - number of aplit elements inside dest 
 */
size_t split(char **dest, size_t dest_cap, char *str, char *delim)
{
	int 	count = 0;

	char *tok = strtok2(str, delim);
	for (; tok && count < dest_cap - 1; count++){
		dest[count] = tok;
		tok = strtok2(NULL, delim);
	}
	dest[count] = NULL;
	
	return count;
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

int handle_post_request(char *buff, char *url, char **req_headers, char *req_body, int argc, char **argv)
{
	char *base_url 	= strtok(url + 1,  "/");
	char *url_arg  	= url_arg = strtok(NULL, "");

	char *cont_type;
	{
		char *header = str_array_find(req_headers, "Content-Type");
		if (header)
		{
			strtok2(header, ": ");
			cont_type = strtok2(NULL, "");
		}
	}

	int cont_len;
	{
		char *header = str_array_find(req_headers, "Content-Length");
		if (header)
		{
			strtok2(header, ": ");
			cont_len = atoi(strtok2(NULL, RN));
		}
	}

	enum CONTENT_ENCODING cont_encoding = NO_ENCOD;
	{
		char *header = str_array_find(req_headers, "Accept-Encoding");
		if (header)
		{
			strtok2(header, ": ");
			char *req_encod = strtok2(NULL, "");
			for (int i = ENCODING_START; i < ENCODING_MAX; i ++)
				if (strstr(req_encod, content_encodings[i]))
					cont_encoding = i;
		}
	}

	printf ("POST %s requested\n", base_url);
	printf ("arg: %s\n", url_arg);

	if (!strcmp(base_url, "files")){
		if (argc < 3 || strcmp(cont_type, CONTENT_TYPE_OCT_STREAM))
		{
			printf("insufficient args\n");
			return snprintf(buff, MAXLINE, error_headers);
		}

		if (!cont_len)
		{
			printf("no content length header\n");
			return success_response(buff, CONT_TYPE_OCT_STREAM, cont_encoding, H200, NULL);
		}

		char file_name[256];
		snprintf(file_name, MAXLINE, "%s/%s", argv[2], url_arg);
		printf("write content: %s to file: %s\n", req_body, file_name);
		FILE *f = fopen(file_name, "w");
		if (!f) {
			printf("file failed to open\n");
			return snprintf(buff, MAXLINE, error_headers);
		}

		size_t n = fwrite(req_body, cont_len, 1, f);
		if (n != 1){
			printf("error writing to file\n");
			fclose(f);
			return snprintf(buff, MAXLINE, error_headers);
		}

		int err = fclose(f);
		if (err){
			printf("error closing file\n");
			return snprintf(buff, MAXLINE, error_headers);
		}

		return success_response(buff, CONT_TYPE_OCT_STREAM, cont_encoding, H201, NULL);
	}

}


/**
 * @brief examines http request provided in req_url and req_headers, writes http response into buff
 */
int handle_get_request(char *buff, char *url, char **req_headers, int argc, char **argv)
{
	char *user_agent;
	char *base_url = strtok(url,  "/");
	char *url_arg = strtok(NULL, "");

	enum CONTENT_ENCODING cont_encoding = NO_ENCOD;
	{
		char *header = str_array_find(req_headers, "Accept-Encoding");
		if (header)
		{
			strtok2(header, ": ");
			char *req_encod = strtok2(NULL, "");
			printf("requested encoding: %s\n", req_encod);
			for (int i = ENCODING_START; i < ENCODING_MAX; i++)
			{
				printf("projected encoding: %s\n", content_encodings[i]);
				if (strstr(req_encod, content_encodings[i])){
					printf("match\n");
					cont_encoding = i;
				}
			}
		}
	}

	printf("GET %s/%s requested\n", url, url_arg);

	if (!strcmp(url, "/"))
		success_response(buff, CONT_TYPE_TEXT, cont_encoding, H200, "");

	else if (!base_url)
		snprintf(buff, MAXLINE, error_headers);

	else if (!strcmp(base_url, "echo"))
		success_response(buff, CONT_TYPE_TEXT, cont_encoding, H200, url_arg);

	else if (!strcmp(base_url, "user-agent"))
		if ((user_agent = str_array_find(req_headers, "User-Agent:")))
			success_response(buff, CONT_TYPE_TEXT, cont_encoding, H200, &user_agent[12]);
		else
			snprintf(buff, MAXLINE, error_headers);

	else if(!strcmp(base_url, "files"))
	{
		if (argc < 3 && strncmp(argv[1], "--directory", 11))
			snprintf(buff, MAXLINE, error_headers);
		else
		{
			char 	filename[255], data[8092];
			int 	c = 0;
			size_t 	sz;

			snprintf(filename, 255, "%s/%s", argv[2], url_arg);
			FILE *f = fopen(filename, "r");
			
			if (!f)
				snprintf(buff, MAXLINE, error_headers);
			
			else if (0 > (sz = fread(data, 1, MAXLINE, f)))
				snprintf(buff, MAXLINE, error_headers);
			
			else success_response(buff, CONT_TYPE_OCT_STREAM, cont_encoding, H200, data);
		}
	}
	else 
		snprintf(buff, MAXLINE, error_headers);
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
		char 			   *split_line[3] = {0};
		char 			   *req_headers[10] = {0};
		char 			   *req_url;

		// wait for http request events, then read request from each events connection fd
		if (-1 == (nfds = epoll_wait(epollfd, events, MAX_EVENTS, 1)))
			printf("epoll_wait error\n");

		for (int i = 0; i < nfds; ++i)
		{
			memset(recvline, 0, MAXLINE);
			memset(buff, 0, MAXLINE);

			while ((n = read(events[i].data.fd, recvline, MAXLINE - 1)) > 0)
				if (strstr((char *)recvline, "\r\n\r\n"))
					break;

			if (n <= 0)
				continue;
	
			split(split_line, 3, (char *)recvline, "\r\n\r\n");
			
			char *headers = split_line[0];
			char *body = split_line[1];
			
			split(req_headers, 10, headers, "\r\n");
			char *method = strtok(req_headers[0], " ");
			char *url = strtok(NULL, " ");
	
			if (strstr(method, "GET"))
				handle_get_request((char *)buff, url, req_headers + 1, argc, argv);
			
			else if (strstr(method, "POST"))
			{
				if (!body) continue;
				handle_post_request((char *)buff, url, req_headers + 1, body, argc, argv);
			}

			else{
				printf("Not a valid HTTP request\n");
				close(events[i].data.fd);
				memset(req_headers, 0, sizeof(req_headers));
				continue;
			}
			
			printf("%s\n", buff);
			
			write(events[i].data.fd, buff, strlen((char *)buff));
			memset(req_headers, 0, sizeof(req_headers));
		}


		fcntl(listenfd, F_SETFL, O_NONBLOCK);
		if (clients < MAX_CLIENTS)
			if (accept_connection(listenfd, epollfd, ev[clients]) == 0)
				clients++;
	}
	return 0;
}
