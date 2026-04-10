#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>

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
 * @param dest_cap size of dest - number of items 
 * @param str text to split, gets menipulated
 * @param delim delimiter string
 * @return int - number of aplit elements inside dest 
 */
int split(char **dest, int dest_cap, char *str, char *delim)
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

/**
 * @brief modifies str in place, replacing first trailing whitespace with null terminator
 * 
 * @return pointer to first non-whitespace character inside modified str
 */
char *trim(char *str)
{
	char *start = str;
	while(isspace((unsigned char) *start)) start++;

	char *end = start + strlen(start) - 1;
	while(end > start && isspace((unsigned char) *end)) end--;

	*(end + 1) = '\0';
	return start;
}
