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

#if defined HAVE_SNMP

#include <asm/byteorder.h>

#include "mstp.h"
#include "config.h"
#include "snmp.h"

#include "libnsh/scalar.h"

#define SNMP_STP_PRIORITY              2
#define SNMP_STP_TIME_SINCE_TOP_CHANGE 3
#define SNMP_STP_TOP_CHANGES           4
#define SNMP_STP_DESIGNATED_ROOT       5
#define SNMP_STP_ROOT_COST             6
#define SNMP_STP_ROOT_PORT             7
#define SNMP_STP_MAX_AGE               8
#define SNMP_STP_HELLO_TIME            9
#define SNMP_STP_HOLD_TIME             10
#define SNMP_STP_FORWARD_DELAY         11
#define SNMP_STP_BRIDGE_MAX_AGE        12
#define SNMP_STP_BRIDGE_HELLO_TIME     13
#define SNMP_STP_BRIDGE_FORWARD_DELAY  14
#define SNMP_STP_TX_HOLD_COUNT         17

static int snmp_get_dot1d_stp(void *value, int len, int id)
{
    CIST_BridgeStatus s;
    char *br_name = INTERFACE_BRIDGE;
    char root_port_name[IFNAMSIZ];
    int br_index = get_index(br_name, "bridge");

    if (br_index < 0)
        return SNMP_ERR_GENERR;

    if (CTL_get_cist_bridge_status(br_index, &s, root_port_name))
        return SNMP_ERR_GENERR;

    switch (id) {
        case SNMP_STP_PRIORITY:
            *(int*)value = s.bridge_id.s.priority;
            break;
        case SNMP_STP_TIME_SINCE_TOP_CHANGE:
            *(int*)value = s.time_since_topology_change;
            break;
        case SNMP_STP_TOP_CHANGES:
            *(int*)value = s.topology_change_count;
            break;
        case SNMP_STP_DESIGNATED_ROOT:
	    snprintf((char*)value, len, "%c%c",
                     s.designated_root.s.priority / 256,
                     s.designated_root.s.priority % 256);
            memcpy(value + 2, s.designated_root.s.mac_address, 6);
            break;
        case SNMP_STP_ROOT_COST:
            *(int*)value = s.root_path_cost;
            break;
        case SNMP_STP_ROOT_PORT:
            *(int*)value = s.root_port_id;
            break;
        case SNMP_STP_MAX_AGE:
            *(int*)value = s.bridge_max_age * 100;
            break;
        case SNMP_STP_HELLO_TIME:
            *(int*)value = s.bridge_hello_time * 100;
            break;
        case SNMP_STP_HOLD_TIME:
            *(int*)value = s.tx_hold_count * 100;
            break;
        case SNMP_STP_FORWARD_DELAY:
           *(int*)value = s.bridge_forward_delay * 100;
            break;
        case SNMP_STP_BRIDGE_MAX_AGE:
            *(int*)value = s.bridge_max_age * 100;
            break;
        case SNMP_STP_BRIDGE_HELLO_TIME:
            *(int*)value = s.bridge_hello_time * 100;
            break;
        case SNMP_STP_BRIDGE_FORWARD_DELAY:
            *(int*)value = s.bridge_forward_delay * 100;
            break;
        case SNMP_STP_TX_HOLD_COUNT:
            *(int*)value = s.tx_hold_count;
            break;
        default:
            return SNMP_ERR_GENERR;
    }

    return SNMP_ERR_NOERROR;
}

nsh_scalar_handler_const    (dot1dStpProtocolSpecification,   ASN_INTEGER,   3);   /* XXX: FIXME! Protocol specification is always 802.1d */
nsh_scalar_group_handler_ro (dot1dStpPriority,                ASN_INTEGER,   SNMP_STP_PRIORITY,              snmp_get_dot1d_stp, sizeof(long), 0);   /* XXX: FIXME! RW support, see snmp_set_stp_prio */
nsh_scalar_group_handler_ro (dot1dStpTimeSinceTopologyChange, ASN_TIMETICKS, SNMP_STP_TIME_SINCE_TOP_CHANGE, snmp_get_dot1d_stp, sizeof(long), 0);
nsh_scalar_group_handler_ro (dot1dStpTopChanges,              ASN_COUNTER,   SNMP_STP_TOP_CHANGES,           snmp_get_dot1d_stp, sizeof(long), 0);
nsh_scalar_group_handler_ro (dot1dStpDesignatedRoot,          ASN_OCTET_STR, SNMP_STP_DESIGNATED_ROOT,       snmp_get_dot1d_stp, 8,            0);
nsh_scalar_group_handler_ro (dot1dStpRootCost,                ASN_INTEGER,   SNMP_STP_ROOT_COST,             snmp_get_dot1d_stp, sizeof(long), 0);
nsh_scalar_group_handler_ro (dot1dStpRootPort,                ASN_INTEGER,   SNMP_STP_ROOT_PORT,             snmp_get_dot1d_stp, sizeof(long), 0);
nsh_scalar_group_handler_ro (dot1dStpMaxAge,                  ASN_INTEGER,   SNMP_STP_MAX_AGE,               snmp_get_dot1d_stp, sizeof(long), 0);
nsh_scalar_group_handler_ro (dot1dStpHelloTime,               ASN_INTEGER,   SNMP_STP_HELLO_TIME,            snmp_get_dot1d_stp, sizeof(long), 0);
nsh_scalar_group_handler_ro (dot1dStpHoldTime,                ASN_INTEGER,   SNMP_STP_HOLD_TIME,             snmp_get_dot1d_stp, sizeof(long), 0);
nsh_scalar_group_handler_ro (dot1dStpForwardDelay,            ASN_INTEGER,   SNMP_STP_FORWARD_DELAY,         snmp_get_dot1d_stp, sizeof(long), 0);
nsh_scalar_group_handler_ro (dot1dStpBridgeMaxAge,            ASN_INTEGER,   SNMP_STP_BRIDGE_MAX_AGE,        snmp_get_dot1d_stp, sizeof(long), 0);   /* XXX: FIXME! RW support, see snmp_set_max_age */
nsh_scalar_group_handler_ro (dot1dStpBridgeHelloTime,         ASN_INTEGER,   SNMP_STP_BRIDGE_HELLO_TIME,     snmp_get_dot1d_stp, sizeof(long), 0);   /* XXX: FIXME! RW support, see snmp_set_hello_time */
nsh_scalar_group_handler_ro (dot1dStpBridgeForwardDelay,      ASN_INTEGER,   SNMP_STP_BRIDGE_FORWARD_DELAY,  snmp_get_dot1d_stp, sizeof(long), 0);   /* XXX: FIXME! RW support, see snmp_set_forward_delay */
nsh_scalar_handler_const    (dot1dStpVersion,                 ASN_INTEGER,   2);   /* XXX: FIXME! Forced version to RSTP */
nsh_scalar_group_handler_ro (dot1dStpTxHoldCount,             ASN_INTEGER,   SNMP_STP_TX_HOLD_COUNT,         snmp_get_dot1d_stp, sizeof(long), 0);

void snmp_init_mib_dot1d_stp(void)
{
    nsh_register_scalar_ro(dot1dStpProtocolSpecification);
    nsh_register_scalar_ro(dot1dStpPriority);
    nsh_register_scalar_ro(dot1dStpTimeSinceTopologyChange);
    nsh_register_scalar_ro(dot1dStpTopChanges);
    nsh_register_scalar_ro(dot1dStpDesignatedRoot);
    nsh_register_scalar_ro(dot1dStpRootCost);
    nsh_register_scalar_ro(dot1dStpRootPort);
    nsh_register_scalar_ro(dot1dStpMaxAge);
    nsh_register_scalar_ro(dot1dStpHelloTime);
    nsh_register_scalar_ro(dot1dStpHoldTime);
    nsh_register_scalar_ro(dot1dStpForwardDelay);
    nsh_register_scalar_ro(dot1dStpBridgeMaxAge);
    nsh_register_scalar_ro(dot1dStpBridgeHelloTime);
    nsh_register_scalar_ro(dot1dStpBridgeForwardDelay);
    nsh_register_scalar_ro(dot1dStpVersion);
    nsh_register_scalar_ro(dot1dStpTxHoldCount);
}

#endif
