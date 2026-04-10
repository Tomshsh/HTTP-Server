#define HDR_200 "HTTP/1.1 200 OK"
#define HDR_201 "HTTP/1.1 201 Created"
#define HDR_202 "HTTP/1.1 202 Accepted"
#define HDR_404 "HTTP/1.1 404 Not Found"
#define HDR_MAX 4

#define HDR_CONTENT_TYPE "Content-Type: "
#define HDR_CONTENT_LEN "Content-Length: %zu"
#define HDR_CONTENT_ENCODING "Content-Encoding: "
#define HDR_CONNECTION_CLOSE "Connection: close"

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

typedef struct http_headers_t{
	enum CONTENT_ENCODING cont_encoding;
	char *cont_type;
	int cont_len;
	int close;
} http_headers;