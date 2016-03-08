/*****************************************************************************
  Copyright (c) 2014 Westermo Teleindustri AB

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

  Authors: Jonas Johansson <jonasj76@gmail.com>

  This code will provide SNMP support for MSTPD daemon.

******************************************************************************/

#if defined HAVE_SNMP

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>
#include <net-snmp/agent/snmp_vars.h>

#if HAVE_NET_SNMP_AGENT_UTIL_FUNCS_H
#include <net-snmp/agent/util_funcs.h>
#else
/* The above header may be buggy. We just need this function. */
int header_generic(struct variable *, oid *, size_t *, int, size_t *, WriteMethod **);
#endif

#define DOT1D_STP_VERSION 16

static u_char* dot1dStp_stat(struct variable *vp, oid *name, size_t *length,
			     int exact, size_t *var_len, WriteMethod **write_method)
{
    static unsigned long long_ret;

    if (header_generic(vp, name, length, exact, var_len, write_method))
        return NULL;

    switch (vp->magic)
    {
        case DOT1D_STP_VERSION:
            long_ret = 2; /* RSTP */
            return (u_char *)&long_ret;
       default:
            break;
    }
    return NULL;
}

static oid dot1dStp_oid[] = {1, 3, 6, 1, 2, 1, 17, 2};
static struct variable1 dot1dStp_vars[] = {
    {DOT1D_STP_VERSION, ASN_INTEGER, RONLY, dot1dStp_stat, 1, {16}}
};

void snmp_init(void)
{
    netsnmp_enable_subagent();
    snmp_disable_log();
    snmp_enable_stderrlog();
    init_agent("mstpdAgent");
    REGISTER_MIB("dot1dStp", dot1dStp_vars, variable1, dot1dStp_oid);
    init_snmp("mstpdAgent");
}

#endif
