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

  This code will provide the config for MSTPD daemon.
  On SIGHUP the daemon fetch new config file from /etc/mstpd-<instance-nr>.conf.
  On SIGUSR1 the daemon will produce status files to /var/run/mstpd/<instance-nr>.

******************************************************************************/
#ifndef CONFIG_H
#define CONFIG_H
#include <paths.h>              /* _PATH_VARRUN */

#ifdef  __LIBC_HAS_VERSIONSORT__
#define sorting_func    versionsort
#else
#define sorting_func    alphasort
#endif

#define MSTPD_STATUS_FILE   "/var/run/mstpd/mstpd.status"
#define MSTPD_STATUS_FILE_TMP "/var/run/mstpd/mstpd.tmp"

#define SYSFS_CLASS_NET     "/sys/class/net"
#define SYSFS_PATH_MAX      256
#define MSTP_STATUS_PATH    "/var/run/mstpd"
#define INTERFACE_BRIDGE    "br0"

#define ETHTOOL_PORT_MASK_FAST_ETHERNET (SUPPORTED_10baseT_Half |	\
					 SUPPORTED_10baseT_Full |	\
					 SUPPORTED_100baseT_Half |	\
					 SUPPORTED_100baseT_Full |	\
					 SUPPORTED_Autoneg |		\
					 SUPPORTED_TP |			\
					 SUPPORTED_MII)

#define ETHTOOL_PORT_MASK_FAST_ETHERNET_OPTIC (SUPPORTED_100baseT_Full)

#define ETHTOOL_PORT_MASK_FAST_ETHERNET_COPPER_SPF (SUPPORTED_100baseT_Full | \
						    SUPPORTED_TP)

#define ETHTOOL_PORT_MASK_FAST_ETHERNET_COPPER_SPF_AUTO (SUPPORTED_100baseT_Full | \
                                                         SUPPORTED_Autoneg)

#define ETHTOOL_PORT_MASK_GIGA_ETHERNET_OPTIC (SUPPORTED_1000baseT_Half | \
                                               SUPPORTED_1000baseT_Full | \
					       SUPPORTED_Autoneg)

#define ETHTOOL_PORT_MASK_GIGA_ETHERNET (SUPPORTED_10baseT_Half |	\
					 SUPPORTED_10baseT_Full |	\
					 SUPPORTED_100baseT_Half |	\
					 SUPPORTED_100baseT_Full |	\
                                         SUPPORTED_1000baseT_Half |	\
					 SUPPORTED_1000baseT_Full |	\
					 SUPPORTED_Autoneg |		\
					 SUPPORTED_TP |			\
					 SUPPORTED_MII)

#define ETHTOOL_PORT_MASK_GIGA_ETHERNET_COPPER_SFP (SUPPORTED_1000baseT_Full | SUPPORTED_TP)
#define ETHTOOL_PORT_MASK_GIGA_ETHERNET_COPPER_SFP_AUTO (SUPPORTED_1000baseT_Full | \
                                                         SUPPORTED_Autoneg)


int  config(void);
int  get_index(const char *ifname, const char *doc);
int  mstp_write_status_file(int display);
void mstp_update_status(void);
int  get_rstp_pid(void);

int  CTL_set_debug_level(int level);
int  CTL_add_bridges(int *br_array, int* *ifaces_lists);
int  CTL_set_cist_port_config(int br_index, int port_index, CIST_PortConfig *cfg);
int  CTL_set_cist_bridge_config(int br_index, CIST_BridgeConfig *cfg);
int  CTL_set_msti_bridge_config(int br_index, __u16 mstid, __u8 bridge_priority);
int  CTL_get_cist_bridge_status(int br_index, CIST_BridgeStatus *status, char *root_port_name);
int  CTL_get_cist_port_status(int br_index, int port_index, CIST_PortStatus *status);
#endif 
