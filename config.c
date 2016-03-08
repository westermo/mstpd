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

#include <assert.h>
#include <net/if.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <libgen.h>             /* basename() */
#include <linux/filter.h>
#include <linux/sockios.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <asm/byteorder.h>
#include <confuse.h>
#include <sys/ioctl.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include "epoll_loop.h"
#include <linux/in6.h>
#include <linux/if_bridge.h>

#include "mstp.h"
#include "log.h"
#include "leds.h"
#include "config.h"

#define MAX_NUM_ATUS        64                     /* Maxumum number of ATU databases */

extern char *__progname;

struct port_data_t
{
    int edge;
    int port_path_cost;
    int enable;
};

struct spanning_conf_t
{
    int prio;
    int forward_delay;
    int hello_time;
    int max_age;
    struct port_data_t port_conf[MAX_NUM_ATUS];
};

struct spanning_conf_t stp_port_conf;
static struct epoll_event_handler signal_event;

cfg_t *parse_conf(char *conf)
{
    cfg_opt_t ports_opts[] = {
	CFG_STR("ifname",      0,         CFGF_NONE),
	CFG_BOOL("enable",     cfg_true,  CFGF_NONE),
	CFG_BOOL("admin-edge", cfg_false, CFGF_NONE),
	CFG_INT ("path-cost",  0,         CFGF_NONE),
	CFG_END()
    };
    cfg_opt_t opts[] = {
	CFG_STR ("name",	         0, CFGF_NONE),
	CFG_INT ("prio",	         0, CFGF_NONE),
	CFG_INT ("forward-delay",	15, CFGF_NONE),
	CFG_INT ("hello-time",	 2, CFGF_NONE),
	CFG_INT ("max-age",        0, CFGF_NONE),
	CFG_SEC ("ports", ports_opts, CFGF_MULTI | CFGF_TITLE),
	CFG_END()
    };
    cfg_t *cfg = cfg_init(opts, CFGF_NONE);

    switch (cfg_parse(cfg, conf))
    {
    case CFG_FILE_ERROR:
	fprintf(stderr, "Cannot read configuration file %s: %s\n", conf, strerror(errno));
	return NULL;
	
    case CFG_PARSE_ERROR:
	fprintf(stderr, "Error when parsing configuration file %s: %s\n", conf, strerror(errno));
	return NULL;
         
    case CFG_SUCCESS:
	break;
    }

    return cfg;
}

int get_rstp_pid(void)
{
    char filename[] = "/var/run/mstpd.pid";
    FILE *fp;
    int pid = 0;
    
    fp = fopen (filename, "r");
    if (!fp)
	return 0;
    
    fscanf (fp, "%d", &pid);
    fclose (fp);
    return pid;
}

static int read_config(cfg_t *parse_cfg)
{
    size_t i;
    int forward_delay = 0;
    int hello_time = 0;
    int max_age = 0;
    int prio = 0;

    /* Save the prio value in table for later use. */
    prio = cfg_getint(parse_cfg, "prio");
    if(prio > 255)
	prio = 255;
    stp_port_conf.prio = prio;

    forward_delay = cfg_getint(parse_cfg, "forward-delay");
    if(forward_delay > 255)
	forward_delay = 255;
    stp_port_conf.forward_delay = forward_delay;

    hello_time = cfg_getint(parse_cfg, "hello-time");
    if(hello_time > 255)
	hello_time = 255;
    stp_port_conf.hello_time = hello_time;

    max_age = cfg_getint(parse_cfg, "max-age");
    if(max_age > 255)
	max_age = 255;
    stp_port_conf.max_age = max_age;

    for(i = 0; i < cfg_size(parse_cfg, "ports"); i++)
    {
	int port_index = 0;
	int port_path_cost = 0;
	int enable = 0, edge = 0;
	char * name;
	cfg_t * cfg_port = cfg_getnsec(parse_cfg, "ports", i);

	name = cfg_getstr(cfg_port, "ifname");
	port_index = if_nametoindex(name);
	if(!port_index)
	{
	    ERROR("Could not find ifindex for %s", name);
	    continue;
	}
	LOG("%s name=%s index=%d", __FUNCTION__, name, port_index);

	/* Save the enable value in table for later use. */
	enable = cfg_getbool(cfg_port, "enable");
	stp_port_conf.port_conf[port_index].enable = enable;

	/* Save the edge value in table for later use. */
	edge = cfg_getbool(cfg_port, "admin-edge");
	stp_port_conf.port_conf[port_index].edge = edge;

	/* Save the port_path_cost value in table for later use. */
	port_path_cost = cfg_getint(cfg_port, "path-cost");
	stp_port_conf.port_conf[port_index].port_path_cost = port_path_cost;
    }

    return 0;
}

int port_is_enabled(char * ifname)
{
    int port_index = if_nametoindex(ifname);

    if(port_index > 0 && stp_port_conf.port_conf[port_index].enable)
    {
	LOG("%s ret=1 index=%d enable=%d\n", __FUNCTION__, port_index,
	    stp_port_conf.port_conf[port_index].enable);
	return 1;
    }
    LOG("%s ret=0 index=%d enable=%d\n", __FUNCTION__, port_index,
	stp_port_conf.port_conf[port_index].enable);

    return 0;
}

/* filter out . .. and eth3.10, i.e., this and parent directory as well
 * as 1Q interfaces ... */
static int not_dot_dotdot(const struct dirent *entry)
{
    if(strchr (entry->d_name, '.'))
	return 0;
   
    if(port_is_enabled((char *)entry->d_name))
    {
	INFO("%s name=%s. return 1\n", __func__, entry->d_name);
	return 1;
    }
    return 0;  
}

static int get_port_list(const char *br_ifname, struct dirent ***namelist)
{
    int res;
    char buf[SYSFS_PATH_MAX];

    snprintf(buf, sizeof(buf), SYSFS_CLASS_NET "/%s/brif", br_ifname);
    res = scandir(buf, namelist, not_dot_dotdot, sorting_func);
    if(res < 0)
	ERROR("Error getting list of all ports of bridge %s", br_ifname);

    return res;
}

int get_index(const char *ifname, const char *doc)
{
    int r = if_nametoindex(ifname);

    if(0 == r)
    {
	ERROR("Cannot find interface index for %s %s. Not a valid interface.", doc, ifname);
	return -1;
    }

    return r;
}

static int cmd_addbridge(char *bridge_name)
{
    int i, j, res, ifcount, brcount = 1;
    int *br_array;
    int* *ifaces_lists;
    char filename[128];

    if(NULL == (br_array = malloc((brcount + 1) * sizeof(int))))
    {
    out_of_memory_exit:
	ERROR("out of memory, brcount = %d", brcount);
	return -1;
    }
   
    if(NULL == (ifaces_lists = malloc(brcount * sizeof(int*))))
    {
	free(br_array);
	goto out_of_memory_exit;
    }

    /* Create directory for MSTP */
    snprintf (filename, sizeof (filename), "%s", MSTP_STATUS_PATH);
    mkdir(filename, 0755);

    br_array[0] = brcount;
    for(i = 1; i <= brcount; ++i)
    {
	struct dirent **namelist;

	br_array[i] = get_index(bridge_name, "bridge");

        /* Create directory for MSTP instance */
	snprintf (filename, sizeof (filename), "%s/%d", MSTP_STATUS_PATH, i-1);
	mkdir(filename, 0755);

	if(0 > (ifcount = get_port_list(bridge_name, &namelist)))
	{
        ifaces_error_exit:
	    for(i -= 2; i >= 0; --i)
		free(ifaces_lists[i]);
	    free(ifaces_lists);
	    free(br_array);
	    return ifcount;
	}

	if(NULL == (ifaces_lists[i - 1] = malloc((ifcount + 1) * sizeof(int))))
	{
	    ERROR("out of memory, bridge %s, ifcount = %d", bridge_name, ifcount);
	    for(j = 0; j < ifcount; ++j)
		free(namelist[j]);
	    free(namelist);
	    ifcount = -1;
	    goto ifaces_error_exit;
	}

	ifaces_lists[i - 1][0] = ifcount;
	for(j = 1; j <= ifcount; ++j)
	{
	    ifaces_lists[i - 1][j] = get_index(namelist[j - 1]->d_name, "port");
	    free(namelist[j - 1]);

	    /* Create file structure for status for each bridge port */
	    snprintf(filename, sizeof(filename), "%s/%d/%s", MSTP_STATUS_PATH, i-1, namelist[j - 1]->d_name);
	    mkdir(filename, 0755);
	}
	free(namelist);
    }

    res = CTL_add_bridges(br_array, ifaces_lists);

    for(i = 0; i < brcount; ++i)
	free(ifaces_lists[i]);
    free(ifaces_lists);
    free(br_array);

    return res;
}

int mstp_bridge_enable_stp(int sd, char *brname, int onoff)
{
    struct ifreq ifr;
    unsigned long args[4];

    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_data = (char *) &args;
    strncpy(ifr.ifr_name, brname, sizeof(ifr.ifr_name));
    args[0] = (unsigned long)BRCTL_SET_BRIDGE_STP_STATE;
    args[1] = (unsigned long) onoff;
    args[2] = (unsigned long) sizeof(args);
    args[3] = 0;
    if(ioctl(sd, SIOCDEVPRIVATE, &ifr) < 0)
    {
	ERROR ("When enabling the stp on the bridge with BRCTL_SET_BRIDGE_STP_STATE");
	return 1;
    }
    else
	return 0;
}

int mstp_set_max_age(int br_index, int max_age)
{
    CIST_BridgeConfig cfg;
    int ret;
   
    /* setup the bridge max aging time. */
    memset(&cfg, 0, sizeof(cfg));
    cfg.bridge_max_age = max_age;
    cfg.set_bridge_max_age = true;
    ret = CTL_set_cist_bridge_config(br_index, &cfg);
    if (ret == -1)
    {
	ERROR ("CTL_set_cist_bridge_config return max_age error %d", ret);
	return 1;
    }

    return 0;
}

int mstp_set_hello_time(int br_index, int hello_time)
{
    CIST_BridgeConfig cfg;
    int ret;
   
    /* setup the bridge hello time. */
    memset(&cfg, 0, sizeof(cfg));
    cfg.bridge_hello_time = hello_time;
    cfg.set_bridge_hello_time = true;
    ret = CTL_set_cist_bridge_config(br_index, &cfg);
    if(ret == -1)
    {
	ERROR ("CTL_set_cist_bridge_config return hello time error %d\n", ret);
	return 1;
    }

    return 0;
}

int mstp_set_forward_delay(int br_index, int delay)
{
    CIST_BridgeConfig cfg;
    int ret;
   
    /* setup the bridge forward delay. */
    memset(&cfg, 0, sizeof(cfg));
    cfg.bridge_forward_delay = delay;
    cfg.set_bridge_forward_delay = true;
    ret = CTL_set_cist_bridge_config(br_index, &cfg);
    if(ret == -1)
    {
	ERROR("CTL_set_cist_bridge_config return bridge_forward_delay, %d\n", delay);
	return 1;
    }
   
    return 0;
}

int mstp_set_prio(int br_index, int mstid, int prio)
{
    int ret;
    /* setup the bridge prio. */
    if(prio > 255)
	prio = 255;
    ret = CTL_set_msti_bridge_config(br_index, mstid, prio);
    LOG("MSTPD ret=%d br_index=%d id=%d prio=%d\n", ret, br_index, mstid, prio);
    if (ret == -1)
    {
	ERROR("CTL_set_msti_bridge_config return bridge_prio error %d\n", ret);
	return 1;
    }
   
    return 0;
}

int mstp_set_port_path_cost(int br_index, int port_index, int port_path_cost)
{
    CIST_PortConfig p_cfg;
    int ret;
   
    /* setup the ports path cost. */
    port_path_cost = stp_port_conf.port_conf[port_index].port_path_cost;
    memset(&p_cfg, 0, sizeof(p_cfg));
    p_cfg.admin_external_port_path_cost = port_path_cost;
    p_cfg.set_admin_external_port_path_cost = true;
    ret = CTL_set_cist_port_config(br_index, port_index, &p_cfg);
    if(ret == -1)
    {
	ERROR ("CTL_set_cist_port_config return port_path_cost error %d", ret);
	return 1;
    }

    return 0;
}

int mstp_set_port_edge(int br_index, int port_index, int edge)
{
    CIST_PortConfig p_cfg;
    int ret;

    memset(&p_cfg, 0, sizeof(p_cfg));
    p_cfg.admin_edge_port = (bool)edge;
    p_cfg.set_admin_edge_port = true;
    ret = CTL_set_cist_port_config(br_index, port_index, &p_cfg);
    if (ret == -1)
    {
	ERROR("CTL_set_cist_port_config return admin_edge_port error %d", ret);
	return 1;
    }
   
    return 0;
}
int mstp_force_protocol_version(int br_index, int proto)
{
    CIST_BridgeConfig cfg;
    int ret;
   
    /* setup the bridge protocol version. */
    memset(&cfg, 0, sizeof(cfg));
    cfg.protocol_version = proto;
    cfg.set_protocol_version = true;
    ret = CTL_set_cist_bridge_config(br_index, &cfg);
    LOG("MSTPD ret=%d br_index=%d\n", ret, br_index);
    if(ret == -1)
    {
	ERROR("Could not force STP protocol version, protocol %d\n", proto);
	return 1;
    }
   
    return 0;
}

/* Handle SIGHUP, i.e. configuration changes at runtime. */
static int reconfig(void)
{
    cfg_t *parse_cfg;
    char *br_name = INTERFACE_BRIDGE;
    int sd = socket(AF_INET, SOCK_STREAM, 0);
    int br_index;
    size_t i;

    LOG("Entering reconfig");
    memset(&stp_port_conf, 0, sizeof(stp_port_conf));
   
    parse_cfg = parse_conf("/etc/mstpd-0.conf");
    if(!parse_cfg || read_config(parse_cfg))
    {
	ERROR("Couldn't read configuration from file!!!");
	return 1;
    }

    br_index = if_nametoindex(br_name);
    LOG("br_name=%s index=%d\n", br_name, br_index);
    if(!br_index)         /* Error */
    {
	ERROR("Could not find ifindex for %s", br_name);
	return -1;
    }

    /* Add a bridge, mstpctl addbridge bridge */
    cmd_addbridge(br_name);

    mstp_bridge_enable_stp(sd, br_name, 1);
    mstp_force_protocol_version(br_index, protoRSTP);
    mstp_set_prio(br_index, 0, stp_port_conf.prio);
    mstp_set_forward_delay(br_index, stp_port_conf.forward_delay);
    mstp_set_hello_time(br_index, stp_port_conf.hello_time);
    mstp_set_max_age(br_index, stp_port_conf.max_age); 

    for(i = 0; i < cfg_size(parse_cfg, "ports"); i++)
    {
	int port_index;
	char * ifname;

	cfg_t * cfg_port = cfg_getnsec(parse_cfg, "ports", i);

	ifname = cfg_getstr(cfg_port, "ifname");

	if(port_is_enabled(ifname))
	{
	    port_index = if_nametoindex(ifname);
	    if(!port_index)
	    {
		ERROR("Could not find ifindex for %s", ifname);
		continue;
	    }

	    mstp_set_port_edge(br_index, port_index, stp_port_conf.port_conf[port_index].edge);
	    mstp_set_port_path_cost(br_index, port_index,
				    stp_port_conf.port_conf[port_index].port_path_cost);
	}
    }
    cfg_free(parse_cfg);
    close(sd);

    return 0;
}

static int path_is_directory (const char* path)
{
    struct stat s_buf;

    if (stat(path, &s_buf))
        return 0;

    return S_ISDIR(s_buf.st_mode);
}

static void delete_folder_tree (const char* directory_name)
{
    DIR*            dp;
    struct dirent*  ep;
    char            p_buf[512] = {0};

    dp = opendir(directory_name);
    while ((ep = readdir(dp)) != NULL) {

	if((!strcmp(ep->d_name, ".") || !strcmp(ep->d_name, "..")))
	    continue;

        sprintf(p_buf, "%s/%s", directory_name, ep->d_name);
        if (path_is_directory(p_buf))
            delete_folder_tree(p_buf);
        else
            unlink(p_buf);
    }

    closedir(dp);
    rmdir(directory_name);
}

static void signal_handler_cb(uint32_t events, struct epoll_event_handler *h)
{
    struct signalfd_siginfo info;
    ssize_t bytes;
    unsigned sig;

    bytes = read(h->fd, &info, sizeof(info));
    if(bytes != sizeof(info))
    {
	fprintf(stderr, "ERROR: Invalid size of signal information ");
    }
    else
    {
	sig = info.ssi_signo;
	if((sig == SIGTERM) || (sig == SIGINT) || (sig == SIGQUIT))
	{
	    char filename[160];

	    snprintf(filename, sizeof(filename), "/var/run/%s.pid", __progname);
	    remove(filename);
	    delete_folder_tree(MSTP_STATUS_PATH);
	    leds_off();
	    exit(0);
	}
    
	if(sig == SIGHUP)
	    reconfig ();
	
	if (sig == SIGUSR1)
	{
	    mstp_write_status_file(1);
	    mstp_update_status ();
	}
    }
}

static int signal_handler_init(void)
{
    int err;
    sigset_t sigset;
    int fd;

    /* Create a sigset of all the signals that we're interested in */
    err = sigemptyset(&sigset);
    assert(err == 0);
    err = sigaddset(&sigset, SIGTERM);
    assert(err == 0);
    err = sigaddset(&sigset, SIGHUP);
    assert(err == 0);
    err = sigaddset(&sigset, SIGINT);
    assert(err == 0);
    err = sigaddset(&sigset, SIGQUIT);
    assert(err == 0);
    err = sigaddset(&sigset, SIGUSR1);
    assert(err == 0);

    /* We must block the signals in order for signalfd to receive them */
    err = sigprocmask(SIG_BLOCK, &sigset, NULL);
    assert(err == 0);
  
    /* Create the signalfd */
    fd = signalfd(-1, &sigset, 0);
    assert(fd != -1);

    signal_event.fd = fd;
    signal_event.handler = signal_handler_cb;
  
    if(0 == add_epoll(&signal_event))
    {
	return 0;
    }

    close(fd);

    return 1;
}

int config(void)
{
    led_init();
    reconfig ();
    signal_handler_init();

    return 0;
}
