#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <stdlib.h>
#include "global.h"
#include "encoding.h"
#include "utils.h"

#define SERVER_PORT 4221
#define MAXLINE 4096
#define MAX_EVENTS 10
#define MAX_CLIENTS 10

static inline size_t success_response(char *buff, enum CONTENT_TYPE type, enum CONTENT_ENCODING encod, enum RES_CODE res_code, char *body)
{

	char ch = '\0';
	if (!body) body = (void *)&ch;

	FILE 	*in, *out;
	size_t	size = strlen(body);
	char 	*out_p = body;

	char optional_cont_encod[100] = "";
	if (encod != NO_ENCOD) {
		snprintf(optional_cont_encod, 100, HDR_CONTENT_ENCODING "%s" RN, content_encodings[encod]);
		in = fmemopen(body, strlen(body), "r");
		out = open_memstream(&out_p, &size);

		if(!in || !out) perror("memopen");
		else {
			if (encod == ENCOD_GZIP){
				int ret = gzip_defl(in, out, -1);
				if (ret != Z_OK)
					printf("Compression failed: %d\n", ret);
			}
			
			fclose(in);
			fclose(out);
		}

	}

	char optional_cont_type[100] = "";
	if (size > 0) snprintf(optional_cont_type, 100, HDR_CONTENT_TYPE "%s" RN, content_types[type]);

	char optional_cont_length[100] = "";
	if (size > 0) snprintf(optional_cont_length, 100, HDR_CONTENT_LEN RN, size);

	int sz = snprintf(buff, 8092, "%s" RN "%s%s%s" RN, 
						response_codes[res_code], optional_cont_type, optional_cont_encod, optional_cont_length
					);

	if (size > 0) memcpy(buff + sz, out_p, size);

	return sz + size;
}

/**
 * @param src array of strings, each item should contain one http-header
 * @param target pointer to a `http_headers` type, will be populated by the respective values taken from src
 */
void parse_http_headers(char **src, http_headers *target)
{
	target->cont_type = NULL;
	{
		char *header = str_array_find(src, "Content-Type");
		if (header)
		{
			strtok2(header, ": ");
			target->cont_type = strtok2(NULL, "");
		}
	}

	target->cont_len = 0;
	{
		char *header = str_array_find(src, "Content-Length");
		if (header)
		{
			strtok2(header, ": ");
			target->cont_len = atoi(strtok2(NULL, RN));
		}
	}

	target->cont_encoding = NO_ENCOD;
	{
		char *header = str_array_find(src, "Accept-Encoding");
		if (header)
		{
			strtok2(header, ": ");
			char *req_encod = strtok2(NULL, "");
			char *dest[10] = {0};
			int count = split(dest, 10, req_encod, ",");
			for (int j = 0; j < count; j++)
				for (int i = ENCODING_START; i < ENCODING_MAX; i ++)
					if (!strcmp(trim(dest[j]), content_encodings[i]))
						target->cont_encoding = i;
		}
	}
}


size_t handle_post_request(char *buff, char *url, char **req_headers, char *req_body, int argc, char **argv)
{
	http_headers headers;
	char *base_url 		 	= strtok(url + 1,  "/");
	char *url_arg  			= url_arg = strtok(NULL, "");
	
	parse_http_headers(req_headers, &headers);

	printf ("POST %s requested\n", base_url);
	printf ("arg: %s\n", url_arg);

	if (!strcmp(base_url, "files")){
		if (argc < 3 || strcmp(headers.cont_type, CONTENT_TYPE_OCT_STREAM))
		{
			printf("insufficient args\n");
			return snprintf(buff, MAXLINE, error_headers);
		}

		if (!headers.cont_len)
		{
			printf("no content length header\n");
			return success_response(buff, CONT_TYPE_OCT_STREAM, headers.cont_encoding, H200, NULL);
		}

		char file_name[256];
		snprintf(file_name, MAXLINE, "%s/%s", argv[2], url_arg);
		FILE *f = fopen(file_name, "w");
		if (!f) {
			printf("file failed to open\n");
			return snprintf(buff, MAXLINE, error_headers);
		}

		size_t n = fwrite(req_body, headers.cont_len, 1, f);
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

		return success_response(buff, CONT_TYPE_OCT_STREAM, headers.cont_encoding, H201, NULL);
	}

}


/**
 * @brief examines http request provided in req_url and req_headers, writes http response into buff
 * 
 * 
 */
int handle_get_request(char *buff, char *url, char **req_headers, int argc, char **argv)
{
	char *user_agent;
	char *base_url = strtok(url,  "/");
	char *url_arg = strtok(NULL, "");
	http_headers headers;

	parse_http_headers(req_headers, &headers);

	printf("GET /%s/%s requested\n", base_url, url_arg);

	if (!strcmp(url, "/"))
		return success_response(buff, CONT_TYPE_TEXT, headers.cont_encoding, H200, "");

	else if (!base_url)
		return snprintf(buff, MAXLINE, error_headers);

	else if (!strcmp(base_url, "echo"))
		return success_response(buff, CONT_TYPE_TEXT, headers.cont_encoding, H200, url_arg);

	else if (!strcmp(base_url, "user-agent"))
		if ((user_agent = str_array_find(req_headers, "User-Agent:")))
			return success_response(buff, CONT_TYPE_TEXT, headers.cont_encoding, H200, &user_agent[12]);
		else
			return snprintf(buff, MAXLINE, error_headers);

	else if(!strcmp(base_url, "files"))
	{
		if (argc < 3 && strncmp(argv[1], "--directory", 11))
			return snprintf(buff, MAXLINE, error_headers);
		else
		{
			char 	filename[255], data[8092];
			int 	c = 0;
			size_t 	sz;

			snprintf(filename, 255, "%s/%s", argv[2], url_arg);
			FILE *f = fopen(filename, "r");
			
			if (!f)
				return snprintf(buff, MAXLINE, error_headers);
			
			else if (0 > (sz = fread(data, 1, MAXLINE, f)))
				return snprintf(buff, MAXLINE, error_headers);
			
			else 
				return success_response(buff, CONT_TYPE_OCT_STREAM, headers.cont_encoding, H200, data);
		}
	}
	else
		return snprintf(buff, MAXLINE, error_headers);
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
		size_t			   res_size;
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
				res_size = handle_get_request((char *)buff, url, req_headers + 1, argc, argv);
			
			else if (strstr(method, "POST"))
			{
				if (!body) continue;
				res_size = handle_post_request((char *)buff, url, req_headers + 1, body, argc, argv);
			}

			else{
				printf("Not a valid HTTP request\n");
				close(events[i].data.fd);
				memset(req_headers, 0, sizeof(req_headers));
				continue;
			}
			
			write(events[i].data.fd, buff, res_size);
			memset(req_headers, 0, sizeof(req_headers));
		}


		fcntl(listenfd, F_SETFL, O_NONBLOCK);
		if (clients < MAX_CLIENTS)
			if (accept_connection(listenfd, epollfd, ev[clients]) == 0)
				clients++;
	}
	return 0;
}
