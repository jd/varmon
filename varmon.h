/* varmon.h VA RAID Monitor (VARMon)
    
Linux RAID Monitor for Mylex DAC960/1100 RAID controllers
    
Copyright 1999 by VA Linux Systems
Author Dragan Stancevic <visitor@valinux.com>
    
This program is free software; you may redistribute and/or modify it under
the terms of the GNU General Public License Version 2 as published by the
Free Software Foundation.
    
This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY
or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for complete details.
    
The author respectfully requests that any modifications to this software be
sent directly to him for evaluation and testing.
    
*/
/*
 Patches for varmon 1.0.2 submitted by:
	Wei Gao <wgao@valinux.com>,
	Kurt Garloff <garloff@suse.de>

	Thanks.
*/
/*
    
I hope you find this software useful.
    
Send comments, suggestions and bug reports to visitor@valinux.com
    
Thanks.
    
--Dragan
    
*/

#define BLUE_BLACK 2
#define BLUE_GREEN 15
#define BLUE_WHITE 9
#define BLUE_YELLOW 10
#define BLUE_RED 12
#define BLUE_CYAN 13
#define BLACK_YELLOW 5
#define BLACK_WHITE 8
#define CARDLIST_FOCUS (A_BOLD|COLOR_PAIR(CYAN_WHITE))
#define CARDLIST_NOFOCUS (COLOR_PAIR(CYAN_BLACK))
#define CARDLIST_LOCKED (COLOR_PAIR(CYAN_WHITE))
#define TITLE_NOFOCUS  (COLOR_PAIR(CYAN_BLACK))
#define CYAN_BLACK 3
#define CYAN_WHITE 7
#define CYAN_GREEN 17
#define CYAN_RED 18
#define CYAN_YELLOW 19
#define GREEN_BLACK 1
#define RED_YELLOW 4
#define WHITE_BLUE 6
#define YELLOW_GREEN 11
#define KEY_RETURN 343
#define KEY_RETURN1 10
#define KEY_ESCPE 27
#define KEY_r 114
#define KEY_R 82
#define KEY_INIT 9999
#define KEY_POST_INIT 10000
#define MEET_POINT "/proc/rd/"
#define NO_TABS 4
#define NO_REBUILD 0
#define REBUILD 1
#define CONSISTENCY 2
#define CARD0 "c0"
#define CARD1 "c1"
#define CARD2 "c2"
#define CARD3 "c3"
#define CARD4 "c4"
#define CARD5 "c5"
#define CARD6 "c6"
#define CARD7 "c7"
#define WARNING 2
#define SCR_BUFF 70
#define MAX_NAME 13
#define AUTHOR_NAME (5*MAX_NAME)
#define AUTHOR_EMAIL (2*MAX_NAME)
#define MEM_SIZE 6
#define CHANGE 2
#define DOUBLE_CHANGE 4
#define BIOS_GEO (DOUBLE_CHANGE*2)
#define MAX_SAF_TE (MAX_NAME*2)
#define ROOT 1
#define NO_WIN_FAIL 0
#define TRASH_ALL 1
#define BOTTOM_FAIL 2
#define TITLE_FAIL 3
#define CARDLIST_FAIL 4
#define STATUS_WIN_FAIL 5
#define INIT_CARD_FAIL 6
#define MAX_READ 4096
 
#define ON_SNAP 1
#define ON_SCREEN 2
#define MAX_LINE 80
#define LABEL 15
#define NO_LOCK 0
#define LOCK_CARDS 1
#define LOCK_CMD 2
#define LOCK_DEV 3
#define B_YES_NO 0x1
#define B_OK 0x2
#define B_NONE 0x4
#define B_ERR 0x8   

#define DEVICES 			0
#define TEMPERATURE			1
#define SCSI_READ_BUFFER 	0x3c
#define SCSI_WRITE_BUFFER 	0x3b

typedef struct {
	int channel;
	int id;
	char got_nstor;
	char fan[3][8];
	char ps[2][8];
	char temp[2][8];
} BCKP_T;

typedef struct {
	int no_bp;
	BCKP_T bp[3];
} SAFTE_T;				

/*
--------------------------------------------------------------------------------
This structure contains data for one physical device. Later we count physical
devices from the snapshot and create an array of all physical devices in
that particular snapshot.
*/
typedef struct {
	char target[LABEL];
	char state[LABEL];
	char blocks[LABEL];
	char focus;
} PHY_DEV;
 
typedef struct {
	char name[LABEL];
	char focus;
}CMD;
 
/*
--------------------------------------------------------------------------------
This structure contains the information for one logical drive. Manipulation
is the same as for physical devices.
*/
typedef struct {
	char dev_name[LABEL];
	char raid_l[LABEL];
	char state[LABEL];
	char blocks[LABEL];
	char wrt_opt[LABEL];
	char build_check;   /*Rebuild or check flag*/
	char completed[LABEL];  /*If in rebuild, percentage done*/
	char focus;
} LOG_DRIV;

/*
--------------------------------------------------------------------------------
This structure contains a curent snapshot of the card status, it is supposed
to be refreshed once per second by a clone of the main process. It contains
also the pseudo semaphores for the race conditions between the main and cloned
process. Main process creates an array of the cards snapshots in which clone
puts the information.
*/
typedef struct {
	char card_path[100];
	char dr_ver[MAX_LINE];
	char dr_au[MAX_LINE];
	char c_type[LABEL];
	char frmw[LABEL];
	char chnl[LABEL];
	char mem[LABEL];
	char pci_bus[LABEL];
	char device[LABEL];
	char function[LABEL];
	char io_addr[LABEL];
	char pci_addr[LABEL];
	char pci_mapped[LABEL];
	char irq[LABEL];
	char cq_depth[LABEL];
	char max_block_com[LABEL];
	char dq_depth[LABEL];
	char max_sg_seg[LABEL];
	char stripe_size[LABEL];
	char seg_size[LABEL];
	char bios_geo[LABEL];
	char fault_mngmt;
	char rebuild;
	char rb_dev[LABEL];
	char rb_done[LABEL];
	char no_ph_dev;
	char no_lg_drv;
	char got_dead;
	char got_safte;
	SAFTE_T safte;
	PHY_DEV *ph_dev;
	LOG_DRIV *lg_drv;
	char FirmwareType; // V2
} SNAPSHOT;

 
typedef struct {
	char name[MAX_NAME];
	char status[MAX_NAME];
	char path[(MAX_NAME*2)];
	int x;
	int y;
	char focus;
}CARD;
 
 
typedef struct {
	char label[MAX_NAME];
	int lpad, rpad, llpad, lrpad;
	int x;
	char focus;
}TAB;


typedef struct {
	WINDOW *root;
	WINDOW *bottom;
	WINDOW *title;
	WINDOW *cardlist;
	WINDOW *status_win;
}SWINDOWS;     
/*
-------------------------------------------------------------------------------
Define a global pointer to an a array of snapshots.
*/
SNAPSHOT *snap_all = 0;     /*A pointer to a array of all cards snapshots*/
#define IS_FIRMWARE_LEVEL_2(c) \
	((snap_all[c].FirmwareType == 2 )? 1 : 0)
int no_pages = 0;       /*A counter of how many pages there is in a snapshot*/
struct utsname OS;
char lock = NO_LOCK;
char err_msg[100];
CMD cmd[6];    

int backplane_index = 0;

