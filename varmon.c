/* varmon.c VA RAID Monitor (VARMon)
    
Linux RAID Monitor for Mylex DAC960/1100 RAID controllers

Copyright 1999 by VA Linux Systems
Original author Dragan Stancevic <visitor@valinux.com>
Modification by Julien Danjou <julien@danjou.info>
    
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
 
#include <errno.h>
#include <ctype.h>
#include <ncurses.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <scsi/scsi.h>
#include "DAC960.h"
#include "varmon.h"

/*
-------------------------------------------------------------------------------
Silently log the actions.
*/
int debugs(char *msg){
	FILE *log;

	log = fopen(".varmon.log", "a");
	if(!log){
		/*Exit silently*/
		return 0;
	}
	fputs(msg, log);
	fclose(log);
	return 1;
}
 
int debugi(int msg){
	FILE *log;
		 
	log = fopen(".varmon.log", "a");
	if(!log){
		/*Exit silently*/
		return 0;
	}
	fprintf(log, "[%d]", msg);
	fclose(log);
	return 1;
}  

// V2 support
#define BYTE_0(x)     (unsigned char)(x & 0xFF)
#define BYTE_1(x)     (unsigned char)((x >> 8) & 0xFF)
static int v2_SCSI_cmd(int fd, int cdb_s, unsigned char *cdb, int c, int t, 
  int C, int data_s, unsigned char * data, int sense_s, unsigned char *sense) 
{
	int rc;
#define v2_scsi_cmd	v2_user_cmd.CommandMailbox.SCSI_10

	DAC960_V2_UserCommand_T	v2_user_cmd ;
	memset((char *)&v2_user_cmd, 0, sizeof(v2_user_cmd));
        memcpy(v2_scsi_cmd.SCSI_CDB, cdb, cdb_s);
	v2_scsi_cmd.CommandOpcode	= DAC960_V2_SCSI_10_Passthru;
	v2_scsi_cmd.CommandTimeout.TimeoutValue = 
		DAC960_V1_DCDB_Timeout_10_seconds;
	v2_scsi_cmd.DataTransferSize	= (data_s > 0 ? data_s : -data_s);
	v2_scsi_cmd.PhysicalDevice.TargetID	= t;
	v2_scsi_cmd.PhysicalDevice.Channel	= c;
	v2_scsi_cmd.PhysicalDevice.Controller	= C;
	v2_scsi_cmd.CDBLength		= cdb_s;
	v2_user_cmd.DataTransferLength	= data_s;
	v2_user_cmd.DataTransferBuffer	= data;
	v2_user_cmd.RequestSenseLength	= sense_s;
	v2_user_cmd.RequestSenseBuffer	= sense;
	rc = ioctl(fd,DAC960_IOCTL_V2_EXECUTE_COMMAND,&v2_user_cmd);
	return rc;
}

// End V2 support

/* Send the command to the DAC driver. */
static int snd_cmd(int bus_h, DAC960_V1_DCDB_T *dcdb, void *tr_data, int tr_len, int controller){
	int status = 0;
	DAC960_V1_UserCommand_T usr_cmd;

	memset(&usr_cmd, 0, sizeof(usr_cmd));

	usr_cmd.ControllerNumber = controller;
	usr_cmd.CommandMailbox.Type3.CommandOpcode = DAC960_V1_DCDB;
	usr_cmd.DataTransferLength = tr_len;
	usr_cmd.DataTransferBuffer = tr_data;
	usr_cmd.DCDB = dcdb;

	status = ioctl(bus_h, DAC960_IOCTL_V1_EXECUTE_COMMAND, &usr_cmd);
	if (status == -1) {
		perror("\nFailed sending command to DAC driver");
		exit(2);
	}
	return(status);
}                              

/* Get the information from the backplane. */
int get_backplane_info(int what, SAFTE_T *safte, int controller, int cur_bp){
	int bus_h, status, tr_len = 64, i;
	int channel = safte->bp[cur_bp].channel;
	int id  = safte->bp[cur_bp].id;


	static int fans = 0, power_s = 0, slots = 0, temp_s = 0;
	unsigned char *p;

	char bus_n[] = "/dev/rd/c0d0";
	char bus_n_devfs[] = "/dev/rd/disc0/disc";
	char error_m[80];
	unsigned char tr_data[1024];
	DAC960_V1_DCDB_T dcdb;
	


	if((bus_h = open(bus_n, O_RDWR|O_NONBLOCK)) == -1)
	{
	  if((bus_h = open(bus_n_devfs, O_RDWR|O_NONBLOCK)) == -1)
	  {
		sprintf(error_m, "\nFailed to open %s", bus_n);
		perror(error_m);
		exit(1);
	  }
	}
	
	memset(tr_data, 0, 1024);
	memset(&dcdb, 0, sizeof(DAC960_V1_DCDB_T));
	
	switch(what){
	  case DEVICES:
	    if (IS_FIRMWARE_LEVEL_2(controller)) {
	      unsigned char cdb[10] = 
	      {SCSI_READ_BUFFER,0x1,0x0,0x0,0x0,0x0,0x0,
	       BYTE_1(tr_len),BYTE_0(tr_len),0x0};
	      status = v2_SCSI_cmd(bus_h, sizeof(cdb), cdb, channel, id, 
                           controller, tr_len, tr_data, 0, NULL); 
	    }
	    else {
	      dcdb.Channel = channel;
	      dcdb.TargetID = id;
	      dcdb.Direction = DAC960_V1_DCDB_DataTransferDeviceToSystem;
	      dcdb.EarlyStatus = false;
	      dcdb.Timeout = DAC960_V1_DCDB_Timeout_10_seconds;
	      dcdb.NoAutomaticRequestSense = false;
	      dcdb.DisconnectPermitted = true;
	      dcdb.TransferLength = tr_len;
	      dcdb.CDBLength =10;
	      dcdb.TransferLengthHigh4 =0;
	      dcdb.SenseLength = sizeof(dcdb.SenseData);
	      dcdb.CDB[0] = SCSI_READ_BUFFER;
	      dcdb.CDB[1] = 0x1; /* Mode is SAF-TE format */
	      dcdb.CDB[2] = 0x0; /* Read enclosure config */
	      dcdb.CDB[3] = 0x0;
	      dcdb.CDB[4] = 0x0;
	      dcdb.CDB[5] = 0x0;
	      dcdb.CDB[6] = 0x0;
	      dcdb.CDB[7] = (tr_len & 0xFF00) >> 8;
	      dcdb.CDB[8] = (tr_len & 0xFF);
	      dcdb.CDB[9] = 0x0;
 
	      status = snd_cmd(bus_h, &dcdb, tr_data, tr_len, controller);
	    }
	    fans = (int) tr_data[0];
	    power_s   = (int) tr_data[1];
	    slots = (int) tr_data[2];
	    temp_s = (int) tr_data[4];
	  break;
			
	  case TEMPERATURE:
	    tr_len = fans + power_s + slots + temp_s + 5;
	    if (IS_FIRMWARE_LEVEL_2(controller)) {
	      unsigned char cdb[10] = 
	      {SCSI_READ_BUFFER,0x1,0x1,0x0,0x0,0x0,0x0,
	       BYTE_1(tr_len),BYTE_0(tr_len),0x0};
	      status = v2_SCSI_cmd(bus_h, sizeof(cdb), cdb, channel, id, 
                           controller, tr_len, tr_data, 0, NULL);
	    }
	    else {
	      dcdb.Channel = channel;
	      dcdb.TargetID = id;
	      dcdb.Direction = DAC960_V1_DCDB_DataTransferDeviceToSystem;
	      dcdb.EarlyStatus = false;
	      dcdb.Timeout = DAC960_V1_DCDB_Timeout_10_seconds;
	      dcdb.NoAutomaticRequestSense = false;
	      dcdb.DisconnectPermitted = true;
	      dcdb.TransferLength = tr_len;
	      dcdb.CDBLength = 10;
	      dcdb.TransferLengthHigh4 = 0;
	      dcdb.SenseLength = sizeof(dcdb.SenseData);
	      dcdb.CDB[0] = SCSI_READ_BUFFER;
	      dcdb.CDB[1] = 0x1; /* Mode is SAF-TE format */
	      dcdb.CDB[2] = 0x1; /* Read enclosure config */
	      dcdb.CDB[3] = 0x0;
	      dcdb.CDB[4] = 0x0;
	      dcdb.CDB[5] = 0x0;
	      dcdb.CDB[6] = 0x0;
	      dcdb.CDB[7] = (tr_len & 0xFF00) >> 8;
	      dcdb.CDB[8] = (tr_len & 0xFF);
	      dcdb.CDB[9] = 0x0;
   
	      status = snd_cmd(bus_h, &dcdb, tr_data, tr_len, controller);
	    }
		
		p = tr_data;
		for (i=0; i < fans; i++){
			/*intf("Fan[%d] status",i);*/
				if (*p == 0x0)
					strcpy(safte->bp[cur_bp].fan[i], "ON");
					/*printf("operational\n");*/
				else if (*p == 0x01)
					strcpy(safte->bp[cur_bp].fan[i], "OFF");
					/*printf("malfunction\n");*/
				else if (*p == 0x02)
					strcpy(safte->bp[cur_bp].fan[i], "NONE");
					/*printf("not installed\n");*/
/*				else if (*p == 0x80)
					printf("status not reportable\n");
				else
					printf("broken\n");*/
				p++;
			}

			 for (i=0; i < power_s; i++){
				/*printf("Power Supply[%d] status",i);*/
				if (*p == 0x0)
					strcpy(safte->bp[cur_bp].ps[i], "ON");
					/*printf("operational and on\n");*/
				else if (*p == 0x01)
					strcpy(safte->bp[cur_bp].ps[i], "OFF");
					/*printf("operational and off\n");*/
				else if (*p == 0x10)
					strcpy(safte->bp[cur_bp].ps[i], "ERR");
					/*printf("malfunction and commanded on\n");*/
				else if (*p == 0x11)
					strcpy(safte->bp[cur_bp].ps[i], "ERR");
					/*printf("malfunction and commanded off\n");*/
				else if (*p == 0x20)
					strcpy(safte->bp[cur_bp].ps[i], "NONE");
					/*printf("not installed\n");*/
/*				else if (*p == 0x21)
					printf("installed\n");
				else if (*p == 0x80)
					printf("status not reportable\n");
				else
					printf("broken\n");*/
				p++;
			}      
/*
			for (i=0; i < slots; i++){
				printf("Device slot %d SCSI-ID -=> %d\n",i, (char )*p);
				p++;
			}*/
			p += (slots + 1 +1);
/*			printf("Door lock status -=> ");
			if (*p == 0x0)
				printf("locked\n");
			else if (*p == 0x01)
				printf("unlocked / no door lock\n");
			else if (*p == 0x80)
				printf("status not reportable\n");
			else
				printf("broken\n");
			p++;*/
/*
			printf("Speaker status -=> ");
			if (*p == 0x0)
				printf("Speaker off / no speaker installed\n");
			else if (*p == 0x01)
				printf("Speaker on\n");
			else
				printf("broken\n");
			p++;*/
			/*p += fans + slots + power_s + 1 + 1;*/
			for (i=0; i < temp_s; i++){
				if(safte->bp[cur_bp].got_nstor)
					sprintf(safte->bp[cur_bp].temp[i], "%d",
							(int)((float)((float)5/(float)9)*(*p-42)));
				else
					sprintf(safte->bp[cur_bp].temp[i], "%d", *p);
				p++;
			}
/*			{
			unsigned char   bitcnt;
			unsigned char   bitmask;
			unsigned char   tmp;

			if (*p & 0x80){
				printf("Enclosure Temperature Alert -=> [");
				for (bitcnt =0;bitcnt < 7; bitcnt++){
					bitmask = 0x1 << bitcnt;
					tmp = ((*p & bitmask) >> bitcnt);
					if (tmp == 0x1)
						printf("TEMP t%d\n",(8+bitcnt));
				}
				p++;
				for (bitcnt =0;bitcnt < 8; bitcnt++){
					bitmask = 0x1 << bitcnt;
					tmp = ((*p & bitmask) >> bitcnt);
					if (tmp == 0x1)
						printf("t%d ",bitcnt);
				}
				printf("]\n");
				p++;
			}
			else {
				printf("Enclosure Temperatures normal\n");
				p++;
				p++;
			}
			} 	*/
		break;
		}
	close(bus_h);
return 1;
}

/*------------------------------------------------------------------*/
struct {
	int controller_number;
	int channel;
	int target;
} backplane[32];

static int inquiry (int fd, int channel, int target,  DAC960_SCSI_Inquiry_T *inqptr, int controller){
	DAC960_V1_DCDB_T DCDB;
	int status;

	if (IS_FIRMWARE_LEVEL_2(controller)) {
		unsigned char cdb[6] = {0x12,0x0,0x0,0x0,sizeof(*inqptr),0x0};
		return v2_SCSI_cmd(fd, 6, cdb, channel, target, 
		controller, sizeof(*inqptr), (unsigned char *)inqptr, 0, NULL);
	}

	memset(&DCDB, 0, sizeof(DCDB));

	DCDB.Channel = channel;
	DCDB.TargetID = target;
	DCDB.Direction = DAC960_V1_DCDB_DataTransferDeviceToSystem;
	DCDB.EarlyStatus = false;
	DCDB.Timeout = DAC960_V1_DCDB_Timeout_10_seconds;
	DCDB.NoAutomaticRequestSense = false;
	DCDB.DisconnectPermitted = true;
	DCDB.TransferLength = sizeof(DAC960_SCSI_Inquiry_T);
	DCDB.CDBLength =6;
	DCDB.TransferLengthHigh4 =0;
	DCDB.SenseLength = sizeof(DCDB.SenseData);
	DCDB.CDB[0] = 0x12;
	DCDB.CDB[1] = 0x0;
	DCDB.CDB[2] = 0x0;
	DCDB.CDB[3] = 0x0;
	DCDB.CDB[4] = sizeof(DAC960_SCSI_Inquiry_T);
	DCDB.CDB[5] = 0x0;

	status = snd_cmd(fd, &DCDB, inqptr, sizeof(DAC960_SCSI_Inquiry_T), controller);
	return(status);
}

static int tur(int fd, int channel, int target, int controller){
	DAC960_V1_DCDB_T DCDB;
	int status;

	if (IS_FIRMWARE_LEVEL_2(controller)) {
		unsigned char cdb[6] = {0x0,0x0,0x0,0x0,0x0,0x0};
		return v2_SCSI_cmd(fd, 6, cdb, channel, target, 
			controller, 0, NULL, 0, NULL);
	}

	memset(&DCDB, 0, sizeof(DCDB));

	DCDB.Channel = channel;
	DCDB.TargetID = target;
	DCDB.Direction = DAC960_V1_DCDB_NoDataTransfer;
	DCDB.EarlyStatus = false;
	DCDB.Timeout = DAC960_V1_DCDB_Timeout_10_seconds;
	DCDB.NoAutomaticRequestSense = false;
	DCDB.DisconnectPermitted = true;
	DCDB.TransferLength = 0;
	DCDB.CDBLength =6;
	DCDB.TransferLengthHigh4 =0;
	DCDB.SenseLength = sizeof(DCDB.SenseData);
	DCDB.CDB[0] = TEST_UNIT_READY;
	DCDB.CDB[1] = 0x0;
	DCDB.CDB[2] = 0x0;
	DCDB.CDB[3] = 0x0;
	DCDB.CDB[4] = 0x0;
	DCDB.CDB[5] = 0x0;

	status = snd_cmd(fd, &DCDB, 0, 0, controller);
	return(status);
}
	
int detect_backplane(){
	int fd;
	DAC960_SCSI_Inquiry_T inq;
	int num_controllers;
	int controller_number;
	struct utsname ut;

	uname(&ut);
	
	/* If running 2.6 kernel */
	if(!strncmp("2.6", ut.release, 3))
	{
	  if((fd = open("/dev/dac960_gam", O_RDWR | O_NONBLOCK)) < 0)
	  {
		printf("Unable to open device (%m)\n");
		exit(1);
	  }
	}
	/* 2.4 kernel */
	else
	{
	  if((fd = open("/dev/rd/c0d0", O_RDWR | O_NONBLOCK))<0)
	  {
		/* devfs ? */
		if((fd = open("/dev/rd/disc0/disc", O_RDWR | O_NONBLOCK))<0)
		{
		  printf("Unable to open device (%m)\n");
		  exit(1);
		}
	  }
	}
	
	num_controllers = ioctl(fd, DAC960_IOCTL_GET_CONTROLLER_COUNT, 0);

	if(num_controllers <0){
		printf("get_controller_count ioctl() failed (%m)\n");
		exit(-1);
	}
	printf("\n");	
/*	printf("Scanning all channels for devices:\n");*/
	for(controller_number =0; controller_number < num_controllers; controller_number++){
		DAC960_ControllerInfo_T info;
	    int channel, target;

		info.ControllerNumber = controller_number;
		
		if(ioctl(fd, DAC960_IOCTL_GET_CONTROLLER_INFO, &info) <0){
			printf("Unable to get controller info (%m)\n");
			exit(-1);
		}
/*V2 support*/	snap_all[controller_number].FirmwareType = info.FirmwareType;  
		printf ("DAC960: Ctrlr %d, PCI %02x:%02x:%02x, IRQ %d, "
		"Channels %d\n", info.ControllerNumber, info.PCI_Bus, 
		info.PCI_Device, info.PCI_Function, info.IRQ_Channel, 
		info.Channels);
		printf ("DAC960: Model %s, Firmware %s\n", info.ModelName, 
		info.FirmwareVersion);
    	for(channel =0; channel < info.Channels; channel++)
			for(target =0; target < DAC960_V1_MaxTargets; target++){
				int status;
				
				printf("\rScanning Controller[%d], Channel[%d], ID[%d]  ", controller_number,
						channel, target);
				status = tur(fd, channel, target, controller_number);
#ifdef I_THOUGH_STATUS_0_MEANS_TUR_OK
				if (status != 0x0e){
#else
				if (status == 0){
#endif
					fflush(stdout);
					status = inquiry(fd, channel, target, &inq, controller_number);
					if ((!strncmp(inq.VendorIdentification,"QLogic",6)) ||
						(!strncmp(inq.VendorIdentification,"VA Linux",8))){
						int k;
						
						k = snap_all[controller_number].safte.no_bp;
						
						if(!strncmp(inq.VendorIdentification,"VA LinuxnexStor",15))
							snap_all[controller_number].safte.bp[k].got_nstor = 1;
						else
							snap_all[controller_number].safte.bp[k].got_nstor = 0;


						snap_all[controller_number].got_safte = 1;
						snap_all[controller_number].safte.bp[k].channel = channel;
						snap_all[controller_number].safte.bp[k].id = target;
						snap_all[controller_number].safte.no_bp++;

					}
				}
				}
	}
	close(fd);
	return 1;
}

int msg_box(char *cmd, int buttons){

	WINDOW *msgw;
	char msg1[] = "Are you sure you want to ";
	char msg_line[200];
	char msg_tmp[200];
	char *begin;
	char *delimit;
	int msg_len;

	int response = KEY_RIGHT, key;
	int dummy;
	
	strcpy(msg_tmp, msg1);
	
	if(buttons & B_OK){
		strcpy(msg_tmp, cmd);
		strcpy(msg_line, msg_tmp);
	}else if(buttons & B_NONE){
		strcpy(msg_tmp, cmd);
		strcpy(msg_line, msg_tmp);
	}else if((delimit = index(cmd, ' '))){
		begin = cmd;
		if((strstr(cmd, "check-consistency"))){
			strncat(msg_tmp, cmd, (delimit-begin));
			strcat(msg_tmp, " of RAID array");
			strcat(msg_tmp, delimit);
		}else {
			strncat(msg_tmp, cmd, (delimit-begin));
			strcat(msg_tmp, " device");
			strcat(msg_tmp, delimit);
		}
		strcat(msg_tmp, "?");
		strcpy(msg_line, msg_tmp);
	} else {
		strcat(msg_tmp, "cancel any rebuild or consistency check in progress?");
		strcpy(msg_line, msg_tmp);
	} 

	msgw = subwin(stdscr, 6, 45, ((LINES-5)/2), 18);

	if((buttons & B_YES_NO)){
		wattrset(msgw, A_BOLD|COLOR_PAIR(RED_YELLOW));
		wmove(msgw,0,0);
		for(dummy = 0; dummy <= ((msgw->_maxx+1)*(msgw->_maxy+1)) ; dummy++)
			waddch(msgw,' ');
		box(msgw,0,0);
		
		wattrset(msgw, A_DIM|COLOR_PAIR(BLUE_WHITE));
		mvwaddstr(msgw, (msgw->_maxy - 1), ((msgw->_maxx/2)-(strlen(" YES"))), " YES ");
		wattrset(msgw, A_BOLD|COLOR_PAIR(BLUE_WHITE));
		mvwaddstr(msgw, (msgw->_maxy - 1), ((msgw->_maxx/2)+(strlen("NO "))), " NO ");
		wattrset(msgw, A_BOLD|COLOR_PAIR(RED_YELLOW));

		
	}else if ((buttons & B_OK)){
		
		if(buttons & B_ERR)
			wattrset(msgw, A_BOLD|COLOR_PAIR(RED_YELLOW));
		else
			wattrset(msgw, A_BOLD|COLOR_PAIR(CYAN_YELLOW));
		
		wmove(msgw,0,0);
		for(dummy = 0; dummy <= ((msgw->_maxx+1)*(msgw->_maxy+1)) ; dummy++)
			waddch(msgw,' ');
		box(msgw,0,0);
			
		if(buttons & B_ERR)
			wattrset(msgw, A_BOLD|COLOR_PAIR(BLUE_WHITE));
		else
			wattrset(msgw, A_BOLD|COLOR_PAIR(BLUE_YELLOW));

		mvwaddstr(msgw, (msgw->_maxy - 1), ((msgw->_maxx/2)-((strlen(" OK ")/2))), " OK ");
		
		if(buttons & B_ERR)
			wattrset(msgw, A_BOLD|COLOR_PAIR(RED_YELLOW));
		else
			wattrset(msgw, A_BOLD|COLOR_PAIR(CYAN_YELLOW));

	}else if ((buttons & B_NONE)){
		wattrset(msgw, A_BOLD|COLOR_PAIR(CYAN_YELLOW));
		wmove(msgw,0,0);
		for(dummy = 0; dummy <= ((msgw->_maxx+1)*(msgw->_maxy+1)) ; dummy++)
			waddch(msgw,' ');
		box(msgw,0,0);
	}

	msg_len = strlen(msg_tmp);
	if(msg_len > 43){
		begin = msg_tmp;
		for(dummy = 1; msg_len; dummy++){
			strncpy(msg_line, begin, 43);
			msg_line[42] = '\0';
			if(strlen(msg_line) > 41){
				delimit = rindex(msg_line, ' ');
			} else delimit = 0;
			if(delimit){
				*delimit = '\0';
				begin += (delimit-msg_line);
				begin++;
				mvwaddstr(msgw, dummy, ((msgw->_maxx/2)-((strlen(msg_line)/2))), msg_line);
			}else{
				mvwaddstr(msgw, dummy, ((msgw->_maxx/2)-((strlen(msg_line)/2))), msg_line);
				break;
			}
			msg_len = strlen(begin);
		}
	}else{
		mvwaddstr(msgw, 1, ((msgw->_maxx/2)-((strlen(msg_line)/2))), msg_line);
	
	}

	clearok(stdscr, TRUE);
	refresh();
	clearok(stdscr, FALSE);
	
	wrefresh(msgw);
	refresh();
	
	if ((buttons & B_NONE)){
		delwin(msgw);
		return 0;
	}
	while ((key = wgetch(stdscr))){
		if(key == KEY_ESCPE){
			response = key;
			break;
		}
		if((key == KEY_RETURN) || (key == KEY_RETURN1)){
			break;
		}
		if(key == KEY_LEFT){
			wattrset(msgw, A_BOLD|COLOR_PAIR(BLUE_WHITE));
			mvwaddstr(msgw, (msgw->_maxy - 1), ((msgw->_maxx/2)-(strlen(" YES"))), " YES ");
			wattrset(msgw, A_DIM|COLOR_PAIR(BLUE_WHITE));
			mvwaddstr(msgw, (msgw->_maxy - 1), ((msgw->_maxx/2)+(strlen("NO "))), " NO ");
			wattrset(msgw, A_BOLD|COLOR_PAIR(RED_YELLOW));
			response = KEY_LEFT;
		}else if(key == KEY_RIGHT){
			wattrset(msgw, A_BOLD|COLOR_PAIR(BLUE_WHITE));
			mvwaddstr(msgw, (msgw->_maxy - 1), ((msgw->_maxx/2)+(strlen("NO "))), " NO ");
			wattrset(msgw, A_DIM|COLOR_PAIR(BLUE_WHITE));
			mvwaddstr(msgw, (msgw->_maxy - 1), ((msgw->_maxx/2)-(strlen(" YES"))), " YES ");
			wattrset(msgw, A_BOLD|COLOR_PAIR(RED_YELLOW));
			response = KEY_RIGHT;
		}
		wrefresh(msgw);
		refresh();
	}
	clearok(stdscr, TRUE);
	delwin(msgw);
	refresh();
	clearok(stdscr, FALSE);

	return response;
}

/*
-------------------------------------------------------------------------------
Execute commads.
*/
int command(char *target, char *msg){
	FILE *targeth;
	int run_cmd = 0;
	char buffer[200];
	char buffer_old[200];
	char seconds = 1;
		
	run_cmd = msg_box(msg, B_YES_NO);
	switch(run_cmd){
		case KEY_ESCPE:
		case KEY_RIGHT:
			run_cmd = msg_box("Operation canceled.", B_OK);
			return 0;
		break;
		
		case KEY_LEFT:
			run_cmd = msg_box("Operation in progress, this might"
					" take a few moments. Please wait...", B_NONE);

			targeth = fopen(target, "r+");
			if(!target){
				run_cmd = msg_box("Error communicating to the DAC driver!",
					   	B_OK|B_ERR);
				return 0;
			}
			
			/* It's trick to make the driver reset the user command */
			fputs("Run varmon -b", targeth);

			rewind(targeth);
			fgets(buffer_old, 200, targeth);
			rewind(targeth);
			fputs(msg, targeth);
			rewind(targeth);
			while(seconds){
				fgets(buffer, 200, targeth);
				rewind(targeth);

				if(strcmp(buffer_old, buffer)){
					*rindex(buffer, '\n') = 0;
					run_cmd = msg_box(buffer, B_OK);
					break;
				}
				
				if (seconds > 10){
					run_cmd = msg_box("Error communicating to the DAC"
							" driver! Giving up...", (B_OK|B_ERR));
						break;
				}
				sleep(1);
				seconds++;
			}

			fclose(targeth);
		break;
		
		default:
			return 0;
	
	}
	return 1;
}

/*
-------------------------------------------------------------------------------
Parse the target into lines for the parse_line,
I know I know, there is a function called fgets,
but there was a bug in one version of the
driver concerning sequential reads from proc
which would fail in reading all the information
so we try to have support for that version of the
driver too.
*/
int extract_data(char **buffer,char *file){

	int fileID, read_size, max_alow = MAX_READ;/*, to;*/
	char *data;

	cycle:

	fileID = open(file, O_RDONLY);
	if (fileID == -1){
		debugs("\nFailed to open file:");
		debugs(file);
		return 0;
	}

	data = calloc(max_alow, sizeof(char));
	while((read_size = read(fileID, data, max_alow))){
		if(read_size == max_alow){
			close(fileID);
			max_alow *= 2;
/*			Not enough stack, resizing.*/
			free(data);
			goto cycle;       

		}
	}

/*	NOTE Instead of freing the data we just pass it on
	to the function we got called within, let them worry
	about it.
*/
	*buffer = data;
/*	Passing on the stack.*/

	close(fileID);

	return 1;
}

/*
-------------------------------------------------------------------------------
Parse the data into single lines, one at the time.
Preparation to parse all the data into the data structure.
*/
int get_line(char *line, char *stat_path, int reset_flag){
	char **datap, *data = "Null";
	static char *offset;
	static char old_path[100];
	static int offsetc = 0;
	static int got_data = 0;
	static int e_of = 0;
	int linec;
/*	Init pointer to a pointer to a char*/
	datap = &data;

/*	If reset of the virtual lseek is
	required set the end of file flag*/
	if(!reset_flag) e_of = 1;

/*	Flag, an error has ocured*/

/*	Is the end of file flag up?
 	if so set it to ! end of file
 	and return 0*/	
	if(e_of){
		e_of = 0;
		offsetc = 0;
		return 0;
	}
	
	if(!strstr(stat_path, old_path)){
		got_data = 0;
	}

	if(!got_data){
	/*		Flag, an error has ocured*/
		if(!extract_data(datap, stat_path)){
		       	return 2;
		}
		got_data = 1;
		offset = *datap;
		offsetc = 0;
		strcpy(old_path, stat_path);

	}

	for(linec = 0; offset[offsetc] != '\n'; offsetc++, linec++)
		line[linec] = offset[offsetc];

	line[linec] = '\0';

	if(offset[(offsetc+1)] == '\0'){
/*
		Freeing the inherited stack.
*/
		free(offset);
		offsetc = 0;
		got_data = 0;
		e_of = 1;
       /*		return 0;*/
	}else{
	
		offsetc++;
	
	}
	
	return 1;
}


/*
-------------------------------------------------------------------------------
Parse the lines into data structure
This function parses the information from the stream directly
*/

int get_stat(SWINDOWS *all_win, CARD *card_pool, TAB *tab_pool){
	int cur_card, maxx, maxy, del_yy = 2, del_xx = 1;
	
	maxy = all_win->status_win->_maxy;
	maxx = all_win->status_win->_maxx;

/*		Repaint our screen
*/
	wattrset(all_win->status_win, A_BOLD|COLOR_PAIR(BLUE_WHITE));
	wmove(all_win->status_win, del_yy, del_xx);
	for(;del_yy < (maxy+1) ; del_yy++){
		for(del_xx = 1; del_xx < maxx; del_xx++)
			waddch(all_win->status_win, ' ');
		del_xx = 1;
		wmove(all_win->status_win, del_yy, del_xx);
	}

/*	Walk trough the pages and get data from it, after that
	flush it on the screen.
*/
	for(cur_card = 0; cur_card < no_pages; cur_card++){

/*		Does this card have focus?
*/
		if(card_pool[cur_card].focus){

/*			Does this tab have focus?
*/
			if(tab_pool[0].focus){
				
/*				Display all the cyan characters
*/   
				wattrset(all_win->status_win,
				A_BOLD|COLOR_PAIR(BLUE_CYAN));

				mvwaddstr(all_win->status_win,
				2, 2, "Card:");
				
				mvwaddstr(all_win->status_win,
				3, 2, "Channels:");
				
				mvwaddstr(all_win->status_win,
				4, 2, "Memory:");
				
				mvwaddstr(all_win->status_win,
				5, 2, "Firmware:");
				
				mvwaddstr(all_win->status_win,
				maxy-4, 2, "Author:");
				
				mvwaddstr(all_win->status_win,
				maxy-3, 2, "Ver.:");
				
				mvwaddstr(all_win->status_win,
				maxy-2, 2, "Kernel:");

				mvwaddstr(all_win->status_win,
				maxy-1, 2, "Host:");

				mvwaddstr(all_win->status_win,
				7, 2, "Controller Q Depth:");
				
				mvwaddstr(all_win->status_win,
				8, 2, "Driver Q Depth:");
				
				mvwaddstr(all_win->status_win,
				9, 2, "Stripe Size:");
				
				mvwaddstr(all_win->status_win,
				10, 2, "Segment Size:");
				
				mvwaddstr(all_win->status_win,
				11, 2, "Max Block/Command:");
				
				mvwaddstr(all_win->status_win,
				12, 2, "Max Scatter/Gather:");
				
				mvwaddstr(all_win->status_win,
				13, 2, "BIOS Geometry:");
				
				mvwaddstr(all_win->status_win,
				14, 2, "SAF-TE:");

				mvwaddstr(all_win->status_win,
				2, 30, "PCI Bus:");
				
				mvwaddstr(all_win->status_win,
				3, 30, "Device:");
				
				mvwaddstr(all_win->status_win,
				4, 30, "Function:");
				
				mvwaddstr(all_win->status_win,
				5, 30, "IRQ:");

				/*mvwaddstr(all_win->status_win,
				6, 30, "I/O Address:");*/

				mvwaddstr(all_win->status_win,
				6, 30, "PCI Address:");
				
				mvwaddstr(all_win->status_win,
				7, 30, "Mapped to:");
				
				if(snap_all[cur_card].got_safte){
					int a,b,d;
					char c[4];

					for(a = 0, d = 9; a < snap_all[cur_card].safte.no_bp; a++){
						sprintf(c, "C%d", snap_all[cur_card].safte.bp[a].channel);
						for(b = 0; b < 3; b++){
							mvwaddstr(all_win->status_win, (b+d), 28, c);
							switch(b){
								case 0:
									mvwaddstr(all_win->status_win, (b+d),
										   	31, "Fan0:");
									mvwaddstr(all_win->status_win, (b+d),
										   	42, "Fan1:");
									mvwaddstr(all_win->status_win, (b+d),
										   	53, "Fan2:");
								break;
							
								case 1:
									mvwaddstr(all_win->status_win, (b+d),
										   	31, "Temp0:    C");
									mvwaddch(all_win->status_win, (b+d),
										   	40, ACS_DEGREE);
									mvwaddstr(all_win->status_win, (b+d),
										   	46, "Temp1:    C");
									mvwaddch(all_win->status_win, (b+d),
										   	55, ACS_DEGREE);
								break;	
								
								case 2:
									mvwaddstr(all_win->status_win, (b+d),
										   	31, "Power0:");
									mvwaddstr(all_win->status_win, (b+d),
										   	46, "Power1:");
								break;
							
							}
						}
						d += b;
						d++;
					}
					
				}

/*				Display all the yellow characters
*/   
				wattrset(all_win->status_win,
				A_BOLD|COLOR_PAIR(BLUE_YELLOW));

				mvwaddstr(all_win->status_win,
				2, 12, snap_all[cur_card].c_type);
				
				mvwaddstr(all_win->status_win,
				3, 12, snap_all[cur_card].chnl);
				
				mvwaddstr(all_win->status_win,
				4, 12, snap_all[cur_card].mem);
				
				mvwaddstr(all_win->status_win,
				5, 12, snap_all[cur_card].frmw);
				
				mvwaddstr(all_win->status_win,
				maxy-4, 9, snap_all[cur_card].dr_au);

				mvwaddstr(all_win->status_win,
				maxy-3, 9, snap_all[cur_card].dr_ver);
				
				mvwaddnstr(all_win->status_win,
				maxy-2, 9, OS.release, 15);
				
				mvwaddnstr(all_win->status_win,
				maxy-1,9, OS.nodename, 15);

				mvwaddstr(all_win->status_win,
				7, 21, snap_all[cur_card].cq_depth);
				
				mvwaddstr(all_win->status_win,
				8, 21, snap_all[cur_card].dq_depth);
				
				mvwaddstr(all_win->status_win,
				9, 21, snap_all[cur_card].stripe_size);
				
				mvwaddstr(all_win->status_win,
				10, 21, snap_all[cur_card].seg_size);
				
				mvwaddstr(all_win->status_win,
				11, 21, snap_all[cur_card].max_block_com);
				
				mvwaddstr(all_win->status_win,
				12, 21, snap_all[cur_card].max_sg_seg);
				
				mvwaddstr(all_win->status_win,
				13, 21, snap_all[cur_card].bios_geo);
				
				if(snap_all[cur_card].fault_mngmt)
					mvwaddstr(all_win->status_win,
					14, 21, "ON");
				else
					mvwaddstr(all_win->status_win,
					14, 21, "OFF");

				mvwaddstr(all_win->status_win,
				2, 49, snap_all[cur_card].pci_bus);
				
				mvwaddstr(all_win->status_win,
				3, 49, snap_all[cur_card].device);

				mvwaddstr(all_win->status_win,
				4, 49, snap_all[cur_card].function);
				
				mvwaddstr(all_win->status_win,
				5, 49, snap_all[cur_card].irq);
				
				/*mvwaddstr(all_win->status_win,
				6, 49, snap_all[cur_card].io_addr);*/
				
				mvwaddstr(all_win->status_win,
				6, 49, snap_all[cur_card].pci_addr);
				
				mvwaddstr(all_win->status_win,
				7, 49, snap_all[cur_card].pci_mapped);

/*				mvwaddstr(all_win->status_win,
				17, 47, OS.sysname);

				mvwaddstr(all_win->status_win,
				17, 53, OS.machine);

				mvwaddnstr(all_win->status_win,
				18,47, OS.nodename, 15);

				mvwaddnstr(all_win->status_win,
				19, 47, OS.release, 15);*/
			
			
				if(snap_all[cur_card].got_safte){
					int a,b,d;
					char c[4];

					for(a = 0, d = 9; a < snap_all[cur_card].safte.no_bp; a++){
						sprintf(c, "C%d", snap_all[cur_card].safte.bp[a].channel);
						for(b = 0; b < 3; b++){
							switch(b){
								case 0:
									mvwaddstr(all_win->status_win, (b+d),
										   	36, (strlen(snap_all[cur_card].safte.bp[a].fan[0])?
										   	snap_all[cur_card].safte.bp[a].fan[0] : "N/A"));

									mvwaddstr(all_win->status_win, (b+d),
										   	47, (strlen(snap_all[cur_card].safte.bp[a].fan[1])?
										   	snap_all[cur_card].safte.bp[a].fan[1] : "N/A"));
									
									mvwaddstr(all_win->status_win, (b+d),
										   	58, (strlen(snap_all[cur_card].safte.bp[a].fan[2])?
										   	snap_all[cur_card].safte.bp[a].fan[2] : "N/A"));
								break;

								case 1:
									mvwaddstr(all_win->status_win, (b+d),
										   	37, (strlen(snap_all[cur_card].safte.bp[a].temp[0])?
										   	snap_all[cur_card].safte.bp[a].temp[0] : "N/A"));

									mvwaddstr(all_win->status_win, (b+d),
										   	52, (strlen(snap_all[cur_card].safte.bp[a].temp[1])?
										   	snap_all[cur_card].safte.bp[a].temp[1] : "N/A"));
								break;

								case 2:
									mvwaddstr(all_win->status_win, (b+d),
										   	38, (strlen(snap_all[cur_card].safte.bp[a].ps[0])?
										   	snap_all[cur_card].safte.bp[a].ps[0] : "N/A"));

									mvwaddstr(all_win->status_win, (b+d),
										   	53, (strlen(snap_all[cur_card].safte.bp[a].ps[1])?
										   	snap_all[cur_card].safte.bp[a].ps[1] : "N/A"));
								break;
							}
						}
						d += b;
						d++;
					}
																			  
				}

/*				Display all the white characters
*/   
				wattrset(all_win->status_win,
				A_BOLD|COLOR_PAIR(BLUE_WHITE));

				/*mvwaddch(all_win->status_win,
				2, 27, ACS_ULCORNER);*/

				mvwaddch(all_win->status_win,
				6, 27, ACS_URCORNER);
				
				/*whline(all_win->status_win, ACS_HLINE, maxx-28);*/
				
				/*waddstr(all_win->status_win,
				"System Settings");*/
				
				/*mvwaddch(all_win->status_win,
				2, maxx, ACS_RTEE);*/
				
				mvwvline(all_win->status_win, 7, 27,
				ACS_VLINE, (maxy-7));

				mvwaddch(all_win->status_win,
				6, 0, ACS_LTEE);

				mvwhline(all_win->status_win, 6, 1,
				ACS_HLINE, 26);
				
				waddstr(all_win->status_win, "Controller Settings");
				
				/*mvwaddch(all_win->status_win,
				6, 27, ACS_RTEE);*/

				mvwhline(all_win->status_win, 8, 28,
				ACS_HLINE, (maxx-28));

				mvwhline(all_win->status_win, 12, 28,
				ACS_HLINE, (maxx-28));

				mvwhline(all_win->status_win, 16, 28,
				ACS_HLINE, (maxx-28));

				mvwaddch(all_win->status_win,
				8, maxx, ACS_RTEE);

				mvwaddstr(all_win->status_win,
				8, 28, "SAF-TE Information");

				mvwaddch(all_win->status_win,
				8, 27, ACS_LTEE);

				/*mvwhline(all_win->status_win, 15, 29,
				ACS_HLINE, (maxx-29));*/

				/*mvwaddstr(all_win->status_win,
				16, 29, "OS Info");*/

				mvwaddch(all_win->status_win,
				maxy, 27, ACS_BTEE);

				mvwaddch(all_win->status_win,
				(maxy-5), 0, ACS_LTEE);

				whline(all_win->status_win, ACS_HLINE, 26);
				
				mvwaddstr(all_win->status_win,
				maxy-5, 1, "General Driver Info");

				mvwaddch(all_win->status_win,
				maxy-5, 27, ACS_RTEE);

				/*mvwaddch(all_win->status_win,
				16, 27, ACS_LTEE);*/

				/*mvwaddch(all_win->status_win,
				16, maxx, ACS_RTEE);*/
			}

/*			Does this tab have focus?
*/
			if(tab_pool[1].focus){
				int matrixx0 = 2, matrixy0 = 4;
				int matrixx1 = 23, matrixy1 = 4;
				int matrixx2 = 44, matrixy2 = 4;
				int counter;
				char ds_size[10];
				float df_size ;

				/*Asume there are no devices present*/
				wattrset(all_win->status_win, A_BOLD|COLOR_PAIR(BLUE_CYAN));
				mvwaddstr(all_win->status_win, 2, matrixx0, "CH:0 No Devices");
				mvwaddstr(all_win->status_win, 2, matrixx1, "CH:1 No Devices");
				mvwaddstr(all_win->status_win, 2, matrixx2, "CH:2 No Devices");

				for(counter = 0; counter < snap_all[cur_card].no_ph_dev;
				counter++){
/*				
				Set the display matrix
*/
					if(snap_all[cur_card].ph_dev[counter].state[0] == 'D')
						wattrset(all_win->status_win,
						A_BOLD|COLOR_PAIR(BLUE_RED));
					else if(snap_all[cur_card].ph_dev[counter].state[0] == 'O')
						wattrset(all_win->status_win,
						A_BOLD|COLOR_PAIR(BLUE_GREEN));
					else
						wattrset(all_win->status_win,
						A_BOLD|COLOR_PAIR(BLUE_YELLOW));

					df_size = atol(snap_all[cur_card].ph_dev[counter].blocks);
					
					if((snap_all[cur_card].ph_dev[counter].state[0] == 'n')
						|| (snap_all[cur_card].ph_dev[counter].state[0] == 'F')
						|| (snap_all[cur_card].ph_dev[counter].state[0] == 'S'))
						strcpy(ds_size, "");
					else
						sprintf(ds_size, "%.1fGB", ((df_size*512)/(float)1073741824));

					if(snap_all[cur_card].ph_dev[counter].target[0] == '0'){

						if((snap_all[cur_card].ph_dev[counter].focus) &&
							(lock == LOCK_CMD)){

							wattrset(all_win->status_win, CARDLIST_NOFOCUS);
							mvwhline(all_win->status_win, matrixy0, (matrixx0-1),
							' ', 20);
						}

						mvwaddstr(all_win->status_win,
						matrixy0, matrixx0 + 10,
					   	ds_size);

						mvwaddstr(all_win->status_win,
						matrixy0, matrixx0 + 5,
					   	snap_all[cur_card].ph_dev[counter].state);

						mvwaddstr(all_win->status_win,
						matrixy0++, matrixx0,
					   	snap_all[cur_card].ph_dev[counter].target);
						
						wattrset(all_win->status_win,
						A_BOLD|COLOR_PAIR(BLUE_CYAN));

						mvwhline(all_win->status_win, 2, (matrixx0-1),
						' ', 17);
						mvwaddstr(all_win->status_win, 2, matrixx0 - 1, "CH:ID");
						mvwaddstr(all_win->status_win, 2, matrixx0 + 5, "STAT");
						mvwaddstr(all_win->status_win, 2, matrixx0 + 10, "Size");

					}

					if(snap_all[cur_card].ph_dev[counter].target[0] == '1'){
						
						if((snap_all[cur_card].ph_dev[counter].focus) &&
							(lock == LOCK_CMD)){

							wattrset(all_win->status_win, CARDLIST_NOFOCUS);
							mvwhline(all_win->status_win, matrixy1, (matrixx1-1),
							' ', 20);
						}

						mvwaddstr(all_win->status_win,
						matrixy1, matrixx1 + 10,
					   	ds_size);

						mvwaddstr(all_win->status_win,
						matrixy1, matrixx1 + 5,
					   	snap_all[cur_card].ph_dev[counter].state);

						mvwaddstr(all_win->status_win,
						matrixy1++, matrixx1,
					   	snap_all[cur_card].ph_dev[counter].target);

/*						Display all the cyan characters
*/   
						wattrset(all_win->status_win,
						A_BOLD|COLOR_PAIR(BLUE_CYAN));

						mvwhline(all_win->status_win, 2, (matrixx1-1),
						' ', 17);
						mvwaddstr(all_win->status_win, 2, matrixx1-1, "CH:ID");
						mvwaddstr(all_win->status_win, 2, matrixx1 + 5, "STAT");
						mvwaddstr(all_win->status_win, 2, matrixx1 + 10, "Size");
					}

					if(snap_all[cur_card].ph_dev[counter].target[0] == '2'){
						
						if((snap_all[cur_card].ph_dev[counter].focus) &&
							(lock == LOCK_CMD)){

							wattrset(all_win->status_win, CARDLIST_NOFOCUS);
							mvwhline(all_win->status_win, matrixy2, (matrixx2-1),
							' ', 19);
						}

						mvwaddstr(all_win->status_win,
						matrixy2, matrixx2 + 10,
					   	ds_size);

						mvwaddstr(all_win->status_win,
						matrixy2, matrixx2 + 5,
					   	snap_all[cur_card].ph_dev[counter].state);

						mvwaddstr(all_win->status_win,
						matrixy2++, matrixx2,
					   	snap_all[cur_card].ph_dev[counter].target);

/*						Display all the cyan characters
*/   
						wattrset(all_win->status_win,
						A_BOLD|COLOR_PAIR(BLUE_CYAN));

						mvwhline(all_win->status_win, 2, (matrixx2-1),
						' ', 17);
						mvwaddstr(all_win->status_win, 2, matrixx2-1, "CH:ID");
						mvwaddstr(all_win->status_win, 2, matrixx2 + 5, "STAT");
						mvwaddstr(all_win->status_win, 2, matrixx2 + 10, "Size");
					}
				}

/*				Display all the white characters
*/   
				wattrset(all_win->status_win,
				A_BOLD|COLOR_PAIR(BLUE_WHITE));

				mvwhline(all_win->status_win, 3, 1,
				ACS_HLINE, (maxx-1));
				
				mvwvline(all_win->status_win, 2, (matrixx1-2),
				ACS_VLINE, (maxy-2));

				mvwvline(all_win->status_win, 2, (matrixx2-2),
				ACS_VLINE, (maxy-2));

				mvwaddch(all_win->status_win,
				3, 0, ACS_LTEE);

				mvwaddch(all_win->status_win,
				3, maxx, ACS_RTEE);
				
				mvwaddch(all_win->status_win,
				3, (matrixx1-2), ACS_PLUS);

				mvwaddch(all_win->status_win,
				3, (matrixx2-2), ACS_PLUS);

				mvwaddch(all_win->status_win,
				1, (matrixx1-2), ACS_TTEE);

				mvwaddch(all_win->status_win,
				1, (matrixx2-2), ACS_TTEE);

				mvwaddch(all_win->status_win,
				maxy, (matrixx1-2), ACS_BTEE);

				mvwaddch(all_win->status_win,
				maxy, (matrixx2-2), ACS_BTEE);
			}

/*			Does this tab have focus?
*/
			if(tab_pool[2].focus){
				int matrixx0 = 1, matrixy0 = 4;
				int counter, mid = 31;
				char *offset;
				char ds_size[10];
				float df_size ;

/*				Display all the cyan characters
*/ 
				wattrset(all_win->status_win,
				A_BOLD|COLOR_PAIR(BLUE_CYAN));

				mvwaddstr(all_win->status_win, 2, matrixx0, "DEV");
				mvwaddstr(all_win->status_win, 2, matrixx0 + 6, "STAT");
				mvwaddstr(all_win->status_win, 2, matrixx0 + 13, "Size");
				mvwaddstr(all_win->status_win, 2, matrixx0 + 20, "Opt");
				mvwaddstr(all_win->status_win, 2, matrixx0 + 24, "Level");

				wattrset(all_win->status_win,
				A_BOLD|COLOR_PAIR(BLUE_GREEN));

				for(counter = 0; counter < snap_all[cur_card].no_lg_drv;
				counter++){
					offset = rindex(snap_all[cur_card].lg_drv[counter].dev_name, '/');
					offset++;

					if(counter == 16){
					   	matrixx0 = mid;
					   	matrixx0++;
						matrixy0 = 4;

						wattrset(all_win->status_win,
						A_BOLD|COLOR_PAIR(BLUE_CYAN));

						mvwaddstr(all_win->status_win, 2, matrixx0, "DEV");
						mvwaddstr(all_win->status_win, 2, matrixx0 + 6, "STAT");
						mvwaddstr(all_win->status_win, 2, matrixx0 + 13, "Size");
						mvwaddstr(all_win->status_win, 2, matrixx0 + 20, "Opt");
						mvwaddstr(all_win->status_win, 2, matrixx0 + 24, "Level");
						
						wattrset(all_win->status_win,
						A_BOLD|COLOR_PAIR(BLUE_GREEN));
					}
						
					if((snap_all[cur_card].lg_drv[counter].focus) &&
							(lock == LOCK_DEV)){
							wattrset(all_win->status_win, CARDLIST_NOFOCUS);
							mvwhline(all_win->status_win, matrixy0, matrixx0,
							' ', 30);
					}else if(snap_all[cur_card].lg_drv[counter].state[1] == 'F')
						wattrset(all_win->status_win,
						A_BOLD|COLOR_PAIR(BLUE_RED));
					else if(snap_all[cur_card].lg_drv[counter].state[1] == 'N')
						wattrset(all_win->status_win,
						A_BOLD|COLOR_PAIR(BLUE_GREEN));
					else
						wattrset(all_win->status_win,
						A_BOLD|COLOR_PAIR(BLUE_YELLOW));

					mvwaddstr(all_win->status_win,
					matrixy0, matrixx0 + 6,
				   	snap_all[cur_card].lg_drv[counter].state);

					df_size = atol(snap_all[cur_card].lg_drv[counter].blocks);
					sprintf(ds_size, "%.1fGB", ((df_size*512)/(float)1073741824));

					mvwaddstr(all_win->status_win,
					matrixy0, matrixx0 + 12,
				   	ds_size);

					mvwaddstr(all_win->status_win,
					matrixy0, matrixx0 + 20,
				   	snap_all[cur_card].lg_drv[counter].wrt_opt);

					mvwaddstr(all_win->status_win,
					matrixy0, matrixx0 + 23,
				   	snap_all[cur_card].lg_drv[counter].raid_l);

					mvwaddstr(all_win->status_win,
					matrixy0++, matrixx0,
				   	offset);


				}

/*				Display all the white characters
*/   
				wattrset(all_win->status_win,
				A_BOLD|COLOR_PAIR(BLUE_WHITE));

				mvwhline(all_win->status_win, 3, 1,
				ACS_HLINE, (maxx-1));
				
				mvwaddch(all_win->status_win,
				3, 0, ACS_LTEE);

				mvwaddch(all_win->status_win,
				3, maxx, ACS_RTEE);
				
				mvwaddch(all_win->status_win,
				3, mid, ACS_TTEE);

				mvwaddch(all_win->status_win,
				maxy, mid, ACS_BTEE);


				mvwvline(all_win->status_win, 4, mid,
				ACS_VLINE, (maxy-4));

			}

/*			Does this tab have focus?
*/
			if(tab_pool[3].focus){
				int matrixx = 2, matrixy = 3;
				int i;
				
				wattrset(all_win->status_win,
				A_BOLD|COLOR_PAIR(BLUE_WHITE));

				mvwaddch(all_win->status_win,
				2, 0, ACS_LTEE);

				mvwaddch(all_win->status_win,
				2, maxx, ACS_RTEE);

				mvwhline(all_win->status_win, 2, 1,
				ACS_HLINE, (maxx-1));

				mvwaddch(all_win->status_win,
				2, 14, ACS_TTEE);
				
				
				mvwaddch(all_win->status_win,
				10, 0, ACS_LTEE);

				mvwaddch(all_win->status_win,
				10, maxx, ACS_RTEE);

				mvwhline(all_win->status_win, 10, 1,
				ACS_HLINE, (maxx-1));

				mvwaddch(all_win->status_win,
				10, 14, ACS_BTEE);

				mvwvline(all_win->status_win, 3, 14,
				ACS_VLINE, 7);
				
				matrixx = 30;
				matrixy = 2;

				mvwaddstr(all_win->status_win,
				matrixy, matrixx, "Description");

				matrixx = 2;

				mvwaddstr(all_win->status_win,
				matrixy, matrixx, "Commands");

				mvwaddstr(all_win->status_win,
				matrixy, matrixx, "Commands");

				mvwaddch(all_win->status_win,
				12, 5, ACS_ULCORNER);
				
				mvwaddch(all_win->status_win,
				12, 58, ACS_URCORNER);

				mvwhline(all_win->status_win, 12, 6,
				ACS_HLINE, 52);

				mvwvline(all_win->status_win, 13, 5,
				ACS_VLINE, 4);
				
				mvwhline(all_win->status_win, 14, 7,
				ACS_CKBOARD, 50);
				
				mvwhline(all_win->status_win, 15, 7,
				ACS_CKBOARD, 50);

				mvwvline(all_win->status_win, 13, 58,
				ACS_VLINE, 4);
				
				mvwhline(all_win->status_win, 17, 6,
				ACS_HLINE, 52);

				mvwaddch(all_win->status_win,
				17, 5, ACS_LLCORNER);
				
				mvwaddch(all_win->status_win,
				17, 58, ACS_LRCORNER);

				if(!snap_all[cur_card].rebuild){
					mvwaddstr(all_win->status_win,
					12, 11, "No Rebuild or Consistency Check in Progress");
				}else{
					char put_line[80];
					int percent;
					percent = atoi(snap_all[cur_card].rb_done);
					percent /= 2;
					
					wattrset(all_win->status_win,
					A_REVERSE|COLOR_PAIR(CYAN_GREEN));
					mvwhline(all_win->status_win, 14, 7,
					' ', percent);
				
					mvwhline(all_win->status_win, 15, 7,
					' ', percent);
					
					wattrset(all_win->status_win,
					A_BOLD|COLOR_PAIR(BLUE_WHITE));

					if(snap_all[cur_card].rebuild == REBUILD){
						sprintf(put_line, "Drive (%s) is being rebuilt.",
						snap_all[cur_card].rb_dev);
					}else{
						sprintf(put_line, "Checking Consistency on (%s).",
						snap_all[cur_card].rb_dev);
					}


					mvwaddstr(all_win->status_win,
					13, 13, put_line);
					
					sprintf(put_line, "%s%% is done.",
							snap_all[cur_card].rb_done);

					mvwaddstr(all_win->status_win,
					16, 26, put_line);
				}

				matrixy = 3;

/*				Display all the cyan characters
*/ 
				wattrset(all_win->status_win,
				A_DIM|COLOR_PAIR(BLUE_CYAN));

				for(i = 0; i < 6; i++){
					if((lock == LOCK_CARDS) && cmd[i].focus){
						wattrset(all_win->status_win,
						CARDLIST_NOFOCUS);
						mvwhline(all_win->status_win, matrixy, 2,
						' ', 11);
						
						mvwaddstr(all_win->status_win,
						matrixy++, matrixx, cmd[i].name);
						
						wattrset(all_win->status_win,
						COLOR_PAIR(BLUE_WHITE));

						if(i == 0){
							mvwaddstr(all_win->status_win,
							3, 16, "This command kills physical device(s) in the");
							
							mvwaddstr(all_win->status_win,
							4, 16, "chain.");
							
							mvwaddstr(all_win->status_win,
							6, 16, "Caution: device(s) you select will be marked");
							
							mvwaddstr(all_win->status_win,
							7, 16, "as DED, press ENTER to get into device");
							
							mvwaddstr(all_win->status_win,
							8, 16, "selection or ESC to exit from Command mode.");
							
						}else if(i == 1){
							
							mvwaddstr(all_win->status_win,
							3, 16, "This command forces physical device(s) in the");
							
							mvwaddstr(all_win->status_win,
							4, 16, "chain to become Online.");
							
							mvwaddstr(all_win->status_win,
							6, 16, "Caution: device(s) you select will be marked");
							
							mvwaddstr(all_win->status_win,
							7, 16, "as ONL, press ENTER to get into device");
							
							mvwaddstr(all_win->status_win,
							8, 16, "selection or ESC to exit from Command mode.");

						}else if(i == 2){

							mvwaddstr(all_win->status_win,
							3, 16, "This command forces physical device(s) in the");
							
							mvwaddstr(all_win->status_win,
							4, 16, "chain to become Standby.");
							
							mvwaddstr(all_win->status_win,
							6, 16, "Caution: device(s) you select will be marked");
							
							mvwaddstr(all_win->status_win,
							7, 16, "as SBY, press ENTER to get into device");
							
							mvwaddstr(all_win->status_win,
							8, 16, "selection or ESC to exit from Command mode.");
							
						}else if(i == 3){

							mvwaddstr(all_win->status_win,
							3, 16, "This command initializes an asynchronous");
							
							mvwaddstr(all_win->status_win,
							4, 16, "rebuild on to a selected physical device.");
							
							mvwaddstr(all_win->status_win,
							6, 16, "Press ENTER to get into device selection or");
							
							mvwaddstr(all_win->status_win,
							7, 16, "ESC to exit from Command mode.");
							
						}else if(i == 4){

							mvwaddstr(all_win->status_win,
							3, 16, "This command initializes an asynchronous");
							
							mvwaddstr(all_win->status_win,
							4, 16, "process where the integrity of redundant data");
							
							mvwaddstr(all_win->status_win,
							5, 16, "on a selected RAID array is verified.");
							
							mvwaddstr(all_win->status_win,
							7, 16, "Press ENTER to get into array selection or");
							
							mvwaddstr(all_win->status_win,
							8, 16, "ESC to exit from Command mode.");

							
						}else if(i == 5){
							
							mvwaddstr(all_win->status_win,
							3, 16, "This command cancels any rebuild or");
							
							mvwaddstr(all_win->status_win,
							4, 16, "consistency check in progress.");
							
							mvwaddstr(all_win->status_win,
							6, 16, "Press ENTER to cancel activity or ESC to exit");
							
							mvwaddstr(all_win->status_win,
							7, 16, "from Command mode.");
						}else if(i == 6){
							
							mvwaddstr(all_win->status_win,
							4, 16, "Press ENTER to cancel any consistency check in");
							
							mvwaddstr(all_win->status_win,
							5, 16, "progress, or press ESC to exit Expert Mode.");
							
						}

					}else if((lock == LOCK_CARDS)&& !cmd[i].focus){
						wattrset(all_win->status_win,
						A_BOLD|COLOR_PAIR(BLUE_CYAN));
						
						mvwaddstr(all_win->status_win,
						matrixy++, matrixx, cmd[i].name);
					}else{
						wattrset(all_win->status_win,
						A_DIM|COLOR_PAIR(BLUE_CYAN));
						
						mvwaddstr(all_win->status_win,
						matrixy++, matrixx, cmd[i].name);
					}

				}

/*				Display all the yellow characters
*/   
				if(lock == NO_LOCK){
					wattrset(all_win->status_win,
					COLOR_PAIR(BLUE_WHITE));

					mvwaddstr(all_win->status_win,
					4, 17, "The \"Expert\" tab allows you to manipulate");
				
					mvwaddstr(all_win->status_win,
					5, 17, "current RAID status. Use UP/DOWN arrows to");
				
					mvwaddstr(all_win->status_win,
					6, 17, "select a card than press ENTER to lock the");
					
					mvwaddstr(all_win->status_win,
					7, 17, "selected card.");
				}
				
			}	

		}	
	}	
	return 1;	
}
	
/*
--------------------------------------------------------------------------------
	Interact with user*/
void mv_fc(SWINDOWS *all_win, CARD *card_pool, TAB *tab_pool, int key_press, int no_cards){
	int dummy = 0, x = 0, card_focus = 0, tab_focus = 0;
	int dummy1 = 0;
        size_t dummy2 = 0;
	char line[80];
	
	if(key_press != KEY_INIT){
		for(;dummy < no_cards; dummy++)
			if(card_pool[dummy].focus) card_focus = dummy;
		dummy = 0;
		for(;dummy < NO_TABS; dummy++)
			if(tab_pool[dummy].focus) tab_focus = dummy;

	}
	
	dummy = 0;
	switch (key_press){
		case KEY_INIT:
			for(;dummy < no_pages; dummy++){
				if(card_pool[dummy].focus){

	            	if(lock) wattrset(all_win->cardlist, CARDLIST_LOCKED);
					else if(!snap_all[dummy].got_dead)
						wattrset(all_win->cardlist, A_BOLD|COLOR_PAIR(BLUE_GREEN));
					else if(snap_all[dummy].got_dead == 1)
						wattrset(all_win->cardlist, A_BOLD|COLOR_PAIR(BLUE_YELLOW));
					else if(snap_all[dummy].got_dead == 2)
						wattrset(all_win->cardlist, A_BOLD|COLOR_PAIR(BLUE_RED));
					
					strcpy(line, snap_all[dummy].c_type);
					if(snap_all[dummy].got_dead)
						strcat(line, "*");
					else
						strcat(line, " ");
					strcat(line, snap_all[dummy].chnl);
					strcat(line, "C");

				    mvwaddstr(all_win->cardlist,
					card_pool[dummy].y, card_pool[dummy].x,
					line);
					
				} else {
					if(!snap_all[dummy].got_dead)
						wattrset(all_win->cardlist, A_BOLD|COLOR_PAIR(CYAN_GREEN));
					else if(snap_all[dummy].got_dead == 1)
						wattrset(all_win->cardlist, A_BOLD|COLOR_PAIR(CYAN_YELLOW));
					else if(snap_all[dummy].got_dead == 2)
						wattrset(all_win->cardlist, A_BOLD|COLOR_PAIR(CYAN_RED));
						
/*					wattrset(all_win->cardlist, CARDLIST_NOFOCUS);*/
					strcpy(line, snap_all[dummy].c_type);
					if(snap_all[dummy].got_dead)
						strcat(line, "*");
					else
						strcat(line, " ");
					strcat(line, snap_all[dummy].chnl);
					strcat(line, "C");

				    mvwaddstr(all_win->cardlist,
					card_pool[dummy].y, card_pool[dummy].x,
					line);
					
				}
			}
			
           	if(lock)wattrset(all_win->cardlist, CARDLIST_LOCKED);
			else wattrset(all_win->cardlist, CARDLIST_NOFOCUS);

			box(all_win->cardlist,0,0);
		    mvwaddstr(all_win->cardlist, 0,1, "Detected cards");

			dummy = 0;
			wmove(all_win->status_win, 0,0);
			wattrset(all_win->status_win, A_BOLD|COLOR_PAIR(BLUE_WHITE));
			box(all_win->status_win, 0,0);
			for(;dummy < NO_TABS; dummy++){

				if(!tab_pool[dummy].focus){
					wattrset(all_win->status_win, A_BOLD|COLOR_PAIR(BLUE_YELLOW)); 
					waddch(all_win->status_win, ' ');
					waddstr(all_win->status_win, tab_pool[dummy].label);
					waddch(all_win->status_win, ' ');
					wattrset(all_win->status_win, A_BOLD|COLOR_PAIR(BLUE_WHITE));


				}else{
					wattrset(all_win->status_win, A_BOLD|COLOR_PAIR(BLUE_WHITE));
					waddch(all_win->status_win, tab_pool[dummy].lpad);
					waddstr(all_win->status_win, tab_pool[dummy].label);
					waddch(all_win->status_win, tab_pool[dummy].rpad);
				}
				for(dummy1 = 0; dummy1 < tab_pool[dummy].x; dummy1++) waddch(all_win->status_win, ' ');
				x += (2+strlen(tab_pool[dummy].label)+tab_pool[dummy].x);
			}
			
			whline(all_win->status_win, ' ', (all_win->status_win->_maxx-x)+1);


			dummy = 0;
			x = 0;
			wmove(all_win->status_win, 1,0);
			for(;dummy < NO_TABS; dummy++){
				if(!tab_pool[dummy].focus){
					if(dummy == 0)
						waddch(all_win->status_win, ACS_ULCORNER);
					else
						waddch(all_win->status_win, ACS_HLINE);

					for(dummy2 = 0; dummy2 < strlen(tab_pool[dummy].label); dummy2++) waddch(all_win->status_win, ACS_HLINE);
					waddch(all_win->status_win, ACS_HLINE);
				}else {
					waddch(all_win->status_win, tab_pool[dummy].llpad);
					for(dummy2 = 0; dummy2 < strlen(tab_pool[dummy].label); dummy2++) waddch(all_win->status_win, ' ');
					waddch(all_win->status_win, tab_pool[dummy].lrpad);
				}
				for(dummy1 = 0; dummy1 < tab_pool[dummy].x; dummy1++) waddch(all_win->status_win, ACS_HLINE);
				x += (2+strlen(tab_pool[dummy].label)+tab_pool[dummy].x);
			}
			whline(all_win->status_win, ACS_HLINE, (all_win->status_win->_maxx-x));
			mvwaddch(all_win->status_win, 1, all_win->status_win->_maxx, ACS_URCORNER);
			
			get_stat(all_win, card_pool, tab_pool);
			
			wrefresh(all_win->cardlist);
			wrefresh(all_win->status_win);
			move(0,0);
			
			refresh();

		break;

		case KEY_DOWN:
			if(lock == LOCK_CARDS){
				int i;
				for(i = 0; i < 6; i++){
					if(cmd[i].focus){
						cmd[i].focus = 0;
						if((i+1) == 6)
							cmd[0].focus = '0';
						else
							cmd[(i+1)].focus = '0';
						break;
					}
				}

				get_stat(all_win, card_pool, tab_pool);
				wrefresh(all_win->status_win);
				refresh();
				break;
			}
			
			if(lock == LOCK_CMD){
				int i;
				for(i = 0; i < snap_all[card_focus].no_ph_dev; i++)
					if(snap_all[card_focus].ph_dev[i].focus){
						if((i+1) == snap_all[card_focus].no_ph_dev){
							snap_all[card_focus].ph_dev[0].focus = '0';
							snap_all[card_focus].ph_dev[i].focus = 0;
							break;
						}else{
						   	snap_all[card_focus].ph_dev[(i+1)].focus = '0';
						   	snap_all[card_focus].ph_dev[i].focus = 0;
							break;
						}
					}
				get_stat(all_win, card_pool, tab_pool);
				wrefresh(all_win->status_win);
				refresh();
				break;
			}
			
			
			if(lock == LOCK_DEV){
				int i;
				for(i = 0; i < snap_all[card_focus].no_lg_drv; i++)
					if(snap_all[card_focus].lg_drv[i].focus){
						if((i+1) == snap_all[card_focus].no_lg_drv){
							if(i){
								snap_all[card_focus].lg_drv[0].focus = '0';
								snap_all[card_focus].lg_drv[i].focus = 0;
							}
							break;
						}else{
						   	snap_all[card_focus].lg_drv[(i+1)].focus = '0';
						   	snap_all[card_focus].lg_drv[i].focus = 0;
							break;
						}
					}
				get_stat(all_win, card_pool, tab_pool);
				wrefresh(all_win->status_win);
				refresh();
				break;
			}


			if(no_cards == 1) break;
			card_pool[card_focus].focus = 0;
			wattrset(all_win->cardlist, CARDLIST_NOFOCUS);
			
			

			strcpy(line, snap_all[card_focus].c_type);
			if(snap_all[card_focus].got_dead)
				strcat(line, "*");
			else
				strcat(line, " ");
			strcat(line, snap_all[card_focus].chnl);
			strcat(line, "C");


			box(all_win->cardlist,0,0);
		    mvwaddstr(all_win->cardlist, 0,1, "Detected cards");

			if(!snap_all[card_focus].got_dead)
				wattrset(all_win->cardlist, A_BOLD|COLOR_PAIR(CYAN_GREEN));
			else if(snap_all[card_focus].got_dead == 1)
				wattrset(all_win->cardlist, A_BOLD|COLOR_PAIR(CYAN_YELLOW));
			else if(snap_all[card_focus].got_dead == 2)
					wattrset(all_win->cardlist, A_BOLD|COLOR_PAIR(CYAN_RED));

		    mvwaddstr(all_win->cardlist, card_pool[card_focus].y,
			card_pool[card_focus].x, line);

			if((card_focus+1) == no_cards) card_focus = 0;
			else card_focus++;

			card_pool[card_focus].focus = '0' ;
			
		        
			if(!snap_all[card_focus].got_dead)
				wattrset(all_win->cardlist, A_BOLD|COLOR_PAIR(BLUE_GREEN));
			else if(snap_all[card_focus].got_dead == 1)
				wattrset(all_win->cardlist, A_BOLD|COLOR_PAIR(BLUE_YELLOW));
			else if(snap_all[card_focus].got_dead == 2)
					wattrset(all_win->cardlist, A_BOLD|COLOR_PAIR(BLUE_RED));

			strcpy(line, snap_all[card_focus].c_type);
			if(snap_all[card_focus].got_dead)
				strcat(line, "*");
			else
				strcat(line, " ");
			strcat(line, snap_all[card_focus].chnl);
			strcat(line, "C");
			
			mvwaddstr(all_win->cardlist, card_pool[card_focus].y,
			card_pool[card_focus].x, line);
		        
			wattrset(all_win->cardlist, CARDLIST_NOFOCUS);

			get_stat(all_win, card_pool, tab_pool);

			wrefresh(all_win->status_win);
			
			wrefresh(all_win->cardlist);
			refresh();
		break;

		case KEY_UP:
			if(lock == LOCK_CARDS){
				int i;
				for(i = 5; i > -1; i--){
					if(cmd[i].focus){
						cmd[i].focus = 0;
						if((i-1) == -1)
							cmd[5].focus = '0';
						else
							cmd[(i-1)].focus = '0';
						break;
					}
				}

				get_stat(all_win, card_pool, tab_pool);
				wrefresh(all_win->status_win);
				refresh();
				break;
			}

			if(lock == LOCK_CMD){
				int i, no_drv;

				i = snap_all[card_focus].no_ph_dev;
				i--;
				no_drv = i;

				for(; i > -1; i--)
					if(snap_all[card_focus].ph_dev[i].focus){
						if((i-1) == -1){
							snap_all[card_focus].ph_dev[no_drv].focus = '0';
							snap_all[card_focus].ph_dev[i].focus = 0;
							break;
						}else{
						   	snap_all[card_focus].ph_dev[(i-1)].focus = '0';
						   	snap_all[card_focus].ph_dev[i].focus = 0;
							break;
						}
					}
				get_stat(all_win, card_pool, tab_pool);
				wrefresh(all_win->status_win);
				refresh();
				break;
			}
			
			if(lock == LOCK_DEV){
				int i, no_dev;

				i = snap_all[card_focus].no_lg_drv;
				i--;
				no_dev = i;

				for(; i > -1; i--)
					if(snap_all[card_focus].lg_drv[i].focus){
						if(((i-1) == -1)){
							if(no_dev){
								snap_all[card_focus].lg_drv[no_dev].focus = '0';
								snap_all[card_focus].lg_drv[i].focus = 0;
							}
							break;
						}else{
						   	snap_all[card_focus].lg_drv[(i-1)].focus = '0';
						   	snap_all[card_focus].lg_drv[i].focus = 0;
							break;
						}
					}
				get_stat(all_win, card_pool, tab_pool);
				wrefresh(all_win->status_win);
				refresh();
				break;
			}
			
			

			if(no_cards ==  1) break;
			card_pool[card_focus].focus = 0;
			wattrset(all_win->cardlist, CARDLIST_NOFOCUS);

			box(all_win->cardlist,0,0);
		    mvwaddstr(all_win->cardlist, 0,1, "Detected cards");

			if(!snap_all[card_focus].got_dead)
				wattrset(all_win->cardlist, A_BOLD|COLOR_PAIR(CYAN_GREEN));
			else if(snap_all[card_focus].got_dead == 1)
				wattrset(all_win->cardlist, A_BOLD|COLOR_PAIR(CYAN_YELLOW));
			else if(snap_all[card_focus].got_dead == 2)
					wattrset(all_win->cardlist, A_BOLD|COLOR_PAIR(CYAN_RED));

			strcpy(line, snap_all[card_focus].c_type);
			if(snap_all[card_focus].got_dead)
				strcat(line, "*");
			else
				strcat(line, " ");
			strcat(line, snap_all[card_focus].chnl);
			strcat(line, "C");
			
			mvwaddstr(all_win->cardlist, card_pool[card_focus].y,
			card_pool[card_focus].x, line);

			wattrset(all_win->cardlist, CARDLIST_FOCUS);
			if(!card_focus) card_focus = (no_cards-1); else card_focus--;

			card_pool[card_focus].focus = '0' ;

			if(!snap_all[card_focus].got_dead)
				wattrset(all_win->cardlist, A_BOLD|COLOR_PAIR(BLUE_GREEN));
			else if(snap_all[card_focus].got_dead == 1)
				wattrset(all_win->cardlist, A_BOLD|COLOR_PAIR(BLUE_YELLOW));
			else if(snap_all[card_focus].got_dead == 2)
					wattrset(all_win->cardlist, A_BOLD|COLOR_PAIR(BLUE_RED));

			strcpy(line, snap_all[card_focus].c_type);
			if(snap_all[card_focus].got_dead)
				strcat(line, "*");
			else
				strcat(line, " ");
			strcat(line, snap_all[card_focus].chnl);
			strcat(line, "C");
			
			mvwaddstr(all_win->cardlist, card_pool[card_focus].y,
			card_pool[card_focus].x, line);
			
			wattrset(all_win->cardlist, CARDLIST_NOFOCUS);

			get_stat(all_win, card_pool, tab_pool);

			wrefresh(all_win->status_win);
			
			
			wrefresh(all_win->cardlist);
			refresh();
		break;

		case KEY_ESCPE:
			if(lock == LOCK_CMD || lock == LOCK_DEV) {
				int i;
				lock = NO_LOCK;
				for(i = 0; i < 4; i++)
					tab_pool[i].focus = 0;
				tab_pool[3].focus = '0';
				mv_fc(all_win, card_pool, tab_pool, KEY_INIT, no_pages);
				return;;
			}
			if(lock == LOCK_CARDS) lock = NO_LOCK;
            wattrset(all_win->cardlist, CARDLIST_NOFOCUS); 
			box(all_win->cardlist,0,0);
		    mvwaddstr(all_win->cardlist, 0,1, "Detected cards");

			strcpy(line, snap_all[card_focus].c_type);
			if(snap_all[card_focus].got_dead)
				strcat(line, "*");
			else
				strcat(line, " ");
			strcat(line, snap_all[card_focus].chnl);
			strcat(line, "C");
			
/*            wattrset(all_win->cardlist, CARDLIST_FOCUS); */

			if(!snap_all[card_focus].got_dead)
				wattrset(all_win->cardlist, A_BOLD|COLOR_PAIR(BLUE_GREEN));
			else if(snap_all[card_focus].got_dead == 1)
				wattrset(all_win->cardlist, A_BOLD|COLOR_PAIR(BLUE_YELLOW));
			else if(snap_all[card_focus].got_dead == 2)
					wattrset(all_win->cardlist, A_BOLD|COLOR_PAIR(BLUE_RED));
					
			mvwaddstr(all_win->cardlist, card_pool[card_focus].y,
			card_pool[card_focus].x, line);
			get_stat(all_win, card_pool, tab_pool);
			wrefresh(all_win->status_win);
			wrefresh(all_win->cardlist);

		break;

		case KEY_RETURN:
		case KEY_RETURN1:
			if(lock == LOCK_CARDS){
				int i;
				
				for(i = 0; i < snap_all[card_focus].no_ph_dev; i++){
					snap_all[card_focus].ph_dev[i].focus = 0;
				}
				for(i = 0; i < snap_all[card_focus].no_lg_drv; i++){
					snap_all[card_focus].lg_drv[i].focus = 0;
				}
				snap_all[card_focus].ph_dev[0].focus = '0';
				snap_all[card_focus].lg_drv[0].focus = '0';
				
				for(i = 0; i < 4; i++)
					tab_pool[i].focus = 0;
				if(cmd[4].focus){
					lock = LOCK_DEV;
				}else if(cmd[5].focus){
					char msg[200], path[200], *tmp;
					tab_pool[3].focus = '0';
					strcpy(msg, "cancel-rebuild");
					strcpy(path, snap_all[card_focus].card_path);
					tmp = rindex(path, '/');
					tmp++;
					*tmp = 0;
					strcat(path, "user_command");
					command(path, msg);
				/*}else if(cmd[6].focus){
					char msg[200], path[200], *tmp;
					tab_pool[3].focus = '0';
					strcpy(msg, "cancel-consistency-check");
					strcpy(path, snap_all[card_focus].card_path);
					tmp = rindex(path, '/');
					tmp++;
					*tmp = 0;
					strcat(path, "user_command");
					command(path, msg);*/
				}else{
					tab_pool[2].focus = '0';
					lock = LOCK_CMD;
				}
				
				mv_fc(all_win, card_pool, tab_pool, KEY_LEFT, no_pages);
				
				return;
			}

			if(lock == LOCK_CMD){
				char msg[200], path[200], *tmp;
				int i, j;

				for(i = 0; i < 6; i++){
					if(cmd[i].focus)
						break;
				}

				for(j = 0; j < snap_all[card_focus].no_ph_dev; j++){
					if(snap_all[card_focus].ph_dev[j].focus)
						break;
				}
				
				if(i == 0){
					sprintf(msg, "kill %s", snap_all[card_focus].ph_dev[j].target);
				}else if(i == 1){
					sprintf(msg, "make-online %s", snap_all[card_focus].ph_dev[j].target);
				}else if(i == 2){
					sprintf(msg, "make-standby %s", snap_all[card_focus].ph_dev[j].target);
				}else if(i == 3){
					sprintf(msg, "rebuild %s", snap_all[card_focus].ph_dev[j].target);
				}
				strcpy(path, snap_all[card_focus].card_path);
				tmp = rindex(path, '/');
				tmp++;
				*tmp = 0;
				strcat(path, "user_command");
				command(path, msg);
	
			}
				
			if(lock == LOCK_DEV){
				char msg[200], path[200], *tmp;
				int i, j;

				for(i = 0; i < 6; i++){
					if(cmd[i].focus)
						break;
				}

				for(j = 0; j < snap_all[card_focus].no_lg_drv; j++){
					if(snap_all[card_focus].lg_drv[j].focus)
						break;
				}
			
				if(i == 4){
					char *offset;
					offset = strstr(snap_all[card_focus].lg_drv[j].dev_name, "/c");
					offset +=4;
					sprintf(msg, "check-consistency %s", offset);
				}
				
				strcpy(path, snap_all[card_focus].card_path);
				tmp = rindex(path, '/');
				tmp++;
				*tmp = 0;
				strcat(path, "user_command");
				command(path, msg);
			}

			if(tab_pool[3].focus){
				int i;
	            wattrset(all_win->cardlist, CARDLIST_LOCKED);
				box(all_win->cardlist,0,0);
			    mvwaddstr(all_win->cardlist, 0,1, "Detected cards");

				strcpy(line, snap_all[card_focus].c_type);
				if(snap_all[card_focus].got_dead)
					strcat(line, "*");
				else
					strcat(line, " ");
				strcat(line, snap_all[card_focus].chnl);
				strcat(line, "C");
			
				mvwaddstr(all_win->cardlist, card_pool[card_focus].y,
				card_pool[card_focus].x, line);

				lock = LOCK_CARDS;
				for(i = 0; i < 6; i++)
					cmd[i].focus = 0;
				cmd[0].focus = '0';

				get_stat(all_win, card_pool, tab_pool);
				
	            wattrset(all_win->cardlist, CARDLIST_NOFOCUS); 
				wrefresh(all_win->cardlist);
				wrefresh(all_win->status_win);
				refresh();
			}
		break;

		case KEY_RIGHT:
			if(lock == LOCK_CARDS) break;
			if(lock == NO_LOCK){
				wattrset(all_win->cardlist, CARDLIST_NOFOCUS);
				box(all_win->cardlist,0,0);
			    mvwaddstr(all_win->cardlist, 0,1, "Detected cards");
			}

			wattrset(all_win->status_win, A_BOLD|COLOR_PAIR(BLUE_WHITE)); 
			box(all_win->status_win, 0, 0);
			
			if(lock == LOCK_CMD){
				tab_pool[tab_focus].focus = 0;
				tab_pool[1].focus = '0';
			}else if(lock == LOCK_DEV){
				tab_pool[tab_focus].focus = 0;
				tab_pool[2].focus = '0';
			}else {
				tab_pool[tab_focus].focus = 0;
				if((tab_focus+1) == NO_TABS) tab_focus = 0; else tab_focus++;
				tab_pool[tab_focus].focus = '0';
			}

			dummy = 0;
			wmove(all_win->status_win, 0,0);
			for(;dummy < NO_TABS; dummy++){

				if(!tab_pool[dummy].focus){
					wattrset(all_win->status_win, COLOR_PAIR(BLUE_BLACK)); 
					waddch(all_win->status_win, ' ');
					waddstr(all_win->status_win, tab_pool[dummy].label);
					waddch(all_win->status_win, ' ');
					wattrset(all_win->status_win, A_BOLD|COLOR_PAIR(BLUE_WHITE));


				}else{
					wattrset(all_win->status_win, A_BOLD|COLOR_PAIR(BLUE_WHITE));
					waddch(all_win->status_win, tab_pool[dummy].lpad);
					waddstr(all_win->status_win, tab_pool[dummy].label);
					waddch(all_win->status_win, tab_pool[dummy].rpad);
				}
				for(dummy1 = 0; dummy1 < tab_pool[dummy].x; dummy1++) waddch(all_win->status_win, ' ');
				x += (2+strlen(tab_pool[dummy].label)+tab_pool[dummy].x);
			}
			whline(all_win->status_win, ' ', (all_win->status_win->_maxx-x)+1);
			dummy = 0;
			x = 0;
			wmove(all_win->status_win, 1,0);
			for(;dummy < NO_TABS; dummy++){
				if(!tab_pool[dummy].focus){
					if(dummy) waddch(all_win->status_win, ACS_HLINE);
					else waddch(all_win->status_win, ACS_ULCORNER);
					for(dummy2 = 0; dummy2 < strlen(tab_pool[dummy].label); dummy2++) waddch(all_win->status_win, ACS_HLINE);
					waddch(all_win->status_win, ACS_HLINE);
				}else {
					waddch(all_win->status_win, tab_pool[dummy].llpad);
					for(dummy2 = 0; dummy2 < strlen(tab_pool[dummy].label); dummy2++) waddch(all_win->status_win, ' ');
					waddch(all_win->status_win, tab_pool[dummy].lrpad);
				}
				for(dummy1 = 0; dummy1 < tab_pool[dummy].x; dummy1++) waddch(all_win->status_win, ACS_HLINE);
				x += (2+strlen(tab_pool[dummy].label)+tab_pool[dummy].x);
			}
			whline(all_win->status_win, ACS_HLINE, (all_win->status_win->_maxx-x));
			mvwaddch(all_win->status_win, 1, all_win->status_win->_maxx, ACS_URCORNER);
			dummy = 0;
			
			get_stat(all_win, card_pool, tab_pool);
			
			wrefresh(all_win->status_win);
			wrefresh(all_win->cardlist);
			refresh();
		break;

		case KEY_LEFT:
			if(lock == LOCK_CARDS) break;
			if(lock == NO_LOCK){
				wattrset(all_win->cardlist, CARDLIST_NOFOCUS);
				box(all_win->cardlist,0,0);
			    mvwaddstr(all_win->cardlist, 0,1, "Detected cards");
			}

			wattrset(all_win->status_win, A_BOLD|COLOR_PAIR(BLUE_WHITE)); 
			box(all_win->status_win, 0, 0);
			

			if(lock == LOCK_CMD){
				tab_pool[tab_focus].focus = 0;
				tab_pool[1].focus = '0';
			}else if(lock == LOCK_DEV){
				tab_pool[tab_focus].focus = 0;
				tab_pool[2].focus = '0';
			}else {
				tab_pool[tab_focus].focus = 0;
				if(!tab_focus) tab_focus = (NO_TABS-1); else tab_focus--;
				tab_pool[tab_focus].focus = '0';
			}
			
			
			dummy = 0;
			wmove(all_win->status_win, 0,0);
			for(;dummy < NO_TABS; dummy++){

				if(!tab_pool[dummy].focus){
					wattrset(all_win->status_win, COLOR_PAIR(BLUE_BLACK)); 
					waddch(all_win->status_win, ' ');
					waddstr(all_win->status_win, tab_pool[dummy].label);
					waddch(all_win->status_win, ' ');
					wattrset(all_win->status_win, A_BOLD|COLOR_PAIR(BLUE_WHITE));


				}else{
					wattrset(all_win->status_win, A_BOLD|COLOR_PAIR(BLUE_WHITE));
					waddch(all_win->status_win, tab_pool[dummy].lpad);
					waddstr(all_win->status_win, tab_pool[dummy].label);
					waddch(all_win->status_win, tab_pool[dummy].rpad);
				}
				for(dummy1 = 0; dummy1 < tab_pool[dummy].x; dummy1++) waddch(all_win->status_win, ' ');
				x += (2+strlen(tab_pool[dummy].label)+tab_pool[dummy].x);
			}
			whline(all_win->status_win, ' ', (all_win->status_win->_maxx-x)+1);

			dummy = 0;
			x = 0;
			wmove(all_win->status_win, 1,0);
			for(;dummy < NO_TABS; dummy++){
				if(!tab_pool[dummy].focus){
					if(dummy) waddch(all_win->status_win, ACS_HLINE);
					else waddch(all_win->status_win, ACS_ULCORNER);
					for(dummy2 = 0; dummy2 < strlen(tab_pool[dummy].label); dummy2++) waddch(all_win->status_win, ACS_HLINE);
					waddch(all_win->status_win, ACS_HLINE);
				}else {
					waddch(all_win->status_win, tab_pool[dummy].llpad);
					for(dummy2 = 0; dummy2 < strlen(tab_pool[dummy].label); dummy2++) waddch(all_win->status_win, ' ');
					waddch(all_win->status_win, tab_pool[dummy].lrpad);
				}
				for(dummy1 = 0; dummy1 < tab_pool[dummy].x; dummy1++) waddch(all_win->status_win, ACS_HLINE);
				x += (2+strlen(tab_pool[dummy].label)+tab_pool[dummy].x);
			}
			whline(all_win->status_win, ACS_HLINE, (all_win->status_win->_maxx-x));
			mvwaddch(all_win->status_win, 1, all_win->status_win->_maxx, ACS_URCORNER);

			get_stat(all_win, card_pool, tab_pool);

			wrefresh(all_win->status_win);
			wrefresh(all_win->cardlist);
			refresh();
		break;


	}
}

/*
--------------------------------------------------------------------------------
	Build the card pool from the extracted information*/
void init_matrix(CARD *card_pool, TAB *tab_pool, int no_cards){
	int dummy = 0, pad_x = 2, pad_y = 2;

	for(;dummy < no_cards; dummy++){
		card_pool[dummy].x = pad_x;
		card_pool[dummy].y = pad_y;
		pad_y += 2;
	}
	dummy = 0;
	for(;dummy < NO_TABS; dummy++){
		tab_pool[dummy].x = 3;
	}
}

int init_card_pool(CARD *card_pool, TAB *tab_pool, int no_cards){
	card_pool[0].focus = '0';
	init_matrix(card_pool, tab_pool, no_cards);
return 1;
}

void init_tab_pool(CARD *card_pool, TAB *tab_pool, int no_cards){
	int dummy = 0;
	
	for(;dummy < NO_TABS; dummy++){
		if(dummy == 0){
			strcpy(tab_pool[dummy].label, "Status");
			tab_pool[dummy].focus = '0';
			tab_pool[dummy].lpad = ACS_ULCORNER;
			tab_pool[dummy].rpad = ACS_URCORNER;
			tab_pool[dummy].llpad = ACS_VLINE;
			tab_pool[dummy].lrpad = ACS_LLCORNER;
		}
		
		if(dummy == 1){
			strcpy(tab_pool[dummy].label, "Devices");
			tab_pool[dummy].focus = 0;
			tab_pool[dummy].lpad = ACS_ULCORNER;
			tab_pool[dummy].rpad = ACS_URCORNER;
			tab_pool[dummy].llpad = ACS_LRCORNER;
			tab_pool[dummy].lrpad = ACS_LLCORNER;
		}

		if(dummy == 2){
			strcpy(tab_pool[dummy].label, "Arrays");
			tab_pool[dummy].focus = 0;
			tab_pool[dummy].lpad = ACS_ULCORNER;
			tab_pool[dummy].rpad = ACS_URCORNER;
			tab_pool[dummy].llpad = ACS_LRCORNER;
			tab_pool[dummy].lrpad = ACS_LLCORNER;
		}

		if(dummy == 3){
			strcpy(tab_pool[dummy].label, "Expert");
			tab_pool[dummy].focus = 0;
			tab_pool[dummy].lpad = ACS_ULCORNER;
			tab_pool[dummy].rpad = ACS_URCORNER;
			tab_pool[dummy].llpad = ACS_LRCORNER;
			tab_pool[dummy].lrpad = ACS_LLCORNER;
		}

	}
	init_matrix(card_pool, tab_pool, no_cards);
}


/*
--------------------------------------------------------------------------------
	Trash UI at the end or in case of failure*/
void trash_UI(SWINDOWS *all_win, int fail_stat){
   
  switch(fail_stat)
  {
  case STATUS_WIN_FAIL:
	goto status_fail;
  case CARDLIST_FAIL:
	goto cardlist_fail;
  case TITLE_FAIL:
	goto title_fail;
  case BOTTOM_FAIL:
			goto bottom_fail;
  case TRASH_ALL:
	goto trash_all;
  case INIT_CARD_FAIL:
	printf("\nDriver not compatible.\n");
	goto init_card_fail;
  }
  
 trash_all:
  werase(all_win->status_win);
  delwin(all_win->status_win);
 status_fail:
  werase(all_win->cardlist);
  delwin(all_win->cardlist);
 cardlist_fail:
  werase(all_win->title);
  delwin(all_win->title);
 title_fail:
  werase(all_win->bottom);
  delwin(all_win->bottom);
 bottom_fail:
  werase(all_win->root);
  refresh();
  delwin(all_win->root);
  endwin();
 init_card_fail:
  return;
}

/*
--------------------------------------------------------------------------------
	Build the user interface */
int build_UI(SWINDOWS *all_win, CARD *card_pool, TAB *tab_pool, int no_cards){
	int dummy, fail_stat = 0;

	if(!(init_card_pool(card_pool, tab_pool, no_cards))) {
		fail_stat = INIT_CARD_FAIL;
		goto hyper;
	}

    all_win->root = initscr();
	if(LINES < 25 || COLS < 80){
		sprintf(err_msg, "Terminal size = %dx%d. Please resize your window to atleast 80x25.", COLS, LINES);
		trash_UI(all_win, BOTTOM_FAIL);
		return 0;
	}
	start_color();
	noecho();
        if(!has_colors()){
		fail_stat = BOTTOM_FAIL;
		goto hyper;
	}

	init_tab_pool(card_pool, tab_pool, no_cards);

	init_pair(YELLOW_GREEN, COLOR_GREEN, COLOR_YELLOW);
	init_pair(GREEN_BLACK, COLOR_BLACK, COLOR_GREEN);
	init_pair(BLUE_BLACK, COLOR_BLACK, COLOR_BLUE);
    init_pair(CYAN_BLACK, COLOR_BLACK, COLOR_CYAN);
    init_pair(RED_YELLOW, COLOR_YELLOW, COLOR_RED);
    init_pair(BLACK_YELLOW, COLOR_YELLOW, COLOR_BLACK);
    init_pair(WHITE_BLUE, COLOR_BLUE, COLOR_WHITE);
    init_pair(CYAN_WHITE, COLOR_WHITE, COLOR_CYAN);
    init_pair(CYAN_GREEN, COLOR_GREEN, COLOR_CYAN);
    init_pair(CYAN_RED, COLOR_RED, COLOR_CYAN);
    init_pair(CYAN_YELLOW, COLOR_YELLOW, COLOR_CYAN);
    init_pair(BLACK_WHITE, COLOR_WHITE, COLOR_BLACK);
    init_pair(BLUE_WHITE, COLOR_WHITE, COLOR_BLUE);
    init_pair(BLUE_YELLOW, COLOR_YELLOW, COLOR_BLUE);
    init_pair(BLUE_RED, COLOR_RED, COLOR_BLUE);
    init_pair(BLUE_CYAN, COLOR_CYAN, COLOR_BLUE);
    init_pair(BLUE_GREEN, COLOR_GREEN, COLOR_BLUE);
        
	if(!(all_win->bottom = subwin(all_win->root, 1, COLS, LINES-1, 0))){
		fail_stat = BOTTOM_FAIL;
		goto hyper;
	}
        if(!(all_win->title = subwin(all_win->root, 3, COLS, 0, 0))){
		fail_stat = TITLE_FAIL;
		goto hyper;
	}
        if(!(all_win->cardlist = subwin(all_win->root, LINES-6, 16, 4, 0))){
		fail_stat = CARDLIST_FAIL;
		goto hyper;
	}
        if(!(all_win->status_win = subwin(all_win->root, LINES-4, COLS-17, 3, 17))){
		fail_stat = STATUS_WIN_FAIL;
		goto hyper;
	}

	hyper:
		if(fail_stat != NO_WIN_FAIL){
			trash_UI(all_win, fail_stat);
			return 0;
		}
		else {
			wattrset(all_win->root, A_DIM|COLOR_PAIR(BLACK_WHITE));
       		wattrset(all_win->title, TITLE_NOFOCUS);
			wattrset(all_win->bottom, A_BOLD|COLOR_PAIR(RED_YELLOW));
			wattrset(all_win->cardlist,  CARDLIST_NOFOCUS);
			wattrset(all_win->status_win, A_BOLD|COLOR_PAIR(BLUE_WHITE));
			wmove(all_win->root,0,0); 
			for(dummy = 0; dummy < COLS*LINES ; dummy++) waddch(all_win->root,ACS_CKBOARD);
			
			wmove(all_win->title,0,0);
			for(dummy = 0; dummy < COLS*3 ; dummy++) waddch(all_win->title,' ');

			wmove(all_win->bottom,0,0);
			for(dummy = 0; dummy < COLS ; dummy++) waddch(all_win->bottom,' ');

			wmove(all_win->cardlist,0,0);
			for(dummy = 0; dummy <= ((all_win->cardlist->_maxx+1)*(all_win->cardlist->_maxy+1)) ; dummy++)
				 waddch(all_win->cardlist,' ');

			wmove(all_win->status_win,0,0);
			for(dummy = 0; dummy <= ((all_win->status_win->_maxx+1)*(all_win->status_win->_maxy+1)) ; dummy++)
				 waddch(all_win->status_win,' ');

			box(all_win->title,0,0);
			box(all_win->cardlist,0,0);
			box(all_win->status_win,0,0);

		        mvwaddstr(all_win->title, 0, ((COLS/2)-16), "=<| VA RAID Monitor (VARMon) |>="); 
		        mvwaddstr(all_win->title, 1, ((COLS/2)-25), "Ver. 1.2.0 by Julien Danjou <julien@danjou.info>"); 

		        mvwaddstr(all_win->bottom, 0, 1, "<ESC> - Exit | <R> - Refresh | <RETURN> - Lock");
		        mvwaddstr(all_win->cardlist, 0,1, "Detected cards");

			mv_fc(all_win, card_pool, tab_pool, KEY_INIT, no_pages);

		}
return 1;
}

/*
--------------------------------------------------------------------------------
	Get a snapshot of the status and put it in the SNAPSHOT array.
	This is the main parser.
*/
void get_snap(){

	char *offset, line[90];
	int result, cur_card, offset_c, ph_dev_c, lg_drv_c, i;
	
	

	for(cur_card = 0; cur_card < no_pages; cur_card++){
		ph_dev_c = 0;
		lg_drv_c = 0;
		snap_all[cur_card].got_dead = 0;
		while((result = get_line(line, snap_all[cur_card].card_path, 1))){

			/*Get driver version*/
			if((offset = strstr(line, "Driver Version"))){
				/*Position to the end of "Driver ..."*/
				offset += 15;
				offset_c = 0;

				while(offset[offset_c] != ' '){
					snap_all[cur_card].dr_ver[offset_c] = offset[offset_c];
					offset_c++;
				}
				snap_all[cur_card].dr_ver[offset_c] = 0;
			}

			/*Get author's name*/
			if((offset = strstr(line, "Copyright"))){
				offset = strstr(offset, "by ");
				offset += 3;
				offset_c = 0;

				while(offset[offset_c] != '<'){
					snap_all[cur_card].dr_au[offset_c] = offset[offset_c];
					offset_c++;
				}
				snap_all[cur_card].dr_au[(offset_c - 1)] = 0;
			}
			/*Get card type*/
			if((offset = strstr(line, "Configuring"))){
				offset = strstr(offset, "DAC");
				offset_c = 0;
// V2
// The better way would be get the string, not hard code it.
// The problem is that the LABEL on UI is too short for some controllers,
// so for now we code it this way.  Should re-visite this piece later. 
if (!offset) {
	char *h ;
	if ((h = strstr(line,"eXtremeRAID 2000")))
		strcpy(snap_all[cur_card].c_type, "eRAID 2000");
	if ((h = strstr(line,"eXtremeRAID 3000")))
		strcpy(snap_all[cur_card].c_type, "eRAID 3000");
	else if ((h = strstr(line,"352")))
		strcpy(snap_all[cur_card].c_type, "aRAID 352");
	else if ((h = strstr(line,"160")))
		strcpy(snap_all[cur_card].c_type, "aRAID 160");
	else if ((h = strstr(line,"170")))
		strcpy(snap_all[cur_card].c_type, "aRAID 170");
	else continue;
}
else {
// V2

				while(offset[offset_c] != ' '){
					snap_all[cur_card].c_type[offset_c] = offset[offset_c];
					offset_c++;
				}
				snap_all[cur_card].c_type[offset_c] = 0;
}
			}
			/*Get firmware version*/
			if((offset = strstr(line, "Firmware"))){
				offset = strstr(offset, ":");
				offset += 2;
				offset_c = 0;
//V2
if (offset[0] >= '6')
	snap_all[cur_card].FirmwareType = 2;
//V2
				while(offset[offset_c] != ','){
					snap_all[cur_card].frmw[offset_c] = offset[offset_c];
					offset_c++;
				}
				snap_all[cur_card].frmw[offset_c] = 0;
			}

			/*Get number of channels on per card*/
			if((offset = strstr(line, "Channels"))){
				offset = strstr(offset, ":");
				offset += 2;
				offset_c = 0;

				while(offset[offset_c] != ','){
					snap_all[cur_card].chnl[offset_c] = offset[offset_c];
					offset_c++;
				}
				snap_all[cur_card].chnl[offset_c] = 0;
			}
			
			/*Get memory size per card*/
			if((offset = strstr(line, "Memory"))){
				offset = strstr(offset, ":");
				offset += 2;
				offset_c = 0;

				while(offset[offset_c] != 0){
					snap_all[cur_card].mem[offset_c] = offset[offset_c];
					offset_c++;
				}
				snap_all[cur_card].mem[offset_c] = 0;
			}
			
			/*Get PCI bus per card*/
			if((offset = strstr(line, "PCI Bus"))){
				offset = strstr(offset, ":");
				offset += 2;
				offset_c = 0;

				while(offset[offset_c] != ','){
					snap_all[cur_card].pci_bus[offset_c] = offset[offset_c];
					offset_c++;
				}
				snap_all[cur_card].pci_bus[offset_c] = 0;
			}
			
			/*Get device per card*/
			if((offset = strstr(line, "Device: "))){
				offset = strstr(offset, ":");
				offset += 2;
				offset_c = 0;

				while(offset[offset_c] != ','){
					snap_all[cur_card].device[offset_c] = offset[offset_c];
					offset_c++;
				}
				snap_all[cur_card].device[offset_c] = 0;
			}
			
			/*Get function per card*/
			if((offset = strstr(line, "Function:"))){
				offset = strstr(offset, ":");
				offset += 2;
				offset_c = 0;

				while(offset[offset_c] != ','){
					snap_all[cur_card].function[offset_c] = offset[offset_c];
					offset_c++;
				}
				snap_all[cur_card].function[offset_c] = 0;
			}
			
			/*Get I/O address per card*/
			if((offset = strstr(line, "I/O Address:"))){
				offset = strstr(offset, ":");
				offset += 2;
				offset_c = 0;

				while(offset[offset_c] != 0){
					snap_all[cur_card].io_addr[offset_c] = offset[offset_c];
					offset_c++;
				}
				snap_all[cur_card].io_addr[offset_c] = 0;
			}
			
			/*Get PCI address per card*/
			if((offset = strstr(line, "PCI Address:"))){
				offset = strstr(offset, ":");
				offset += 2;
				offset_c = 0;

				while(offset[offset_c] != ' '){
					snap_all[cur_card].pci_addr[offset_c] = offset[offset_c];
					offset_c++;
				}
				snap_all[cur_card].pci_addr[offset_c] = 0;
			}

			/*Get memory mapped address per card*/
			if((offset = strstr(line, "mapped at"))){
				offset += 10;
				offset_c = 0;

				while(offset[offset_c] != ','){
					snap_all[cur_card].pci_mapped[offset_c] = offset[offset_c];
					offset_c++;
				}
				snap_all[cur_card].pci_mapped[offset_c] = 0;
			}
			
			/*Get IRQ per card*/
			if((offset = strstr(line, "IRQ Channel:"))){
				offset = strstr(offset, ":");
				offset += 2;
				offset_c = 0;

				while(offset[offset_c] != 0){
					snap_all[cur_card].irq[offset_c] = offset[offset_c];
					offset_c++;
				}
				snap_all[cur_card].irq[offset_c] = 0;
			}
		
			/*Get Controller Queue depth per card*/
			if((offset = strstr(line, "Controller Queue Depth:"))){
				offset = strstr(offset, ":");
				offset += 2;
				offset_c = 0;

				while(offset[offset_c] != ','){
					snap_all[cur_card].cq_depth[offset_c] = offset[offset_c];
					offset_c++;
				}
				snap_all[cur_card].cq_depth[offset_c] = 0;
			}
			
			/*Get Maximum Blocks per Command - per card*/
			if((offset = strstr(line, "Blocks per Command:"))){
				offset = strstr(offset, ":");
				offset += 2;
				offset_c = 0;

				while(offset[offset_c] != 0){
					snap_all[cur_card].max_block_com[offset_c] = offset[offset_c];
					offset_c++;
				}
				snap_all[cur_card].max_block_com[offset_c] = 0;
			}
			
			/*Get Driver Que depth per card*/
			if((offset = strstr(line, "Driver Queue Depth:"))){
				offset = strstr(offset, ":");
				offset += 2;
				offset_c = 0;

				while(offset[offset_c] != ','){
					snap_all[cur_card].dq_depth[offset_c] = offset[offset_c];
					offset_c++;
				}
				snap_all[cur_card].dq_depth[offset_c] = 0;
			}
			
			/*Get Max Scatter/Gather Segments per card*/
			if((offset = strstr(line, "Gather Segments:"))){
				offset = strstr(offset, ":");
				offset += 2;
				offset_c = 0;

				while(offset[offset_c] != 0){
					snap_all[cur_card].max_sg_seg[offset_c] = offset[offset_c];
					offset_c++;
				}
				snap_all[cur_card].max_sg_seg[offset_c] = 0;
			}
//V2  Scatter/Gather
			if((offset = strstr(line, "Gather Limit:"))){
				offset = strstr(offset, ":");
				offset += 2;
				offset_c = 0;
				while(offset[offset_c] != ' '){
					snap_all[cur_card].max_sg_seg[offset_c] = offset[offset_c];
					offset_c++;
				}
				snap_all[cur_card].max_sg_seg[offset_c] = 0;
			}
			
			/*Get Stripe Size per card*/
			if((offset = strstr(line, "Stripe Size:"))){
				offset = strstr(offset, ":");
				offset += 2;
				offset_c = 0;

				while(offset[offset_c] != ','){
					snap_all[cur_card].stripe_size[offset_c] = offset[offset_c];
					offset_c++;
				}
				snap_all[cur_card].stripe_size[offset_c] = 0;
			}

			/*Get Segment Size per card*/
			if((offset = strstr(line, "Segment Size:"))){
				offset = strstr(offset, ":");
				offset += 2;
				offset_c = 0;
//V2
				if (IS_FIRMWARE_LEVEL_2(cur_card)) {
				  strcpy(snap_all[cur_card].seg_size,offset);
				}
				else {
//V2
				while(offset[offset_c] != ','){
					snap_all[cur_card].seg_size[offset_c] = offset[offset_c];
					offset_c++;
				}
				snap_all[cur_card].seg_size[offset_c] = 0;
				}
			}
			/*Get Segment Size per card*/
			if((offset = strstr(line, "BIOS Geometry:"))){
				offset = strstr(offset, ":");
				offset += 2;
				offset_c = 0;

				while(offset[offset_c] != 0){
					snap_all[cur_card].bios_geo[offset_c] = offset[offset_c];
					offset_c++;
				}
				snap_all[cur_card].bios_geo[offset_c] = 0;
			}

			/*Get SAF-TE Fault per card*/
			if((offset = strstr(line, "SAF-TE"))){
				if((offset = strstr(line, "Enabled")))
					snap_all[cur_card].fault_mngmt = 1;
				else
					snap_all[cur_card].fault_mngmt = 0;
			}
			
			/*Get Physical Devices per card*/
			if((offset = strstr(line, "Vendor:"))){
				if((offset = strstr(line, ":"))){
					offset--;
					offset_c = 0;
					while(offset[offset_c] != ' '){
						snap_all[cur_card].ph_dev[ph_dev_c].target[offset_c] = offset[offset_c];
						offset_c++;
					}
					snap_all[cur_card].ph_dev[ph_dev_c].target[offset_c] = 0;
				}
				if((offset = strstr(line, "VALinux")) || (offset = strstr(line, "VA Linux"))){
					if((offset = strstr(line, "Fullon 2x2")))
						strcpy(snap_all[cur_card].ph_dev[ph_dev_c].state, "Fullon 2x2");
					else if((offset = strstr(line, "nexStor")))
						strcpy(snap_all[cur_card].ph_dev[ph_dev_c].state, "nexStor");
					ph_dev_c++;
				}else if((offset = strstr(line, "ESG-SHV"))){
					strcpy(snap_all[cur_card].ph_dev[ph_dev_c].state, "SCA HSBP M6");
					ph_dev_c++;
				}
			}
			if((offset = strstr(line, "Disk"))){
				if(!lock && !ph_dev_c)
					snap_all[cur_card].ph_dev[ph_dev_c].focus = '0';
				else if(!lock)
					snap_all[cur_card].ph_dev[ph_dev_c].focus = 0;

				if((offset = strstr(line, "Online"))){
					strcpy(snap_all[cur_card].ph_dev[ph_dev_c].state, "ONL");
				}

				if((offset = strstr(line, "Dead"))){
					strcpy(snap_all[cur_card].ph_dev[ph_dev_c].state, "DED*");
				}

				if((offset = strstr(line, "Standby"))){
					strcpy(snap_all[cur_card].ph_dev[ph_dev_c].state, "SBY");
				}

				if((offset = strstr(line, "Write-Only"))){
					strcpy(snap_all[cur_card].ph_dev[ph_dev_c].state, "WON");
				}
				
				if((offset = strstr(line, ","))){
					offset += 2;
					offset_c = 0;
					while(offset[offset_c] != ' '){
						snap_all[cur_card].ph_dev[ph_dev_c].blocks[offset_c] = offset[offset_c];
						offset_c++;
					}
					snap_all[cur_card].ph_dev[ph_dev_c].blocks[offset_c] = 0;
				}
				
				if((offset = strstr(line, "Disk:"))){
					offset = strstr(line, ":");
					offset--;
					offset_c = 0;
					while(offset[offset_c] != ' '){
						snap_all[cur_card].ph_dev[ph_dev_c].target[offset_c] = offset[offset_c];
						offset_c++;
					}
					snap_all[cur_card].ph_dev[ph_dev_c++].target[offset_c] = 0;
				}else if((offset = strstr(line, "Disk Status")))
					ph_dev_c++;
			};
			

			/*Get Logical Devices per card*/
			if((offset = strstr(line, "RAID-"))){

				if(!lock && !lg_drv_c)
					snap_all[cur_card].lg_drv[lg_drv_c].focus = '0';
				else if(!lock)
					snap_all[cur_card].lg_drv[lg_drv_c].focus = 0;

				offset_c = 0;
				while(offset[offset_c] != ','){
					snap_all[cur_card].lg_drv[lg_drv_c].raid_l[offset_c] = offset[offset_c];
					offset_c++;
				}
				snap_all[cur_card].lg_drv[lg_drv_c].raid_l[offset_c] = 0;
				
				if((offset = strstr(line, "Online"))){
					strcpy(snap_all[cur_card].lg_drv[lg_drv_c].state, "ONL");
				}

				if((offset = strstr(line, "Offline"))){
					strcpy(snap_all[cur_card].lg_drv[lg_drv_c].state, "OFL*");
					snap_all[cur_card].got_dead = 2;
				}

				if((offset = strstr(line, "Critical"))){
					strcpy(snap_all[cur_card].lg_drv[lg_drv_c].state, "CRT*");
					if(!snap_all[cur_card].got_dead) snap_all[cur_card].got_dead = 1;
				}
				
				if((offset = strstr(line, ","))){
					offset++;
					if((offset = strstr(offset, ","))){
						offset_c = 0;
						offset += 2;
						while(offset[offset_c] != ' '){
							snap_all[cur_card].lg_drv[lg_drv_c].blocks[offset_c] = offset[offset_c];
							offset_c++;
						}
						snap_all[cur_card].lg_drv[lg_drv_c].blocks[offset_c] = 0;
					}
				}

				if((offset = strstr(line, "Write Thru"))){
					strcpy(snap_all[cur_card].lg_drv[lg_drv_c].wrt_opt, "WT");
				}
				
				if((offset = strstr(line, "Write Back"))){
					strcpy(snap_all[cur_card].lg_drv[lg_drv_c].wrt_opt, "WB");
				}

				if((offset = strstr(line, "/"))){
					offset_c = 0;
					while(offset[offset_c] != ':'){
						snap_all[cur_card].lg_drv[lg_drv_c].dev_name[offset_c] = offset[offset_c];
						offset_c++;
					}
					snap_all[cur_card].lg_drv[lg_drv_c++].dev_name[offset_c] = 0;
				}
			}

			/*Check for Rebuild in progress*/
			if((offset = strstr(line, "Rebuild in Progress"))){
				snap_all[cur_card].rebuild = REBUILD;
				offset_c = 0;
				offset = index(line, '(');
				offset++;
				while(offset[offset_c] != ')'){
					snap_all[cur_card].rb_dev[offset_c] = offset[offset_c];
					offset_c++;
				}
				snap_all[cur_card].rb_dev[offset_c] = 0;

				offset_c = 0;
				offset = index(line, '%');
				while(*offset != ' ')
					offset--;
				offset++;
				while(offset[offset_c] != '%'){
					snap_all[cur_card].rb_done[offset_c] = offset[offset_c];
					offset_c++;
				}
				snap_all[cur_card].rb_done[offset_c] = 0;

			}else if((offset = strstr(line, "Consistency Check in Progress:"))){
			/*Check for Consistency Check in progress*/
				snap_all[cur_card].rebuild = CONSISTENCY;
				offset_c = 0;
				offset = index(line, '(');
				offset++;

				while(offset[offset_c] != ')'){
					snap_all[cur_card].rb_dev[offset_c] = offset[offset_c];
					offset_c++;
				}
				snap_all[cur_card].rb_dev[offset_c] = 0;

				offset_c = 0;
				offset = index(line, '%');
				while(*offset != ' ')
					offset--;
				offset++;
				while(offset[offset_c] != '%'){
					snap_all[cur_card].rb_done[offset_c] = offset[offset_c];
					offset_c++;
				}
				snap_all[cur_card].rb_done[offset_c] = 0;
				
			}else{
				snap_all[cur_card].rebuild = NO_REBUILD;
				snap_all[cur_card].rb_dev[0] = 0;
				snap_all[cur_card].rb_done[0] = 0;
			}
		}
		
		
		for(i = 0; i < snap_all[cur_card].safte.no_bp; i++){
			get_backplane_info(DEVICES, &snap_all[cur_card].safte, cur_card, i);
			get_backplane_info(TEMPERATURE, &snap_all[cur_card].safte, cur_card, i);
		}

	}
	uname(&OS);
}

/*
--------------------------------------------------------------------------------
This function counts the number of cards in the system and sets the global
value "no_pages", it can be called to either count the number of cards or
to set global paths to all the cards in the SNAPSHOT array.
I am using recursion here because there is not a lot of processing
done in this function.
*/
void count_cards(DIR *meet_p){
	int no_cards = 0;
	char *offset, line[90];
	static char count = 2;	/*Did we call our selves twice?*/
	struct dirent *entry;
	char reverse;

/*	If our work here is done*/	
	if(!count) return;

/*	Rewind that dir*/	
	rewinddir(meet_p);

	while((entry = readdir(meet_p))){
		if(!(strcmp(CARD0, entry->d_name)) || !(strcmp(CARD1, entry->d_name)) || !(strcmp(CARD2, entry->d_name)) ||
		!(strcmp(CARD3, entry->d_name)) || !(strcmp(CARD4, entry->d_name)) || !(strcmp(CARD5, entry->d_name)) ||
		!(strcmp(CARD6, entry->d_name)) || !(strcmp(CARD7, entry->d_name))){

			/*Fill in the paths for the SNAPSHOT array*/
			if(count == 1){
				reverse = entry->d_name[1];
				reverse -= '0';
				/*no_cards*/
				strcpy(snap_all[(int)reverse].card_path, MEET_POINT);
				strcat(snap_all[(int)reverse].card_path, entry->d_name);
				strcat(snap_all[(int)reverse].card_path, "/current_status");
			}

			no_cards++;
		}
			
	}


/*	Tell the size of an SNAPSHOT array needed for all
	the cards in the system, If we entered this function
 	for counting afcourse*/
	if(count == 2){
			int i;
	       	no_pages = no_cards;

		if(!(snap_all =  calloc(no_pages, sizeof(SNAPSHOT)))){
			printf("\nSnap_all: Not enough memory.\n\n");
			exit(3);
		};

		/*Make the pointers in SNAPSHOT point to null*/
		for(no_cards = 0; no_cards < no_pages; no_cards++){
			snap_all[no_cards].ph_dev = 0;
			snap_all[no_cards].lg_drv = 0;
			snap_all[no_cards].no_ph_dev = 0;
			snap_all[no_cards].no_lg_drv = 0;
		}
		
		/*Fill in the command structure*/
		for(i = 0; i < 6; i++){
			if(!i) cmd[i].focus = '0';
			else cmd[i].focus = 0;
		}
		i = 0;
		strcpy(cmd[i++].name, "Kill");
		strcpy(cmd[i++].name, "Online");
		strcpy(cmd[i++].name, "Standby");
		strcpy(cmd[i++].name, "Rebuild");
		strcpy(cmd[i++].name, "Consistency");
		strcpy(cmd[i++].name, "Cancel");

	}
	
/*	After the SNAPSHOT array is initialized fill it in.
 	do not move this upwards to the upper (if count == 1)
 	because that is in the while loop*/
	if(count == 1){

		/*Count Physical && Logical devices*/	
		for(no_cards = 0; no_cards < no_pages; no_cards++){
			/*Count Physica && Logical devices per SNAPSHOT*/
			while(get_line(line, snap_all[no_cards].card_path, 1)){
				
				/*Get Physical Devices per card
				be compatible acros driver versions*/
				if((offset = strstr(line, "Disk:")) || (offset = strstr(line, "Vendor:"))){
					snap_all[no_cards].no_ph_dev++;
				}
				/*Get Logical Devices per card*/
				if((offset = strstr(line, "RAID-"))){
					snap_all[no_cards].no_lg_drv++;
				}

			}
			
			/*Allocate memory for the counted physical devices*/
			if(!(snap_all[no_cards].ph_dev =  calloc(snap_all[no_cards].no_ph_dev, sizeof(PHY_DEV)))){
				printf("\nPhysical Devices: Not enough memory.\n\n");
				exit(3);
			}
			

			/*Allocate memory for the counted logical devices*/
			if(!(snap_all[no_cards].lg_drv =  calloc(snap_all[no_cards].no_lg_drv, sizeof(LOG_DRIV)))){
				printf("\nLogical Devices: Not enough memory.\n\n");
				exit(3);
			}
			
		}

		get_snap();
	}

	count--;

	count_cards(meet_p);
}

void init_path(CARD * card_pool, int no_cards){
	int dummy = 0;
	for(;dummy < no_cards; dummy++){
		strcpy(card_pool[dummy].path, MEET_POINT);
		card_pool[dummy].path[(sizeof(MEET_POINT)-1)] = 'c';
		card_pool[dummy].path[(sizeof(MEET_POINT))] = (dummy+'0');
		card_pool[dummy].path[(sizeof(MEET_POINT)+1)] = '\0';
	}
}


void clean_up(){
	int count;
	
	printf("\n%s\n", err_msg);
	
	printf("\n-=<| VARMon ¡¿ |>=- Starting clean up procedure ... 8^0\n");

	if(!snap_all)
		printf("-=<| VARMon ¡¿ |>=- Nothing to clean. 8^\\\n");
	else{
	
		/*Free the memory for Logical and Physical arrays per card*/
		for(count = 0; count < no_pages; count++){

			if(snap_all[count].ph_dev)
				free(snap_all[count].ph_dev);
			
			if(snap_all[count].lg_drv)
				free(snap_all[count].lg_drv);
			
		}
		
		free(snap_all);
	}
	
	printf("\n-=<| VARMon ¡¿ |>=- Done. Clean exit reached 8^)\n\n");
}

/*
--------------------------------------------------------------------------------
	Let there be varmon*/
int main(int argc, char **argv){
		
	SWINDOWS all_win;
  	int key_press;
	DIR *meet_p;
	CARD *card_pool;
	TAB *tab_pool;

	if(getuid()){
		printf("\nMust be root to run varmon.\n\n");
		exit(1);
	}
	
	if(argc > 1)
		if(!strcmp(argv[1], "-b")){
			printf("\n\tVarmon sends an invalid command to the DAC driver,\n"
					"\tcausing the driver to reset it's command buffer.\n"
					"\tThis is nothing to worry about.\n\n");
			exit(1);
		}
	
	if(!(meet_p = opendir(MEET_POINT))){
		printf("\nDAC driver not loaded. Exiting.\n\n");
		exit(1);
	}
	
	atexit(&clean_up);
	
	count_cards(meet_p);	/*Count the number of cards
							 and fill in the SNAPSHOT paths*/
	closedir(meet_p);
	
	printf("\nScanning for VA safety backplane.\n"
			"Please wait a few moments...");
	detect_backplane();
	printf("\tDone.\n");
	
	
	if(!(card_pool =  calloc(no_pages, sizeof(CARD)))){
		printf("\nCard_pool: Not enough memory.\n\n");
		exit(2);
	};
	if(!(tab_pool =  calloc(NO_TABS, sizeof(TAB)))){
		printf("\nTab_pool: Not enough memory.\n\n");
		exit(2);
	}
	init_path(card_pool, no_pages);

	if(!(build_UI(&all_win, card_pool, tab_pool, no_pages))) goto fall_back;
	curs_set(0);
	keypad(all_win.root,TRUE);
	halfdelay(10);
	while ((key_press = wgetch(all_win.root))){
		if((key_press == KEY_ESCPE) && (lock == NO_LOCK)) break;
		else if((key_press == KEY_ESCPE) && (lock == LOCK_CARDS))
			lock = NO_LOCK;
			
		if(key_press == KEY_r || key_press == KEY_R){
			key_press = KEY_INIT;
			clearok(all_win.root, TRUE);
			get_snap();
		}
		if(key_press == ERR){
			key_press = KEY_INIT;
			get_snap();
		}

		mv_fc(&all_win, card_pool, tab_pool, key_press, no_pages);
		refresh();
		clearok(all_win.root, FALSE);
	}
	trash_UI(&all_win,TRASH_ALL);
	fall_back:
	free(card_pool);
	free(tab_pool);
return 0;
}

