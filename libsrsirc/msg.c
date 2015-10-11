/* msg.c - protocol message handlers
 * libsrsirc - a lightweight serious IRC lib - (C) 2012-15, Timo Buhrmester
 * See README for contact-, COPYING for license information. */

#define LOG_MODULE MOD_IMSG

#if HAVE_CONFIG_H
# include <config.h>
#endif


#include "msg.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <logger/intlog.h>

#include "common.h"
#include "conn.h"

#include <libsrsirc/defs.h>
#include <libsrsirc/util.h>


bool
lsi_msg_reghnd(irc hnd, const char *cmd, hnd_fn hndfn, const char *module)
{
	size_t i = 0;
	D("'%s' registering '%s'-handler", module, cmd);
	for (;i < hnd->msghnds_cnt; i++)
		if (!hnd->msghnds[i].cmd[0])
			break;

	if (i == hnd->msghnds_cnt) {
		size_t ncnt = hnd->msghnds_cnt * 2;
		struct msghnd *narr;
		if (!(narr = malloc(ncnt * sizeof *narr)))
			return false;

		memcpy(narr, hnd->msghnds, hnd->msghnds_cnt * sizeof *narr);
		for (size_t j = hnd->msghnds_cnt; j < ncnt; j++)
			narr[j].cmd[0] = '\0';

		free(hnd->msghnds);

		hnd->msghnds = narr;
		hnd->msghnds_cnt = ncnt;
	}

	hnd->msghnds[i].module = module;
	hnd->msghnds[i].hndfn = hndfn;
	lsi_com_strNcpy(hnd->msghnds[i].cmd, cmd, sizeof hnd->msghnds[i].cmd);
	return true;
}

bool
lsi_msg_reguhnd(irc hnd, const char *cmd, uhnd_fn hndfn, bool pre)
{
	size_t hcnt = pre ? hnd->uprehnds_cnt : hnd->uposthnds_cnt;
	struct umsghnd *harr = pre ? hnd->uprehnds : hnd->uposthnds;
	size_t i = 0;
	D("user registering %s-'%s'-handler", pre?"pre":"post", cmd);
	for (;i < hcnt; i++)
		if (!harr[i].cmd[0])
			break;

	if (i == hcnt) {
		size_t ncnt = hcnt * 2;
		struct umsghnd *narr;
		if (!(narr = malloc(ncnt * sizeof *narr)))
			return false;

		memcpy(narr, harr, hcnt * sizeof *narr);
		for (size_t j = hcnt; j < ncnt; j++)
			narr[j].cmd[0] = '\0';

		free(harr);

		harr = narr;
		hcnt = ncnt;
		if (pre) {
			hnd->uprehnds = harr;
			hnd->uprehnds_cnt = hcnt;
		} else {
			hnd->uposthnds = harr;
			hnd->uposthnds_cnt = hcnt;
		}
	}

	harr[i].hndfn = hndfn;
	lsi_com_strNcpy(harr[i].cmd, cmd, sizeof harr[i].cmd);
	return true;
}

void
lsi_msg_unregall(irc hnd, const char *module)
{
	size_t i = 0;
	for (;i < hnd->msghnds_cnt; i++)
		if (hnd->msghnds[i].cmd[0]
		    && strcmp(hnd->msghnds[i].module, module) == 0)
			hnd->msghnds[i].cmd[0] = '\0';
}

static bool
dispatch_uhnd(irc hnd, tokarr *msg, size_t ac, bool pre)
{
	size_t hcnt = pre ? hnd->uprehnds_cnt : hnd->uposthnds_cnt;
	struct umsghnd *harr = pre ? hnd->uprehnds : hnd->uposthnds;

	for (size_t i = 0; i < hcnt; i++) {
		if (!harr[i].cmd[0])
			continue;

		if (strcmp((*msg)[1], harr[i].cmd) != 0)
			continue;

		D("dispatch a %s-'%s'", pre?"pre":"post", (*msg)[1]);
		if (!harr[i].hndfn(hnd, msg, ac, pre))
			return false;
	}

	return true;
}

uint8_t
lsi_msg_handle(irc hnd, tokarr *msg, bool logon)
{
	uint8_t res = 0;
	size_t i = 0;
	size_t ac = 2;
	while (ac < COUNTOF(*msg) && (*msg)[ac])
		ac++;

	if (!logon && !dispatch_uhnd(hnd, msg, ac, true)) {
		res |= USER_ERR;
		goto msg_handle_fail;
	}

	for (;i < hnd->msghnds_cnt; i++) {
		if (!hnd->msghnds[i].cmd[0])
			continue;

		if (strcmp((*msg)[1], hnd->msghnds[i].cmd) != 0)
			continue;

		D("dispatch a '%s' to '%s'", (*msg)[1], hnd->msghnds[i].module);
		res |= hnd->msghnds[i].hndfn(hnd, msg, ac, logon);
		if (res & CANT_PROCEED)
			goto msg_handle_fail;
	}

	if (!logon && !dispatch_uhnd(hnd, msg, ac, false)) {
		res |= USER_ERR;
		goto msg_handle_fail;
	}

	return res;

msg_handle_fail:
	res &= ~CANT_PROCEED;

	if (res & USER_ERR) {
		E("user-registered message handler denied proceeding");
		res |= CANT_PROCEED;
	} else if (res & AUTH_ERR) {
		E("failed to authenticate");
		res |= CANT_PROCEED;
	} else if (res & IO_ERR) {
		E("i/o error");
		res |= CANT_PROCEED;
	} else if (res & ALLOC_ERR) {
		E("memory allocation failed");
		/* we do proceed for now */
	} else if (res & OUT_OF_NICKS) {
		E("out of nicks");
		res |= CANT_PROCEED;
	} else if (res & PROTO_ERR) {
		char line[1024];
		E("proto error on '%s' (ct:%d)",
		    lsi_ut_sndumpmsg(line, sizeof line, NULL, msg),
		    lsi_conn_colon_trail(hnd->con));
		/* we do proceed for now */
	} else {
		E("can't proceed for unknown reasons");
		/* we do proceed for now */
	}

	return res;

}
