// File: src/includes.h
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <errno.h>
#include <stddef.h>

// File: src/list.h
struct list_meta {
	struct list_node *head, *tail;
	size_t node_size;
};

struct list_node {
	struct list_meta *meta;
	struct list_node *prev, *next;
};


#define list_new(type) __list_new(sizeof (*type), NULL)
#define list_alloc_next(entry) __list_alloc_next(entry, sizeof (typeof (*entry)))
#define list_alloc_prev(entry) __list_alloc_prev(entry, sizeof (typeof (*entry)))
#define list_alloc_at_end(entry) __list_alloc_at_end(entry, sizeof (typeof (*entry)))
#define list_alloc_at_start(entry) __list_alloc_at_start(entry, sizeof (typeof (*entry)))
#define list_foreach(start, entry) for (entry = start; entry != NULL; entry = list_get_next(entry))

#define list_search_by_elem(entry, elem, value)				\
({									\
	typeof (entry) __p;						\
									\
	for (__p = entry; __p != NULL; __p = list_get_next(__p))	\
		if (__p->elem == (value))				\
			break;						\
									\
	__p;								\
})

#define list_search_by_str(entry, elem, str)				\
({									\
	typeof (entry) __p;						\
									\
	for (__p = entry; __p != NULL; __p = list_get_next(__p))	\
		if (!strcmp(__p->elem, str))				\
			break;						\
									\
	__p;								\
})

inline static void* __list_new(size_t size, struct list_meta *meta);
inline static void* list_get_head(void *entry);
inline static void* list_get_tail(void *entry);
inline static void* list_get_prev(void *entry);
inline static void* list_get_next(void *entry);
inline static void* __list_alloc_next(void *entry, size_t size);
inline static void* __list_alloc_prev(void *entry, size_t size);
inline static void* __list_alloc_at_end(void *entry, size_t size);
inline static void* __list_alloc_at_start(void *entry, size_t size);
inline static void* list_free(void *entry);
inline static void  list_destroy(void *entry);

// File: src/log.h
#ifndef FULL_LOG
#define _int_logit _int_logit_short
#else
#define _int_logit _int_logit_full
#endif

#define _int_logit_short(fp, prefix, fmt, ...)				\
do {									\
	fprintf(fp, prefix "%20.20s| " fmt "\n",			\
	    __FUNCTION__, ##__VA_ARGS__);				\
	fflush(fp);							\
} while (0)

#define _int_logit_full(fp, prefix, fmt, ...)				\
do {									\
	fprintf(fp, prefix "%7.7s:%-5d %20.20s| " fmt "\n",		\
	    __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__);		\
	fflush(fp);							\
} while (0)

#define debug(fmt, ...)   _int_logit(stderr, "[D] ", fmt, ##__VA_ARGS__)
#define warning(fmt, ...) _int_logit(stderr, "[W] ", fmt, ##__VA_ARGS__)

#define error(ecode, fmt, ...)						\
do {									\
	_int_logit(stderr, "[E] ", fmt, ##__VA_ARGS__);			\
	exit(ecode);							\
} while (0)

#define perror(str)							\
do {									\
	char buf[64];							\
									\
	strerror_r(errno, buf, 64);					\
	_int_logit(stderr, "[E] ", "%s: %s", str, buf);			\
} while (0)

#undef assert
#define assert(exp)							\
do {									\
	if (!(exp)) {							\
		_int_logit_full(stderr, "[A] ", "%s", #exp);		\
		exit(1);						\
	}								\
} while (0)

// File: src/misc.h
#define typeof(a) __typeof__(a)

#define sqr(x) ((double)(x)*(x))

#define UNUSED(x) (void)x

#define SWAP(a, b) do {typeof (a) __tmp = a; a = b; b = __tmp;} while (0)
#define SWAP_MW(a, b) do {a ^= b; b ^= a; a ^= b;} while(0)

#define MAX(a, b) ((a > b) ? a : b)
#define MIN(a, b) ((a > b) ? b : a)

#define SYSCALL(estat, func, ...)					\
({									\
	int __ret;							\
									\
									\
	while ((__ret = ((func)(__VA_ARGS__))) == -1 && errno == EINTR)	\
		;							\
									\
	if (__ret == -1) {						\
		perror(#func "(" #__VA_ARGS__ ")");			\
		if (estat != 0)						\
			exit(estat);					\
	}								\
									\
	__ret;								\
})

inline static void* xmalloc(size_t size);
inline static void* xrealloc(void *ptr, size_t size);

// File: src/strings.h
typedef char* string;

enum str_opts {
	STROPT_DYNAMIC,
};

struct str_string {
	int isdynamic;
	size_t len;
	size_t bufsize;
};

inline static string str_new(size_t len);
inline static void str_setopt(string str, enum str_opts opt, int val);
inline static size_t str_len(string str);

inline static string str_set(string str, const char *s);
inline static string str_cat(string str, const char *src);
inline static string str_clone(string str);
inline static string str_range(string str, size_t start, size_t end);
inline static string* str_split(string str, const char *delim);

inline static void str_free(string str);
inline static void str_arr_free(string *strs);

// File: src/list.c
inline static void*
__list_new(size_t size, struct list_meta *meta)
{
	uint8_t *p;
	struct list_node *hdr;

	hdr = malloc(size + sizeof (struct list_node));
	p = (uint8_t*)hdr + sizeof (struct list_node);

	if (meta == NULL) {
		hdr->meta = malloc(sizeof (struct list_meta));
		hdr->meta->head = hdr;
		hdr->meta->tail = hdr;
		hdr->meta->node_size = size;
	}
	else
		hdr->meta = meta;

	hdr->prev = NULL;
	hdr->next = NULL;

	return p;
}

inline static void*
list_get_head(void *entry)
{
	uint8_t *p;
	struct list_node *hdr;
	struct list_meta *meta;

	assert(entry && "Argument is NULL");

	p = entry;
	hdr = (struct list_node*)(p - sizeof (struct list_node));
	meta = hdr->meta;

	return (uint8_t*)meta->head + sizeof (struct list_node);
}

inline static void*
list_get_tail(void *entry)
{
	uint8_t *p;
	struct list_node *hdr;
	struct list_meta *meta;

	assert(entry && "Argument is NULL");

	p = entry;
	hdr = (struct list_node*)(p - sizeof (struct list_node));
	meta = hdr->meta;

	return (uint8_t*)meta->tail + sizeof (struct list_node);
}

inline static void*
list_get_prev(void *entry)
{
	uint8_t *p;
	struct list_node *hdr;

	assert(entry && "Argument is NULL");

	p = entry;
	hdr = (struct list_node*)(p - sizeof (struct list_node));

	hdr = hdr->prev;
	if (hdr == NULL)
		p = NULL;
	else
		p = (uint8_t*)hdr + sizeof (struct list_node);

	return p;
}

inline static void*
list_get_next(void *entry)
{
	uint8_t *p;
	struct list_node *hdr;

	assert(entry && "Argument is NULL");

	p = entry;
	hdr = (struct list_node*)(p - sizeof (struct list_node));

	hdr = hdr->next;
	if (hdr == NULL)
		p = NULL;
	else
		p = (uint8_t*)hdr + sizeof (struct list_node);

	return p;
}

inline static void*
__list_alloc_next(void *entry, size_t size)
{
	uint8_t *new_p, *p;
	struct list_node *new_hdr, *hdr;
	struct list_meta *meta;

	if (entry == NULL)
		return __list_new(size, NULL);

	p = entry;
	hdr = (struct list_node*)(p - sizeof (struct list_node));
	meta = hdr->meta;
	new_p = __list_new(size, meta);
	new_hdr = (struct list_node*)(new_p - sizeof (struct list_node));

	if (hdr->next == NULL)
		meta->tail = new_hdr;
	else
		hdr->next->prev = new_hdr;

	new_hdr->meta = meta;
	new_hdr->next = hdr->next;
	new_hdr->prev = hdr;
	hdr->next = new_hdr;

	return new_p;
}

inline static void*
__list_alloc_prev(void *entry, size_t size)
{
	uint8_t *new_p, *p;
	struct list_node *new_hdr, *hdr;
	struct list_meta *meta;

	if (entry == NULL)
		return __list_new(size, NULL);

	p = entry;
	hdr = (struct list_node*)(p - sizeof (struct list_node));
	meta = hdr->meta;
	new_p = __list_new(size, meta);
	new_hdr = (struct list_node*)(new_p - sizeof (struct list_node));

	if (hdr->prev == NULL)
		meta->head = new_hdr;
	else
		hdr->prev->next = new_hdr;

	new_hdr->meta = meta;
	new_hdr->prev = hdr->prev;
	new_hdr->next = hdr;
	hdr->prev = new_hdr;

	return new_p;
}

inline static void*
__list_alloc_at_end(void *entry, size_t size)
{
	if (entry == NULL)
		return __list_new(size, NULL);

	entry = list_get_tail(entry);

	return __list_alloc_next(entry, size);

}

inline static void*
__list_alloc_at_start(void *entry, size_t size)
{
	if (entry == NULL)
		return __list_new(size, NULL);

	entry = list_get_head(entry);

	return __list_alloc_prev(entry, size);
}

inline static void*
list_free(void *entry)
{
	uint8_t *p;
	struct list_node *hdr;
	struct list_meta *meta;

	if (entry == NULL)
		return NULL;

	p = entry;
	hdr = (struct list_node*)(p - sizeof (struct list_node));
	meta = hdr->meta;

	if (hdr->prev == NULL)
		meta->head = hdr->next;
	else
		hdr->prev->next = hdr->next;

	if (hdr->next == NULL)
		meta->tail = hdr->prev;
	else
		hdr->next->prev = hdr->prev;

	p = NULL;

	if (hdr->meta->head != NULL)
		p = (uint8_t*)hdr->meta->head + sizeof (struct list_node);

	if (hdr->prev == NULL && hdr->next == NULL)
		free(meta);

	free(hdr);

	return p;
}

inline static void
list_destroy(void *entry)
{
	uint8_t *p;
	struct list_node *hdr;
	struct list_meta *meta;

	if (entry == NULL)
		return;

	p = entry;
	hdr = (struct list_node*)(p - sizeof (struct list_node));
	meta = hdr->meta;

	for (hdr = meta->head; hdr != NULL;) {
		p = (uint8_t*)hdr + sizeof (struct list_node);
		hdr = hdr->next;
		list_free(p);
	}
}
// File: src/misc.c
inline static void*
xmalloc(size_t size)
{
	void *ret;

	errno = 0;
	ret = malloc(size);

	if (size != 0 && ret == NULL) {
		perror("malloc()");
		exit(EXIT_FAILURE);
	}

	return ret;
}

inline static void*
xrealloc(void *ptr, size_t size)
{
	void *ret;

	ret = realloc(ptr, size);

	if (size != 0 && ret == NULL) {
		perror("realloc()");
		exit(EXIT_FAILURE);
	}

	return ret;
}
// File: src/strings.c
inline static string
__str_get_start(string str)
{
	size_t i;

	for (i = 1; str[-i] != '\0'; ++i)
		;

	return str - i + 1;
}

inline static struct str_string*
__str_get_header(string str)
{
	str = __str_get_start(str);

	return (struct str_string*)(str - sizeof (struct str_string) - 1);
}

inline static string
str_new(size_t len)
{
	string str;
	struct str_string *head;
	size_t size;

	size = sizeof (struct str_string) + len + 2;
	head = xmalloc(size);
	str = (string)head + sizeof (struct str_string) + 1;

	str[-1] = '\0';
	str[ 0] = '\0';

	head->isdynamic = (len == 0) ? 1 : 0;
	head->len = 0;
	head->bufsize = len;

	return str;
}

inline static void
str_setopt(string str, enum str_opts opt, int val)
{
	struct str_string *head;

	assert(str);

	head = __str_get_header(str);

	switch (opt) {
	case STROPT_DYNAMIC:
		head->isdynamic = (val) ? 1 : 0;
		break;

	default:
		error(1, "Unknown option: 0x%x", opt);
	}
}

inline static string
str_set(string str, const char *s)
{
	struct str_string *head;
	size_t slen;
	size_t size;

	assert(s);

	if (str == NULL)
		str = str_new(0);

	head = __str_get_header(str);
	str = __str_get_start(str);
	slen = strlen(s);

	if (slen > head->bufsize && head->isdynamic) {
		size = sizeof (struct str_string) + slen + 2;
		head = xrealloc(head, size);
		str = (string)head + sizeof (struct str_string) + 1;
		head->bufsize = slen;
	}

	strncpy(str, s, head->bufsize);
	str[head->bufsize] = '\0';

	head->len = strlen(str);

	return str;
}

inline static string
str_cat(string str, const char *src)
{
	struct str_string *head;
	size_t slen;
	size_t size;
	char *s;

	assert(str);
	assert(src);

	head = __str_get_header(str);
	slen = strlen(src);

	s = alloca(slen + 1);
	strcpy(s, src);

	if (slen + head->len > head->bufsize && head->isdynamic) {
		size = sizeof (struct str_string) + slen + head->len + 2;
		head = xrealloc(head, size);
		str = (string)head + sizeof (struct str_string) + 1;
		head->bufsize = head->len + slen;
	}

	strncpy(str + head->len, s, head->bufsize - head->len);
	str[head->bufsize] = '\0';
	head->len = strlen(str);

	return str;
}

inline static string
str_clone(string str)
{
	struct str_string *head, *ret;
	size_t size;

	head = __str_get_header(str);
	size = sizeof (struct str_string) + head->bufsize + 2;
	ret = xmalloc(size);
	memcpy(ret, head, size);

	return (string)ret + sizeof (struct str_string) + 1;
}

inline static string
str_range(string str, size_t start, size_t end)
{
	struct str_string *head;
	char *s;
	size_t len;

	assert(str);

	head = __str_get_header(str);

	if (end > head->len)
		end = head->len;
	if (start > end)
		start = end;

	len = end - start;
	s = alloca(len + 1);

	memcpy(s, str + start, len);
	memcpy(str, s, len);

	str[len] = '\0';
	head->len = len;

	return str;
}

inline static size_t
str_len(string str)
{
	assert(str);

	return __str_get_header(__str_get_start(str))->len;
}

inline static string*
str_split(string str, const char *delim)
{
	size_t i, j;
	size_t count, start;
	string *strs;

	assert(str);
	assert(delim);

	strs = xmalloc(sizeof (string*));
	strs[0] = NULL;
	count = 1;

	for (i = start = 0; str[i] != '\0'; ++i) {
		for (j = 0; delim[j] != '\0'; ++j) {
			if (str[i] != delim[j])
				continue;

			strs = xrealloc(strs, ++count * sizeof (string*));
			strs[count - 1] = NULL;
			strs[count - 2] = str_clone(str);
			strs[count - 2] = str_range(strs[count - 2], start, i);

			start = i + 1;
		}
	}

	strs = xrealloc(strs, ++count * sizeof (string*));
	strs[count - 1] = NULL;
	strs[count - 2] = str_clone(str);
	strs[count - 2] = str_range(strs[count - 2], start, i);

	return strs;
}

inline static void
str_free(string str)
{
	if (str == NULL)
		return;

	free(__str_get_header(str));
}

inline static void
str_arr_free(string *strs)
{
	size_t i;

	if (strs == NULL)
		return;

	for (i = 0; strs[i] != NULL; ++i)
		str_free(strs[i]);

	free(strs);
}
