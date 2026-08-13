/* NTP server list
 *
 * Copyright (C) 2026  Joachim Wiberg <troglobit@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (Version 2,
 * June 1991) as published by the Free Software Foundation.  At the
 * time of writing, that license was published by the FSF with the URL
 * http://www.gnu.org/copyleft/gpl.html, and is incorporated herein by
 * reference.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef SNTPD_PEER_H_
#define SNTPD_PEER_H_

#include "sntpd.h"

#define MAX_SERVERS 8

struct ntp_server {
	char        *host;	/* hostname or literal address         */
	uint16_t     port;	/* NTP_PORT unless overridden          */
	unsigned int iburst:1;	/* burst when unreachable, default off */
	unsigned int prefer:1;	/* return here when reachable again    */

	int          dead;	/* KoD DENY/RSTR, never query again    */
	uint8_t      reach;	/* RFC 5905 section 9.2 shift register */
	int          lost;	/* consecutive unanswered probes       */
	int          burst;	/* packets left in the current burst   */
	int          addr_idx;	/* which resolved address we are on    */
	int          naddr;	/* addresses the last resolve returned */
};

extern int min_interval;	/* -m SEC, MIN_INTERVAL by default */

int  peer_add(const char *arg);
int  peer_count(void);
void peer_reset(void);			/* test suite only */
struct ntp_server *peer_get(int idx);
struct ntp_server *peer_active(void);
struct ntp_server *peer_activate(void);

void peer_rx(struct ntp_server *srv);
void peer_naddr(struct ntp_server *srv, int n);
void peer_timeout(struct ntp_server *srv);
void peer_probed(struct ntp_server *srv);
int  peer_unreachable(struct ntp_server *srv);
int  peer_retired(struct ntp_server *srv);
void peer_kod(struct ntp_server *srv, const char *code);
int  peer_spacing(struct ntp_server *srv, int cycle_time);
int  peer_rotate(void);

struct ntp_server *peer_candidate(void);	/* better server to retry, or NULL */
int  peer_switchback(void);			/* 1 when the candidate won        */
void peer_candidate_rx(void);
void peer_candidate_timeout(void);

#endif /* SNTPD_PEER_H_ */
