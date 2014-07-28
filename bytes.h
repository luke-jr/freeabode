#ifndef FABD_BYTES_H
#define FABD_BYTES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct bytes_t {
	uint8_t *buf;
	size_t sz;
	size_t allocsz;
} bytes_t;

#define BYTES_INIT {.buf=NULL,}

static inline
void bytes_init(bytes_t *b)
{
	*b = (bytes_t)BYTES_INIT;
}

// This can't be inline without ugly const/non-const issues
#define bytes_buf(b)  ((b)->buf)

static inline
size_t bytes_len(const bytes_t *b)
{
	return b->sz;
}

static inline
ssize_t bytes_find(const bytes_t * const b, const uint8_t needle)
{
	const size_t blen = bytes_len(b);
	const uint8_t * const buf = bytes_buf(b);
	for (int i = 0; i < blen; ++i)
		if (buf[i] == needle)
			return i;
	return -1;
}

static inline
bool bytes_eq(const bytes_t * const a, const bytes_t * const b)
{
	if (a->sz != b->sz)
		return false;
	return !memcmp(a->buf, b->buf, a->sz);
}

static inline
void bytes_extend_buf(bytes_t * const b, const size_t newsz)
{
	if (newsz <= b->allocsz)
		return;
	
	if (!b->allocsz)
		b->allocsz = 0x10;
	do {
		b->allocsz *= 2;
	} while (newsz > b->allocsz);
	b->buf = realloc(b->buf, b->allocsz);
	if (!b->buf)
		abort();
}

static inline
void bytes_resize(bytes_t * const b, const size_t newsz)
{
	bytes_extend_buf(b, newsz);
	b->sz = newsz;
}

static inline
void *bytes_preappend(bytes_t * const b, const size_t addsz)
{
	size_t origsz = bytes_len(b);
	bytes_extend_buf(b, origsz + addsz);
	return &bytes_buf(b)[origsz];
}

static inline
void bytes_postappend(bytes_t * const b, const size_t addsz)
{
	size_t origsz = bytes_len(b);
	bytes_resize(b, origsz + addsz);
}

static inline
void bytes_append(bytes_t * const b, const void * const add, const size_t addsz)
{
	void * const appendbuf = bytes_preappend(b, addsz);
	memcpy(appendbuf, add, addsz);
	bytes_postappend(b, addsz);
}

static inline
void bytes_cat(bytes_t *b, const bytes_t *cat)
{
	bytes_append(b, bytes_buf(cat), bytes_len(cat));
}

static inline
void bytes_cpy(bytes_t *dst, const bytes_t *src)
{
	dst->sz = src->sz;
	if (!dst->sz) {
		dst->allocsz = 0;
		dst->buf = NULL;
		return;
	}
	dst->allocsz = src->allocsz;
	size_t half;
	while (dst->sz <= (half = dst->allocsz / 2))
		dst->allocsz = half;
	dst->buf = malloc(dst->allocsz);
	memcpy(dst->buf, src->buf, dst->sz);
}

// Efficiently moves the data from src to dst, emptying src in the process
static inline
void bytes_assimilate(bytes_t * const dst, bytes_t * const src)
{
	void * const buf = dst->buf;
	const size_t allocsz = dst->allocsz;
	*dst = *src;
	*src = (bytes_t){
		.buf = buf,
		.allocsz = allocsz,
	};
}

static inline
void bytes_assimilate_raw(bytes_t * const b, void * const buf, const size_t bufsz, const size_t buflen)
{
	free(b->buf);
	b->buf = buf;
	b->allocsz = bufsz;
	b->sz = buflen;
}

static inline
void bytes_shift(bytes_t *b, size_t shift)
{
	if (shift >= b->sz)
	{
		b->sz = 0;
		return;
	}
	b->sz -= shift;
	memmove(bytes_buf(b), &bytes_buf(b)[shift], bytes_len(b));
}

static inline
void bytes_reset(bytes_t *b)
{
	b->sz = 0;
}

static inline
void bytes_nullterminate(bytes_t *b)
{
	bytes_append(b, "", 1);
	--b->sz;
}

static inline
void bytes_free(bytes_t *b)
{
	free(b->buf);
	b->sz = b->allocsz = 0;
}

#endif
