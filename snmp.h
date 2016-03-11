/*****************************************************************************
  Copyright (c) 2016 Westermo Teleindustri AB

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
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>

/* iso(1).org(3).dod(6).internet(1).mgmt(2).mib-2(1).dot1dBridge(17) */
#define oid_dot1dStp                        oid_dot1dBridge, 2
/* iso(1).org(3).dod(6).internet(1).mgmt(2).mib-2(1).dot1dBridge(17).dot1dBridge(2) */
#define oid_dot1dStpProtocolSpecification   oid_dot1dStp, 1
#define oid_dot1dStpPriority                oid_dot1dStp, 2
#define oid_dot1dStpTimeSinceTopologyChange oid_dot1dStp, 3
#define oid_dot1dStpTopChanges              oid_dot1dStp, 4
#define oid_dot1dStpDesignatedRoot          oid_dot1dStp, 5
#define oid_dot1dStpRootCost                oid_dot1dStp, 6
#define oid_dot1dStpRootPort                oid_dot1dStp, 7
#define oid_dot1dStpMaxAge                  oid_dot1dStp, 8
#define oid_dot1dStpHelloTime               oid_dot1dStp, 9
#define oid_dot1dStpHoldTime                oid_dot1dStp, 10
#define oid_dot1dStpForwardDelay            oid_dot1dStp, 11
#define oid_dot1dStpBridgeMaxAge            oid_dot1dStp, 12
#define oid_dot1dStpBridgeHelloTime         oid_dot1dStp, 13
#define oid_dot1dStpBridgeForwardDelay      oid_dot1dStp, 14
#define oid_dot1dStpPortTable               oid_dot1dStp, 15
#define oid_dot1dStpVersion                 oid_dot1dStp, 16
#define oid_dot1dStpTxHoldCount             oid_dot1dStp, 17

void snmp_init(void);
void snmp_fini(void);

int snmp_set_ro(long idx, void* value);
int snmp_commit(void);

void snmp_init_mib_dot1d_stp(void);
void snmp_init_mib_dot1d_stp_port_table(void);
