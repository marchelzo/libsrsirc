/* irc_ext.c - some more irc functionality (see also irc.c)
 * libsrsirc - a lightweight serious IRC lib - (C) 2014, Timo Buhrmester
 * See README for contact-, COPYING for license information. */

#if HAVE_CONFIG_H
# include <config.h>
#endif

#define LOG_MODULE MOD_IRC

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include <libsrsirc/defs.h>
#include "conn.h"
#include <libsrsirc/util.h>
#include <intlog.h>
#include "msg.h"

#include "irc_msghnd.h"

static uint8_t
handle_001(irc hnd, tokarr *msg, size_t nargs, bool logon, void *tag)
{
	if (nargs < 3)
		return CANT_PROCEED|PROTO_ERR;

	ut_freearr(hnd->logonconv[0]);
	hnd->logonconv[0] = ut_clonearr(msg);
	com_strNcpy(hnd->mynick, (*msg)[2],sizeof hnd->mynick);
	char *tmp;
	if ((tmp = strchr(hnd->mynick, '@')))
		*tmp = '\0';
	if ((tmp = strchr(hnd->mynick, '!')))
		*tmp = '\0';

	com_strNcpy(hnd->umodes, DEF_UMODES, sizeof hnd->umodes);
	com_strNcpy(hnd->cmodes, DEF_CMODES, sizeof hnd->cmodes);
	hnd->ver[0] = '\0';
	hnd->service = false;

	return 0;
}

static uint8_t
handle_002(irc hnd, tokarr *msg, size_t nargs, bool logon, void *tag)
{
	ut_freearr(hnd->logonconv[1]);
	hnd->logonconv[1] = ut_clonearr(msg);

	return 0;
}

static uint8_t
handle_003(irc hnd, tokarr *msg, size_t nargs, bool logon, void *tag)
{
	ut_freearr(hnd->logonconv[2]);
	hnd->logonconv[2] = ut_clonearr(msg);

	return 0;
}

static uint8_t
handle_004(irc hnd, tokarr *msg, size_t nargs, bool logon, void *tag)
{
	if (nargs < 7)
		return CANT_PROCEED|PROTO_ERR;

	ut_freearr(hnd->logonconv[3]);
	hnd->logonconv[3] = ut_clonearr(msg);
	com_strNcpy(hnd->myhost, (*msg)[3],sizeof hnd->myhost);
	com_strNcpy(hnd->umodes, (*msg)[5], sizeof hnd->umodes);
	com_strNcpy(hnd->cmodes, (*msg)[6], sizeof hnd->cmodes);
	com_strNcpy(hnd->ver, (*msg)[4], sizeof hnd->ver);
	D("(%p) got beloved 004", hnd);

	return LOGON_COMPLETE;
}

static uint8_t
handle_PING(irc hnd, tokarr *msg, size_t nargs, bool logon, void *tag)
{
	if (nargs < 3)
		return CANT_PROCEED|PROTO_ERR;

	char buf[64];
	snprintf(buf, sizeof buf, "PONG :%s\r\n", (*msg)[2]);
	return conn_write(hnd->con, buf) ? 0 : IO_ERR;
}

static uint8_t
handle_XXX(irc hnd, tokarr *msg, size_t nargs, bool logon, void *tag)
{
	if (!hnd->cb_mut_nick) {
		W("(%p) got no mutnick.. (failing)", hnd);
		return CANT_PROCEED|OUT_OF_NICKS;
	}

	hnd->cb_mut_nick(hnd->mynick, 10); //XXX hc

	if (!hnd->mynick[0]) {
		W("out of nicks");
		return CANT_PROCEED|OUT_OF_NICKS;
	}

	char buf[MAX_NICK_LEN];
	snprintf(buf, sizeof buf, "NICK %s\r\n", hnd->mynick);
	return conn_write(hnd->con, buf) ? 0 : IO_ERR;
}

static uint8_t
handle_464(irc hnd, tokarr *msg, size_t nargs, bool logon, void *tag)
{
	W("(%p) wrong server password", hnd);
	return CANT_PROCEED|AUTH_ERR;
}

static uint8_t
handle_383(irc hnd, tokarr *msg, size_t nargs, bool logon, void *tag)
{
	if (nargs < 3)
		return CANT_PROCEED|PROTO_ERR;

	com_strNcpy(hnd->mynick, (*msg)[2],sizeof hnd->mynick);
	char *tmp;
	if ((tmp = strchr(hnd->mynick, '@')))
		*tmp = '\0';
	if ((tmp = strchr(hnd->mynick, '!')))
		*tmp = '\0';

	com_strNcpy(hnd->myhost, (*msg)[0] ? (*msg)[0] : hnd->con->host,
	    sizeof hnd->myhost);

	com_strNcpy(hnd->umodes, DEF_UMODES, sizeof hnd->umodes);
	com_strNcpy(hnd->cmodes, DEF_CMODES, sizeof hnd->cmodes);
	hnd->ver[0] = '\0';
	hnd->service = true;
	D("(%p) got beloved 383", hnd);

	return LOGON_COMPLETE;
}

static uint8_t
handle_484(irc hnd, tokarr *msg, size_t nargs, bool logon, void *tag)
{
	hnd->restricted = true;
	I("(%p) we're 'restricted'", hnd);
	return 0;
}

static uint8_t
handle_465(irc hnd, tokarr *msg, size_t nargs, bool logon, void *tag)
{
	W("(%p) we're banned", hnd);
	hnd->banned = true;
	free(hnd->banmsg);
	hnd->banmsg = strdup((*msg)[3] ? (*msg)[3] : "");
	if (!hnd->banned)
		EE("strdup");

	return 0; /* well if we are, the server will sure disconnect ua s*/
}

static uint8_t
handle_466(irc hnd, tokarr *msg, size_t nargs, bool logon, void *tag)
{
	W("(%p) we will be banned", hnd);

	return 0; /* so what, bitch? */
}

static uint8_t
handle_ERROR(irc hnd, tokarr *msg, size_t nargs, bool logon, void *tag)
{
	free(hnd->lasterr);
	hnd->lasterr = strdup((*msg)[2] ? (*msg)[2] : "");
	if (!hnd->lasterr)
		EE("strdup");
	else
		W("sever said ERROR: '%s'", hnd->lasterr);
	return 0; /* not strictly a case for CANT_PROCEED */
}

static uint8_t
handle_NICK(irc hnd, tokarr *msg, size_t nargs, bool logon, void *tag)
{
	if (nargs < 3)
		return CANT_PROCEED|PROTO_ERR;

	char nick[128];
	if (!(*msg)[0])
		return CANT_PROCEED|PROTO_ERR;

	ut_pfx2nick(nick, sizeof nick, (*msg)[0]);

	if (!ut_istrcmp(nick, hnd->mynick, hnd->casemapping)) {
		com_strNcpy(hnd->mynick, (*msg)[2], sizeof hnd->mynick);
		I("my nick is now '%s'", hnd->mynick);
	}

	return 0;
}

static uint8_t
handle_005_CASEMAPPING(irc hnd, const char *val)
{
	if (strcasecmp(val, "ascii") == 0)
		hnd->casemapping = CMAP_ASCII;
	else if (strcasecmp(val, "strict-rfc1459") == 0)
		hnd->casemapping = CMAP_STRICT_RFC1459;
	else {
		if (strcasecmp(val, "rfc1459") != 0)
			W("unknown 005 casemapping: '%s'", val);
		hnd->casemapping = CMAP_RFC1459;
	}

	return 0;
}

/* XXX not robust enough */
static uint8_t
handle_005_PREFIX(irc hnd, const char *val)
{
	char str[32];
	if (!val[0])
		return CANT_PROCEED|PROTO_ERR;

	com_strNcpy(str, val + 1, sizeof str);
	char *p = strchr(str, ')');
	if (!p)
		return CANT_PROCEED|PROTO_ERR;

	*p++ = '\0';

	size_t slen = strlen(str);
	if (slen == 0 || slen != strlen(p))
		return CANT_PROCEED|PROTO_ERR;

	com_strNcpy(hnd->m005modepfx[0], str, sizeof hnd->m005modepfx[0]);
	com_strNcpy(hnd->m005modepfx[1], p, sizeof hnd->m005modepfx[1]);

	return 0;
}

static uint8_t
handle_005_CHANMODES(irc hnd, const char *val)
{
	for (int z = 0; z < 4; ++z)
		hnd->m005chanmodes[z][0] = '\0';

	int c = 0;
	char argbuf[64];
	com_strNcpy(argbuf, val, sizeof argbuf);
	char *ptr = strtok(argbuf, ",");

	while (ptr) {
		if (c < 4)
			com_strNcpy(hnd->m005chanmodes[c++], ptr,
			    sizeof hnd->m005chanmodes[0]);
		ptr = strtok(NULL, ",");
	}

	if (c != 4)
		W("005 chanmodes: expected 4 params, got %i. arg: \"%s\"",
		    c, val);

	return 0;
}

static uint8_t
handle_005(irc hnd, tokarr *msg, size_t nargs, bool logon, void *tag)
{
	uint8_t ret = 0;

	for (size_t z = 3; z < nargs; ++z) {
		if (strncasecmp((*msg)[z], "CASEMAPPING=", 12) == 0)
			ret |= handle_005_CASEMAPPING(hnd, (*msg)[z] + 12);
		else if (strncasecmp((*msg)[z], "PREFIX=", 7) == 0)
			ret |= handle_005_PREFIX(hnd, (*msg)[z] + 7);
		else if (strncasecmp((*msg)[z], "CHANMODES=", 10) == 0)
			ret |= handle_005_CHANMODES(hnd, (*msg)[z] + 10);

		if (ret & CANT_PROCEED)
			return ret;
	}

	return ret;
}


bool
imh_regall(void)
{
	bool fail = false;
	fail = fail || !msg_reghnd("001", handle_001, "irc", NULL);
	fail = fail || !msg_reghnd("002", handle_002, "irc", NULL);
	fail = fail || !msg_reghnd("003", handle_003, "irc", NULL);
	fail = fail || !msg_reghnd("004", handle_004, "irc", NULL);
	fail = fail || !msg_reghnd("PING", handle_PING, "irc", NULL);
	fail = fail || !msg_reghnd("432", handle_XXX, "irc", NULL);
	fail = fail || !msg_reghnd("433", handle_XXX, "irc", NULL);
	fail = fail || !msg_reghnd("436", handle_XXX, "irc", NULL);
	fail = fail || !msg_reghnd("437", handle_XXX, "irc", NULL);
	fail = fail || !msg_reghnd("464", handle_464, "irc", NULL);
	fail = fail || !msg_reghnd("383", handle_383, "irc", NULL);
	fail = fail || !msg_reghnd("484", handle_484, "irc", NULL);
	fail = fail || !msg_reghnd("465", handle_465, "irc", NULL);
	fail = fail || !msg_reghnd("466", handle_466, "irc", NULL);
	fail = fail || !msg_reghnd("ERROR", handle_ERROR, "irc", NULL);
	fail = fail || !msg_reghnd("NICK", handle_NICK, "irc", NULL);
	fail = fail || !msg_reghnd("005", handle_005, "irc", NULL);

	return !fail;
}
