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

#include "mstp.h"
#include "config.h"
#include "snmp.h"

#include "libnsh/table.h"

#define MIN_COLUMN 1
#define MAX_COLUMN 6

typedef struct table_data_t table_data_t;
struct table_data_t {
    long         port;
    long         protocol_migration;
    long         admin_edge_port;
    long         oper_edge_port;
    long         admin_point_to_point;
    long         oper_point_to_point;
    long         admin_path_cost;

    table_data_t *next;
};

static struct table_data_t *table_head = NULL;

static NetsnmpCacheLoad table_load;
static NetsnmpCacheFree table_free;
static Netsnmp_First_Data_Point table_get_first;
static Netsnmp_Next_Data_Point  table_get_next;
static Netsnmp_Node_Handler table_handler;

static nsh_table_index_t idx[] = {
   NSH_TABLE_INDEX (ASN_INTEGER, table_data_t, port, 0),
};

nsh_table_free(table_free, table_data_t, table_head)
nsh_table_get_first(table_get_first, table_get_next, table_head)
nsh_table_get_next(table_get_next, table_data_t, idx, 1)

static void table_create_entry(long port,
                               long protocol_migration,
                               long admin_edge_port,
                               long oper_edge_port,
                               long admin_point_to_point,
                               long oper_point_to_point,
                               long admin_path_cost)
{
    table_data_t *entry;

     entry = SNMP_MALLOC_TYPEDEF (table_data_t);
     if (!entry)
         return;

     entry->port                 = port;
     entry->protocol_migration   = protocol_migration;
     entry->admin_edge_port      = admin_edge_port;
     entry->oper_edge_port       = oper_edge_port;
     entry->admin_point_to_point = admin_point_to_point;
     entry->oper_point_to_point  = oper_point_to_point;
     entry->admin_path_cost      = admin_path_cost;

     entry->next = table_head;
     table_head  = entry;
}

int snmp_get_ifup(char *ifname)
{
    FILE *fp;
    char fname[32];
    char buf[8];
    int result = 0;

    snprintf(fname, sizeof(fname), "/sys/class/net/%s/carrier", ifname);
    fp = fopen(fname, "r");
    if (!fp)
        return 0;
    if (fgets (buf, sizeof(buf), fp))
        result = strtoul (buf, NULL, 0);
    fclose (fp);

    return (result == 1) ? 1 : 2;
}

static int table_load (netsnmp_cache *cache, void* vmagic)
{
    CIST_BridgeStatus s;
    char *br_name = INTERFACE_BRIDGE;
    char root_port_name[IFNAMSIZ];
    int br_index = get_index(br_name, "bridge");
    int i;
    cfg_t *parse_cfg;

    if (br_index < 0)
        return 0;

    if (CTL_get_cist_bridge_status(br_index, &s, root_port_name))
        return 0;

    parse_cfg = parse_conf(MSTPD_CONFIG_FILE);
    if(!parse_cfg)
	return 0;

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

        table_create_entry (port_index,
                            2,   /* XXX: FIXME! false(2), we have no protocol migration */
                            ps.admin_edge_port ? 2 : 1,
                            ps.oper_edge_port  ? 2 : 1,
                            0,   /* forceTrue(0), indicating bridge must be connected to point to point link. */
			    snmp_get_ifup(port_p),
                            ps.admin_external_port_path_cost);
    }

    return 0;
}

static int table_handler(netsnmp_mib_handler *handler,
			 netsnmp_handler_registration *reginfo,
			 netsnmp_agent_request_info *reqinfo,
			 netsnmp_request_info *requests)
{
    nsh_table_entry_t table[] = {
        NSH_TABLE_ENTRY_RO (ASN_INTEGER, table_data_t, protocol_migration,   0),
        NSH_TABLE_ENTRY_RO (ASN_INTEGER, table_data_t, admin_edge_port,      0),   /* XXX: FIXME! RW support, see __dot1d_set_stp_port_admin_edge */
        NSH_TABLE_ENTRY_RO (ASN_INTEGER, table_data_t, oper_edge_port,       0),
        NSH_TABLE_ENTRY_RO (ASN_INTEGER, table_data_t, admin_point_to_point, 0),
        NSH_TABLE_ENTRY_RO (ASN_INTEGER, table_data_t, oper_point_to_point,  0),
        NSH_TABLE_ENTRY_RO (ASN_INTEGER, table_data_t, admin_path_cost,      0)    /* XXX: FIXME! RW support, see __dot1d_set_stp_port_path_cost */
    };

    return nsh_handle_table(reqinfo, requests, table, COUNT_OF (table));
}

void snmp_init_mib_dot1d_stp_ext_port_table(void)
{
    oid table_oid[] = { oid_dot1dStpExtPortTable };
    int index[]     = { ASN_INTEGER };

    nsh_register_table("dot1dStpExtPortTable",
		       table_oid,
		       OID_LENGTH (table_oid),
		       MIN_COLUMN,
		       MAX_COLUMN,
		       index,
		       COUNT_OF (index),
		       table_handler,
		       table_get_first,
		       table_get_next,
		       table_load,
		       table_free,
		       HANDLER_CAN_RWRITE);
}

#endif
