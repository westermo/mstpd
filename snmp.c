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

#include <asm/byteorder.h>

#include "mstp.h"
#include "config.h"
#include "snmp.h"

int snmp_commit(void)
{
   return 0;
}

int snmp_set_ro(long idx, void* value)
{
   return SNMP_ERR_READONLY;
}

static void snmp_init_mibs(void)
{
    snmp_init_mib_dot1d_stp();
    snmp_init_mib_dot1d_stp_port_table();
    snmp_init_mib_dot1d_stp_ext_port_table();
}

void snmp_init(void)
{
    netsnmp_enable_subagent();
    snmp_disable_log();
    snmp_enable_stderrlog();
    init_agent("mstpdAgent");
    snmp_init_mibs();
    init_snmp("mstpdAgent");
}

void snmp_fini(void)
{
    snmp_shutdown ("mstpdAgent");
}

#endif
