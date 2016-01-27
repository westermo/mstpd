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

  Authors: Anders Öhlander <anders.ohlander@westermo.se>
           Greger Wrang    <greger.wrang@westermo.se> 

  This code fetches MSTPD status and write it to the /var/run/mstpd/<instance-nr>.

******************************************************************************/

#include <ev.h>                 /* libev types and functions */
#include <getopt.h>
#include <libgen.h>             /* basename() */
#include <linux/filter.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <asm/byteorder.h>
#include <sys/stat.h>           /* mkdir() */
#include <fcntl.h>
#include <netinet/ether.h>
#include <assert.h>
#include <confuse.h>
#include <linux/ethtool.h>
#include <stdbool.h>

#include "netif_utils.h"
#include "mstp.h"
#include "log.h"
#include "leds.h"
#include "config.h"

extern char *__progname;

#define PRT_ID_ARGS(x) ((GET_PRIORITY_FROM_IDENTIFIER(x) >> 4) & 0x0F), \
	GET_NUM_FROM_PRIO(x)

cfg_t *parse_conf(char *conf);

char *port_state_to_string(int state, int ansi)
{
    char *pretty[] = {
	"Disabled  ",
	"\e[4mListening\e[0m  ",
	"\e[4mLearning\e[0m  ",
	"\e[1mForwarding\e[0m",
	"\e[7mBlocking\e[0m  ",
	NULL,
    };
    char *regular[] = {
	"DISABLED",
	"LISTENING",
	"LEARNING",
	"FORWARDING",
	"BLOCKING",
	NULL,
    };

    return ansi ? pretty[state] : regular[state];
}

const char *port_typestr(char *ifname)
{
    switch(ethtool_supported_media (ifname))
    {
    case ETHTOOL_PORT_MASK_GIGA_ETHERNET_OPTIC:
	return "1000-SFP";

    case ETHTOOL_PORT_MASK_GIGA_ETHERNET:
	return "10/1000TX";

    case ETHTOOL_PORT_MASK_FAST_ETHERNET_OPTIC:
	return "100FX";

    case ETHTOOL_PORT_MASK_FAST_ETHERNET_COPPER_SPF:
    case ETHTOOL_PORT_MASK_FAST_ETHERNET_COPPER_SPF_AUTO: 
	return "100TX-SFP";

    case ETHTOOL_PORT_MASK_GIGA_ETHERNET_COPPER_SFP:
    case ETHTOOL_PORT_MASK_GIGA_ETHERNET_COPPER_SFP_AUTO:
	return "1000TX-SFP";

    case ETHTOOL_PORT_MASK_FAST_ETHERNET:
	return "10/100TX";

    default:
	return "NO-SFP";
    }

    return NULL;
}

int sys_ether_ntoa(__u8 *mac, char *str, size_t len)
{
    assert(mac);
    assert(str);
    assert(len > 17);

    snprintf(str, len, "%02x:%02x:%02x:%02x:%02x:%02x",
	     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    return 0;
}

static int write_string(char *file, char *str)
{
    FILE *fp;

    fp = fopen(file, "w");
    if (!fp)
    {
	ERROR("Failed opening %s for writing", file);
	return 1;
    }
    fprintf(fp,"%s\n", str);

    fclose(fp);

    return 0;
}

static int set_instance_value(int instance_num, char *name, int value)
{
    char filename[160];
    char str[50];

    snprintf(filename, sizeof (filename), "%s/%d/%s", MSTP_STATUS_PATH, instance_num, name);
    snprintf(str, sizeof (str), "%d", value);

    return write_string(filename, str);
}

static int set_instance_string(int instance_num, char *name, char *str)
{
    char filename[160];

    snprintf(filename, sizeof (filename), "%s/%d/%s", MSTP_STATUS_PATH, instance_num, name);

    return write_string(filename, str);
}

static int set_port_value(int instance_num, char *port_name, char *var_name, int value)
{
    char filename[160];
    char str[50];

    snprintf(filename, sizeof(filename), "%s/%d/%s/%s", MSTP_STATUS_PATH, instance_num, port_name, var_name);
    snprintf(str, sizeof(str), "%d", value);

    return write_string(filename, str);
}

static int set_port_string(int instance_num, char *port_name, char *var_name, char *str)
{
    char filename[160];

    snprintf (filename, sizeof(filename), "%s/%d/%s/%s", MSTP_STATUS_PATH, instance_num, port_name, var_name);

    return write_string(filename, str);
}


int set_mstp_hello_time(int value)
{
    return set_instance_value(0, "hello_time", value);
}

int set_mstp_max_age(int value)
{
    return set_instance_value(0, "max_age", value);
}

int set_forward_delay(int value)
{
    return set_instance_value(0, "forward_delay", value);
}

int set_hold_count(int value)
{
    return set_instance_value(0, "hold_count", value);
}

/* Data to save for each instance */
int set_no_topology_change(int instance_num, int value)
{
    return set_instance_value(instance_num, "no_topology_change", value);
}

int set_time_since_topology_change(int instance_num, int value)
{
    return set_instance_value(instance_num, "time_since_topology_change", value);
}

int set_bridge_id_prio(int instance_num, int value)
{
    return set_instance_value(instance_num, "bridge_id_prio", value);
}

int set_bridge_mac_adr(int instance_num, char *str)
{
    return set_instance_string(instance_num, "bridge_mac_adr", str);
}

int set_designated_root_prio(int instance_num, int value)
{
    return set_instance_value(instance_num, "designated_root_prio", value);
}

int set_designated_root_mac_adr(int instance_num, char *str)
{
    return set_instance_string(instance_num, "designated_root_mac_adr", str);
}

int set_mstp_root_port(int instance_num, int value, int touch)
{
    static int old_value = -100; 

    if((old_value != value) || (touch))
    {
	led_root(value);

	if(touch)
	    value = old_value;

	if(set_instance_value (instance_num, "root_port", value))
	{
	    printf("Failed writing root port\n");
	    return 1;
	}
	old_value = value;
	return set_instance_value(instance_num, "root_port", value);
    }

    return 0;
}

int set_mstp_root_path_cost(int instance_num, int value)
{
    return set_instance_value(instance_num, "root_path_cost", value);
}

int set_port_state(int instance_num, char *port_name, int value)
{
    return set_port_value(instance_num, port_name, "state", value);
}

int set_port_priority(int instance_num, char *port_name, int value)
{
    return set_port_value(instance_num, port_name, "priority", value);
}

int set_port_path_cost(int instance_num, char *port_name, int value)
{
    return set_port_value(instance_num, port_name, "path_cost", value);
}

int set_port_designated_bridge_mac_adr(int instance_num, char *port_name, char *str)
{
    return set_port_string(instance_num, port_name, "designated_bridge_mac_adr", str);
}

int set_port_designated_bridge_prio(int instance_num, char *port_name, int value)
{
    return set_port_value(instance_num, port_name, "designated_bridge_prio", value);
}

int set_port_designated_root_mac_adr(int instance_num, char *port_name, char *str)
{
    return set_port_string(instance_num, port_name, "designated_root_mac_adr", str);
}

int set_port_designated_root_prio(int instance_num, char *port_name, int value)
{
    return set_port_value(instance_num, port_name, "designated_root_prio", value);
}

int set_port_oper_edge(int instance_num, char *port_name, int value)
{
    return set_port_value(instance_num, port_name, "oper_edge", value);
}

int set_port_desingated_id(int instance_num, char *port_name, int value)
{
    return set_port_value(instance_num, port_name, "port_id", value);
}

int set_port_desingated_cost(int instance_num, char *port_name, int value)
{
    return set_port_value(instance_num, port_name, "designated_cost", value);
}

void mstp_update_status(void)
{
    char *br_name = INTERFACE_BRIDGE;
    CIST_BridgeStatus s;
    char root_port_name[IFNAMSIZ], buf[160];
    int br_index = get_index(br_name, "bridge");
    int sd = socket(AF_INET, SOCK_STREAM, 0);
    cfg_t *parse_cfg;
    size_t i;

    if(0 > br_index)
    {
	ERROR ("MSTPD SIGUSR1 error in bridge %s br_index %d.", br_name, br_index);
	return;
    }

    if(CTL_get_cist_bridge_status(br_index, &s, root_port_name))
    {
	ERROR("Failed to get bridge status %s (index %d)\n", br_name, br_index);
	return;
    }

    set_mstp_hello_time(s.bridge_hello_time);
    set_mstp_root_path_cost(0, s.root_path_cost);
    set_mstp_root_port(0, GET_NUM_FROM_PRIO(s.root_port_id), 0);

    set_mstp_max_age(s.bridge_max_age);
    set_forward_delay(s.bridge_forward_delay);

    set_no_topology_change(0, s.topology_change_count);
    set_time_since_topology_change(0, s.time_since_topology_change);

    sys_ether_ntoa(s.bridge_id.s.mac_address, buf, sizeof(buf));
    set_bridge_mac_adr(0, buf);

    set_bridge_id_prio(0, __be16_to_cpu(s.bridge_id.s.priority));

    sys_ether_ntoa(s.designated_root.s.mac_address, buf, sizeof(buf));
    set_designated_root_mac_adr(0, buf);

    set_designated_root_prio(0, __be16_to_cpu(s.designated_root.s.priority));

    set_hold_count(s.tx_hold_count);

    parse_cfg = parse_conf("/etc/mstpd-0.conf");
    if(!parse_cfg)
	return;

    for(i = 0; i < cfg_size(parse_cfg, "ports"); i++)
    {
	int port_index = 0;
	char *port_p;
	int ret;
	CIST_PortStatus ps;
	cfg_t * cfg_port = cfg_getnsec(parse_cfg, "ports", i);

	port_p = cfg_getstr(cfg_port, "ifname");
	port_index = if_nametoindex(port_p);

	if(cfg_getbool(cfg_port, "enable"))
	    LOG("RSTP port enabled");
	else
	{
	    LOG("Not RSTP port, continue");
	    continue;
	}

	if((ret = CTL_get_cist_port_status(br_index, port_index, &ps)))
	{
	    LOG("%s:%s Failed to get port state\n", br_name, port_p/* bridge.ifaces[i] */);
	    continue;
	}

	set_port_state(0, port_p, ps.state);
	set_port_priority(0, port_p, ps.port_id & 0xff);
	set_port_path_cost(0, port_p, ps.external_port_path_cost);
	set_port_oper_edge(0, port_p, ps.oper_edge_port);
	set_port_desingated_id(0, port_p, GET_NUM_FROM_PRIO(ps.port_id));
	set_port_desingated_cost(0, port_p, ps.designated_external_cost);


	sys_ether_ntoa(ps.designated_bridge.s.mac_address, buf, sizeof(buf));
	set_port_designated_bridge_mac_adr(0, port_p, buf);
	set_port_designated_bridge_prio(0, port_p, __be16_to_cpu(ps.designated_bridge.s.priority));

	sys_ether_ntoa(ps.designated_root.s.mac_address, buf, sizeof(buf));
	set_port_designated_root_mac_adr(0, port_p, buf);
	set_port_designated_root_prio(0, port_p, __be16_to_cpu(ps.designated_root.s.priority));
    }
    close(sd);
    cfg_free(parse_cfg);
}

int mstp_write_status_file(int display)
{
    char *br_name = INTERFACE_BRIDGE;
    CIST_BridgeStatus s;
    char root_port_name[IFNAMSIZ];
    int br_index = get_index(br_name, "bridge"), on = 0, i;
    FILE *fd = NULL;
    char temp[32] = "";
    cfg_t *parse_cfg2;

    if(0 > br_index)
    {
	ERROR("MSTPD SIGUSR1 error in bridge %s br_index %d.", br_name, br_index);
	return 1;
    }

    if(CTL_get_cist_bridge_status(br_index, &s, root_port_name))
    {
	ERROR("Failed to get bridge status %s (index %d)\n", br_name, br_index);
	return 1;
    }

    fd = fopen(MSTPD_STATUS_FILE_TMP, "w");
    if(fd == NULL)
    {
	ERROR("\nOpen user status file ......................[FAIL]\n");
	return 1;
    }
    on = get_rstp_pid();
    if (!on) return 1;

    snprintf(temp, sizeof(temp), "running as PID %d", on);

    fprintf(fd, "STP Enabled               : %s%s\n", on ? "Yes, " : "No", on ? temp : "");
    fprintf(fd, "Force Version             : RSTP\n");
   
    sys_ether_ntoa(s.bridge_id.s.mac_address, temp, sizeof(temp));
    fprintf(fd, "Bridge ID MAC Address     : %s\n", temp);
    fprintf(fd, "Bridge ID Priority        : %-3d (%d)\n",
	    __be16_to_cpu(s.bridge_id.s.priority) >> 12, __be16_to_cpu(s.bridge_id.s.priority));
    fprintf(fd, "Bridge Max Age            : %-3d          Bridge Hello Time : %u\n",
	    s.bridge_max_age, s.bridge_hello_time);
    fprintf(fd, "Bridge Forward Delay      : %-3d          Tx Hold Count     : %d\n",
	    s.bridge_forward_delay, s.tx_hold_count);
    fprintf(fd, "Topology Change Count     : %u\n", s.topology_change_count);
    fprintf(fd, "Time Since Last Change    : %u\n", s.time_since_topology_change);

    sys_ether_ntoa(s.designated_root.s.mac_address, temp, sizeof(temp));
    fprintf(fd, "Designated Root           : %s\n", temp);
    fprintf(fd, "Designated Root Path Cost : %u\n", s.root_path_cost);
    fprintf(fd, "Designated Root Port      : %s\n",
	    s.root_path_cost ? root_port_name : "This switch is root");
    fprintf(fd, "Designated Root Priority  : %d\n", __be16_to_cpu(s.designated_root.s.priority));


    fprintf(fd, "Port     Type         Cost        Priority  State      Edge   Designated Bridge\n");
    fprintf(fd, "===============================================================================\n");

    parse_cfg2 = parse_conf("/etc/mstpd-0.conf");
    if(!parse_cfg2)
	return 1;

    for(i = 0; i < cfg_size(parse_cfg2, "ports"); i++)
    {
	CIST_PortStatus ps;
	int port_index = 0;
	char *port_p;
	int ret = -1;

	cfg_t * cfg_port = cfg_getnsec(parse_cfg2, "ports", i);

	port_p = cfg_getstr(cfg_port, "ifname");
	port_index = if_nametoindex(port_p);

	if((ret = CTL_get_cist_port_status(br_index, port_index, &ps)))
	{
	    LOG ("%s:%s Failed to get port state\n", br_name, port_p);
	    continue;
	}

	sys_ether_ntoa(ps.designated_bridge.s.mac_address, temp, sizeof(temp));
	fprintf(fd, "%-7s  %-11.11s  %-9d   %-8d  %-10s %-5s  %s\n",
		port_p,
		port_typestr(port_p),
		ps.external_port_path_cost,
		ps.port_id & 0xff,
		port_state_to_string (ps.state, 1),
		ps.oper_edge_port ? "True" : "False",
		temp);
    }
   
    fclose(fd);

    if(display)
	system("/bin/cat " MSTPD_STATUS_FILE_TMP);

    rename(MSTPD_STATUS_FILE_TMP, MSTPD_STATUS_FILE);
    return 0;
}
