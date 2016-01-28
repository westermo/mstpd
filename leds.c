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

  Authors: Greger Wrang <greger.wrang@westermo.se>

******************************************************************************/
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "log.h"

#define prod_family         "/boot/signature/family"
static char family_str[50] = {""};
static char green_led[200];

static void change_to_lower_case(char *str)
{
    int i;
    
    for(i = 0; str[i]; i++)
	str[i] = tolower(str[i]);
}

static char *read_str_from_file(char *file, char *str)
{
    FILE *fd = NULL;

    fd = fopen(file, "r");
    if(fd == NULL)
	return NULL;
   
    fscanf(fd, "%s", str);
    fclose(fd);

    return str;
}

static void write_val_to_file(char *file, int val)
{
    FILE *fd = NULL;

    fd = fopen(file, "w");
    if (fd == NULL)
	return;
   
    fprintf(fd, "%d", val);
    fclose(fd);
    return;
}

static void write_str_to_file(char *file, char *str)
{
    FILE *fd = NULL;

    fd = fopen(file, "w");
    if(fd == NULL)
	return;
    
    fprintf(fd, str, 1);
    fclose(fd);
    return;
}

static void led_flash(char *led_dir, int delay)
{
    char fname[50];

    sprintf(fname, "%s/%s", led_dir, "trigger");
    write_str_to_file(fname, "timer");

    sprintf(fname, "%s/%s", led_dir, "delay_on");
    write_val_to_file(fname, delay);

    sprintf(fname, "%s/%s", led_dir, "delay_off");
    write_val_to_file(fname, delay);

    return;
}

static void led_on(char *led_dir, int on)
{
    char fname[50];

    sprintf(fname, "%s/%s", led_dir, "brightness");
    write_val_to_file(fname, on);

    return;
}

void led_root(int root_switch)
{
    if (!root_switch)
	led_flash(green_led, 1000);
    else
	led_on(green_led, 1);
}

void leds_off(void)
{
    led_on(green_led, 0);
}

void led_init(void)
{
    char red_led[200];

    if(read_str_from_file(prod_family, family_str))
    {
	change_to_lower_case(family_str);
	sprintf(red_led, "/sys/class/leds/%s:red:rstp", family_str);
	sprintf(green_led, "/sys/class/leds/%s:green:rstp", family_str);
	LOG("%s - Family %s, %s, %s\n", __FUNCTION__, family_str, red_led, green_led);

	led_on(red_led, 0);
    }
}

