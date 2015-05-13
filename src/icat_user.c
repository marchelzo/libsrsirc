/* icat_user.c - User I/O (i.e. stdin/stdout) handling
 * icat - IRC netcat on top of libsrsirc - (C) 2012-14, Timo Buhrmester
 * See README for contact-, COPYING for license information. */

#define LOG_MODULE MOD_ICATUSER

#if HAVE_CONFIG_H
# include <config.h>
#endif


#include "icat_user.h"

#include <stdio.h>
#include <string.h>

#include <platform/base_io.h>

#include <logger/intlog.h>

#include "icat_common.h"


#define BUFSZ 4096


static const size_t s_bufsz = BUFSZ;
static char s_inbuf[BUFSZ+1];
static char *s_bufhead = s_inbuf;
static char *s_buftail = s_inbuf;
static bool s_eof;


static size_t buf_cnt(void);
static size_t buf_rem(void);


bool
user_canread(void)
{ T("trace");
	if (buf_cnt()) {
		*s_buftail = '\0';
		if (strchr(s_bufhead, '\n'))
			return true;
	}

	int r = b_stdin_canread();

	if (r < 0)
		EE("select");
	if (r <= 0)
		return false;

	if (!buf_rem()) {
		if (s_inbuf == s_bufhead) {
			WE("Line too long, truncating");
			s_buftail[0] = '\n';
			s_buftail[1] = '\0';
			return true;
		} else {
			size_t n = buf_cnt();
			D("Moving %zu bytes to the beginning (offset %zu)",
			    n, (size_t)(s_bufhead - s_inbuf));
			memmove(s_inbuf, s_bufhead, n);
			s_buftail -= s_bufhead - s_inbuf;
			s_bufhead = s_inbuf;
		}
	}

	if (s_eof)
		return false;

	long n = b_stdin_read(s_buftail, buf_rem());

	if (n < 0) {
		EE("read");
		return false;
	}

	if (n == 0) {
		I("EOF on stdin");
		s_eof = true;
		return false;
	}

	s_buftail += n;
	*s_buftail = '\0';

	return strchr(s_bufhead, '\n');
}

size_t
user_readline(char *dest, size_t destsz)
{ T("trace");
	if (!user_canread())
		return 0;

	*s_buftail = '\0';
	char *end = strchr(s_bufhead, '\n') + 1;

	size_t origlen = (size_t)(end - s_bufhead);
	size_t cplen = origlen;
	if (cplen >= destsz)
		cplen = destsz - 1;
	strncpy(dest, s_bufhead, cplen);
	dest[cplen] = '\0';

	s_bufhead = end;

	D("Read line from buffer: '%s'", dest);

	return origlen;
}

int
user_printf(const char *fmt, ...)
{ T("trace");
	char buf[1024];
	va_list l;
	va_start(l, fmt);
	int r = vsnprintf(buf, sizeof buf, fmt, l);
	va_end(l);

	I("To user: %s", buf);
	fputs(buf, stdout);

	return r;
}

bool
user_eof(void)
{ T("trace");
	return s_eof;
}


static size_t
buf_cnt(void)
{ T("trace");
	return (size_t)(s_buftail - s_bufhead);
}

static size_t
buf_rem(void)
{ T("trace");
	size_t unusable = (size_t)(s_bufhead - s_inbuf);
	return s_bufsz - buf_cnt() - unusable;
}