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

#include <confuse.h>

#include "mstp.h"
#include "config.h"
#include "snmp.h"

#include "snmp/weos.h"

#define MIN_COLUMN 1
#define MAX_COLUMN 11

typedef struct table_data_t table_data_t;
struct table_data_t {
    long           port;
    long           priority;
    long           state;
    long           enable;
    long           path_cost;
    unsigned char  designated_root[8];
    long           designated_cost;
    unsigned char  designated_bridge[8];
    unsigned char  designated_port[2];
    u_long         forward_transitions;
    long           path_cost32;

    table_data_t   *next;
};

static struct table_data_t *table_head = NULL;

static NetsnmpCacheLoad table_load;
static NetsnmpCacheFree table_free;
static Netsnmp_First_Data_Point table_get_first;
static Netsnmp_Next_Data_Point  table_get_next;
static Netsnmp_Node_Handler table_handler;

static table_index_t idx[] = {
    INDEX (ASN_INTEGER, table_data_t, port, sizeof(long)),
};

TABLE_INDEX (table_idx, idx)
TABLE_FREE (table_free, table_data_t, table_head)
TABLE_GET_FIRST (table_get_first, table_get_next)
TABLE_GET_NEXT (table_get_next, table_data_t, table_head, table_idx, 1)

static void table_create_entry(long port,
			       long priority,
			       long state,
			       long enable,
			       long path_cost,
			       unsigned char *designated_root,
			       long designated_cost,
			       unsigned char *designated_bridge,
			       unsigned char *designated_port,
			       u_long forward_transitions,
			       long path_cost32)
{
    table_data_t *entry;

    entry = SNMP_MALLOC_TYPEDEF (table_data_t);
    if (!entry)
       return;

    TABLE_ADD(entry, port);
    TABLE_ADD(entry, priority);
    TABLE_ADD(entry, state);
    TABLE_ADD(entry, enable);
    TABLE_ADD(entry, path_cost);
    TABLE_ADD_ARRAY(entry, designated_root,   table_data_t);
    TABLE_ADD(entry, designated_cost);
    TABLE_ADD_ARRAY(entry, designated_bridge, table_data_t);
    TABLE_ADD_ARRAY(entry, designated_port,   table_data_t);
    TABLE_ADD(entry,forward_transitions);
    TABLE_ADD(entry, path_cost32);

    entry->next = table_head;
    table_head  = entry;
}

static int snmp_map_port_state(int state)
{
   /* Map to SNMP port state value */
    switch (state)
    {
        case 0:  return 1; /* disabled(1)   */
        case 1:  return 3; /* listening(3)  */
        case 2:  return 4; /* learning(4)   */
        case 3:  return 5; /* forwarding(5) */
        case 4:  return 2; /* blocking(2)   */
        default: return 5; /* forwarding(5) */
    }
}

static int table_load (netsnmp_cache *cache, void* vmagic)
{
    CIST_BridgeStatus s;
    char *br_name = INTERFACE_BRIDGE;
    char root_port_name[IFNAMSIZ];
    int br_index = get_index(br_name, "bridge");
    int i;
    cfg_t *parse_cfg;
    unsigned char designated_root[8], designated_bridge[8], designated_port[2];

    if (br_index < 0)
        return 0;

    if (CTL_get_cist_bridge_status(br_index, &s, root_port_name))
        return 0;

    parse_cfg = parse_conf(MSTPD_CONFIG_FILE);
    if(!parse_cfg)
	return 0;

    /* designated root */
    snprintf((char*)designated_root, sizeof(designated_root), "%c%c",
        s.designated_root.s.priority / 256, s.designated_root.s.priority % 256);
    memcpy(designated_root + 2, s.designated_root.s.mac_address, 6);

    for (i = 0; i < cfg_size(parse_cfg, "ports"); i++)
    {
        CIST_PortStatus ps;
        char *port_p;
        int port_index = 0;
        cfg_t *cfg_port = cfg_getnsec(parse_cfg, "ports", i);

        port_p = cfg_getstr(cfg_port, "ifname");
        port_index = if_nametoindex(port_p);

        if (CTL_get_cist_port_status(br_index, port_index, &ps))
            continue; /* failed to get port state */

        /* designated bridge */
        snprintf((char*)designated_bridge, sizeof(designated_bridge), "%c%c",
            ps.designated_bridge.s.priority / 256, ps.designated_bridge.s.priority % 256);
        memcpy(designated_bridge + 2, ps.designated_bridge.s.mac_address, 6);

        /* designated port */
        designated_port[0] = (ps.designated_port >> 8) & 0xff;
        designated_port[1] = ps.designated_port & 0xff;

        table_create_entry(port_index,
                           ps.port_id & 0xff,
                           snmp_map_port_state(ps.state),
                           port_is_enabled(port_p) ? 1 : 2,
                           (ps.external_port_path_cost < 0xffff) ? ps.external_port_path_cost : 0xffff,
                           designated_root,
                           ps.designated_external_cost,
                           designated_bridge,
                           designated_port,
                           ps.num_trans_fwd,
                           ps.external_port_path_cost);
    }

    return 0;
}

static int table_handler(netsnmp_mib_handler *handler,
			 netsnmp_handler_registration *reginfo,
			 netsnmp_agent_request_info *reqinfo,
			 netsnmp_request_info *requests)
{
    table_entry_t table[] = {
        TABLE_ENTRY_RO (ASN_INTEGER,   table_data_t, port,                sizeof(long)),
        TABLE_ENTRY_RO (ASN_INTEGER,   table_data_t, priority,            sizeof(long)),
        TABLE_ENTRY_RO (ASN_INTEGER,   table_data_t, state,               sizeof(long)),
        TABLE_ENTRY_RW (ASN_INTEGER,   table_data_t, enable,              sizeof(long),                                   snmp_set_ro),
        TABLE_ENTRY_RW (ASN_INTEGER,   table_data_t, path_cost,           sizeof(long),                                   snmp_set_ro),
        TABLE_ENTRY_RO (ASN_OCTET_STR, table_data_t, designated_root,     ELEMENT_SIZE(table_data_t, designated_root)),
        TABLE_ENTRY_RO (ASN_INTEGER,   table_data_t, designated_cost,     sizeof(long)),
        TABLE_ENTRY_RO (ASN_OCTET_STR, table_data_t, designated_bridge,   ELEMENT_SIZE(table_data_t, designated_bridge)),
        TABLE_ENTRY_RO (ASN_OCTET_STR, table_data_t, designated_port,     ELEMENT_SIZE(table_data_t, designated_port)),
        TABLE_ENTRY_RO (ASN_COUNTER,   table_data_t, forward_transitions, sizeof(long)),
        TABLE_ENTRY_RW (ASN_INTEGER,   table_data_t, path_cost32,         sizeof(long),                                   snmp_set_ro),
    };

    return handle_table(reqinfo, requests, table, ARRAY_ELEMENTS (table), snmp_commit);
}

void snmp_init_mib_dot1d_stp_port_table(void)
{
    oid table_oid[] = { oid_dot1dStpPortTable };
    int index[]     = { ASN_INTEGER };

    register_table("dot1dStpPortTable",
                   table_oid,
                   OID_LENGTH (table_oid),
                   MIN_COLUMN,
                   MAX_COLUMN,
                   index,
		   ARRAY_ELEMENTS (index),
                   table_handler,
                   table_get_first,
                   table_get_next,
                   table_load,
                   table_free,
                   HANDLER_CAN_RWRITE);
}

#endif
