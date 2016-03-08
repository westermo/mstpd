/*****************************************************************************
  Copyright (c) 2006 EMC Corporation.
  Copyright (c) 2011 Factor-SPE

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the Free
  Software Foundation; either version 2 of the License, or (at your option)
  any later version.

  This program is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc., 59
  Temple Place - Suite 330, Boston, MA  02111-1307, USA.

  The full GNU General Public License is included in this distribution in the
  file called LICENSE.

  Authors: Srinivas Aji <Aji_Srinivas@emc.com>
  Authors: Vitalii Demianets <vitas@nppfactor.kiev.ua>

******************************************************************************/

#include <stdio.h>
#include <unistd.h>

#if defined HAVE_SNMP
#include <sys/queue.h>
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>
#include <net-snmp/agent/snmp_vars.h>
#endif

#include "log.h"
#include "epoll_loop.h"
#include "bridge_ctl.h"
#include "snmp.h"

/* globals */
static int epoll_fd = -1;
static struct timeval nexttimeout;

#if defined HAVE_SNMP
struct epoll_handler_entry {
	TAILQ_ENTRY(epoll_handler_entry) next;
	struct epoll_event_handler handler;
};
TAILQ_HEAD(, epoll_handler_entry) snmp_fds;

static void event_snmp_update(void);
#endif

int init_epoll(void)
{
    int r = epoll_create(128);
    if(r < 0)
    {
        ERROR("epoll_create failed: %m\n");
        return -1;
    }
    epoll_fd = r;
    return 0;
}

int add_epoll(struct epoll_event_handler *h)
{
    struct epoll_event ev =
    {
        .events = EPOLLIN,
        .data.ptr = h,
    };
    h->ref_ev = NULL;
    int r = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, h->fd, &ev);
    if(r < 0)
    {
        ERROR("epoll_ctl_add: %m\n");
        return -1;
    }
    return 0;
}

int remove_epoll(struct epoll_event_handler *h)
{
    int r = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, h->fd, NULL);
    if(r < 0)
    {
        ERROR("epoll_ctl_del: %m\n");
        return -1;
    }
    if(h->ref_ev && h->ref_ev->data.ptr == h)
    {
        h->ref_ev->data.ptr = NULL;
        h->ref_ev = NULL;
    }
    return 0;
}

void clear_epoll(void)
{
    if(epoll_fd >= 0)
        close(epoll_fd);
}

static inline int time_diff(struct timeval *second, struct timeval *first)
{
    return (second->tv_sec - first->tv_sec) * 1000
            + (second->tv_usec - first->tv_usec) / 1000;
}

static inline void run_timeouts(void)
{
    bridge_one_second();
    ++(nexttimeout.tv_sec);
#if defined HAVE_SNMP
    snmp_timeout();
    run_alarms();
    event_snmp_update();
#endif
}

#if defined HAVE_SNMP
static inline void event_snmp_read(uint32_t events, struct epoll_event_handler *h)
{
    fd_set fdset;

    FD_ZERO(&fdset);
    FD_SET(h->fd, &fdset);
    snmp_read(&fdset);
    event_snmp_update();
}

static void event_snmp_update(void)
{
    int maxfd = 0;
    fd_set fdset;
    struct timeval timeout;
    int block = 1;
    struct epoll_handler_entry *snmpfd, *snmpfd_next;
    int fd;

    FD_ZERO(&fdset);
    snmp_select_info(&maxfd, &fdset, &timeout, &block);

    /* We need to untrack any event whose FD is not in `fdset` anymore */
    for (snmpfd = TAILQ_FIRST(&snmp_fds); snmpfd; snmpfd = snmpfd_next)
    {
        snmpfd_next = TAILQ_NEXT(snmpfd, next);

        if (!FD_ISSET(snmpfd->handler.fd, &fdset))
        {
            remove_epoll(&snmpfd->handler);
            TAILQ_REMOVE(&snmp_fds, snmpfd, next);
            free(snmpfd);
        }
        else
        {
            FD_CLR(snmpfd->handler.fd, &fdset);
        }
    }

    /* Invariant: FD in `fdset` are not in list of FD */
    for (fd = 0; fd < maxfd; fd++)
    {
        if (FD_ISSET(fd, &fdset))
        {
            struct epoll_handler_entry *snmpfd = calloc(1, sizeof(struct epoll_handler_entry));

            snmpfd->handler.fd = fd;
	    snmpfd->handler.arg = NULL;
	    snmpfd->handler.handler = event_snmp_read;
	    add_epoll(&snmpfd->handler);
	    TAILQ_INSERT_TAIL(&snmp_fds, snmpfd, next);
	}
    }
}
#endif

int epoll_main_loop(void)
{
    gettimeofday(&nexttimeout, NULL);
    ++(nexttimeout.tv_sec);
#define EV_SIZE 8
    struct epoll_event ev[EV_SIZE];

#if defined HAVE_SNMP
    TAILQ_INIT(&snmp_fds);
#endif

    while(1)
    {
        int r, i;
        int timeout;

        struct timeval tv;
        gettimeofday(&tv, NULL);
        timeout = time_diff(&nexttimeout, &tv);
        if(timeout < 0 || timeout > 1000)
        {
            run_timeouts();
            /*
             * Check if system time has changed.
             * NOTE: we can not differentiate reliably if system
             * time has changed or we have spent too much time
             * inside event handlers and run_timeouts().
             * Fix: use clock_gettime(CLOCK_MONOTONIC, ) instead of
             * gettimeofday, if it is available.
             * If it is not available on given system -
             * the following is the best we can do.
             */
            if(timeout < -4000 || timeout > 1000)
            {
                /* Most probably, system time has changed */
                nexttimeout.tv_usec = tv.tv_usec;
                nexttimeout.tv_sec = tv.tv_sec + 1;
            }
            timeout = 0;
        }
#if defined HAVE_SNMP
        netsnmp_check_outstanding_agent_requests();
        event_snmp_update();
#endif
        r = epoll_wait(epoll_fd, ev, EV_SIZE, timeout);
        if(r < 0 && errno != EINTR)
        {
            ERROR("epoll_wait: %m\n");
            return -1;
        }
        for(i = 0; i < r; ++i)
        {
            struct epoll_event_handler *p = ev[i].data.ptr;
            if(p != NULL)
                p->ref_ev = &ev[i];
        }
        for (i = 0; i < r; ++i)
        {
            struct epoll_event_handler *p = ev[i].data.ptr;
            if(p && p->handler)
                p->handler(ev[i].events, p);
        }
        for (i = 0; i < r; ++i)
        {
            struct epoll_event_handler *p = ev[i].data.ptr;
            if(p != NULL)
                p->ref_ev = NULL;
        }
    }

    return 0;
}
