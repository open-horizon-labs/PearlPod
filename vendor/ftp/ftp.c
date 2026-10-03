/*
 * This file is part of the MicroPython ESP32 project, https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2018 LoBo (https://github.com/loboris)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
/*
 * This file is based on 'ftp' from Pycom Limited.
 *
 * Author: LoBo, loboris@gmail.com
 * Copyright (c) 2017, LoBo
 */

#include <stdio.h>
#include <inttypes.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#ifdef PEARL_FTP_HOST
#include "ftp_platform.h"
#else
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "dirent.h"
#include "esp_system.h"
//#include "esp_spi_flash.h" // ESP-IDF V4

#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "esp_wifi.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

//#include "freertos/semphr.h"

#endif

#include "ftp.h"



int ftp_buff_size = CONFIG_MICROPY_FTPSERVER_BUFFER_SIZE;
int ftp_timeout = FTP_CMD_TIMEOUT_MS;
const char *FTP_TAG = "[Ftp]";
const char *MOUNT_POINT = "/sdcard/music";

static uint8_t ftp_stop = 0;

/******************************************************************************
 DECLARE PRIVATE DATA
 ******************************************************************************/
static ftp_data_t ftp_data = {0};
#include <stdatomic.h>
#include "async_writer.h"
static pearl_writer *writer;
static bool discard_file;
static bool async_file;
static char last_writer_trace[256];
#ifdef PEARL_FTP_HOST
#include <time.h>
extern bool pearl_trace_ram_sink(void);
static uint64_t trace_clock(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000; }
#else
#include "esp_timer.h"
#include "tracer.h"
static uint64_t trace_clock(void) { return esp_timer_get_time(); }
#endif
static atomic_int trace_state,trace_substate;
static atomic_uint reply_count,last_reply;
static atomic_uint received_bytes,receive_calls,receive_us,receive_max_us,write_calls,write_us,write_max_us,flush_us,empty_reads;
static bool file_failed;
static void *file_buffer;
static void trace_max(atomic_uint *value,unsigned n) { if(n>atomic_load(value))atomic_store(value,n); }
unsigned pearl_ftp_received_bytes(void) { return atomic_load(&received_bytes); }
void pearl_ftp_reset_progress(void) {
 received_bytes=receive_calls=receive_us=receive_max_us=write_calls=write_us=write_max_us=flush_us=empty_reads=reply_count=last_reply=0;
}
void pearl_ftp_trace(char *out,unsigned size) {
 snprintf(out,size,"state=%d substate=%d replies=%u last_reply=%u bytes=%u reads=%u recv_us=%u recv_max_us=%u empty=%u writes=%u write_us=%u write_max_us=%u flush_us=%u",atomic_load(&trace_state),atomic_load(&trace_substate),atomic_load(&reply_count),atomic_load(&last_reply),atomic_load(&received_bytes),atomic_load(&receive_calls),atomic_load(&receive_us),atomic_load(&receive_max_us),atomic_load(&empty_reads),atomic_load(&write_calls),atomic_load(&write_us),atomic_load(&write_max_us),atomic_load(&flush_us));
 unsigned used=strlen(out);
 if(used+1<size){out[used++]=' ';if(writer)pearl_writer_trace(writer,out+used,size-used);else snprintf(out+used,size-used,"%s",last_writer_trace);}
}
static char *ftp_path = NULL;
static char *ftp_scratch_buffer = NULL;;
static char *ftp_cmd_buffer = NULL;
static char listing_path[256];
static uint8_t ftp_nlist = 0;
static const ftp_cmd_t ftp_cmd_table[] = { { "FEAT" }, { "SYST" }, { "CDUP" }, { "CWD"	},
										   { "PWD"	}, { "XPWD" }, { "SIZE" }, { "MDTM" },
										   { "TYPE" }, { "USER" }, { "PASS" }, { "PASV" },
										   { "LIST" }, { "RETR" }, { "STOR" }, { "DELE" },
										   { "RMD"	}, { "MKD"	}, { "RNFR" }, { "RNTO" },
										   { "NOOP" }, { "QUIT" }, { "APPE" }, { "NLST" } };

// ==== PRIVATE FUNCTIONS ===================================================

uint64_t mp_hal_ticks_ms() {
	uint64_t time_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
	return time_ms;
}

//--------------------------------
static void stoupper (char *str) {
	while (str && *str != '\0') {
		*str = (char)toupper((int)(*str));
		str++;
	}
}

// ==== File functions =========================================

//--------------------------------------------------------------
static bool ftp_open_file (const char *path, const char *mode) {
    discard_file=pearl_trace_ram_sink()&&mode[0]=='w'&&!strcmp(path,"/.pearl/.bench-ram");
    if(discard_file){ftp_data.fp=NULL;async_file=false;file_failed=false;ftp_data.e_open=E_FTP_FILE_OPEN;return true;}
	ESP_LOGI(FTP_TAG, "ftp_open_file: path=[%s]", path);
	char fullname[256];
	if(snprintf(fullname,sizeof(fullname),"%s%s",MOUNT_POINT,path)>=(int)sizeof(fullname))return E_FTP_RESULT_FAILED;
    snprintf(listing_path,sizeof(listing_path),"%s",fullname);
	ESP_LOGI(FTP_TAG, "ftp_open_file: fullname=[%s]", fullname);
	//ftp_data.fp = fopen(path, mode);
	ftp_data.fp = fopen(fullname, mode);
	if (ftp_data.fp == NULL) {
		ESP_LOGE(FTP_TAG, "ftp_open_file: open fail [%s]", fullname);
		return false;
	}
    /* Explicit PSRAM buffering batches small TCP reads into large FAT writes. */
    if(setvbuf(ftp_data.fp,file_buffer,_IOFBF,32768)!=0) {
        fclose(ftp_data.fp);ftp_data.fp=NULL;return false;
    }
    file_failed=false;async_file=mode[0]!='r';
    if(async_file&&!pearl_writer_begin(writer,ftp_data.fp)){fclose(ftp_data.fp);ftp_data.fp=NULL;async_file=false;return false;}
	ftp_data.e_open = E_FTP_FILE_OPEN;
	return true;
}

//--------------------------------------
static void ftp_close_files_dir (void) {
	if (ftp_data.e_open == E_FTP_FILE_OPEN) {
        uint64_t before=trace_clock();
        if(async_file){if(!pearl_writer_finish(writer,true))file_failed=true;async_file=false;ftp_data.fp=NULL;}
        if(ftp_data.fp&&fclose(ftp_data.fp)!=0)file_failed=true;
        atomic_fetch_add(&flush_us,(unsigned)(trace_clock()-before));
		ftp_data.fp = NULL;
	}
	else if (ftp_data.e_open == E_FTP_DIR_OPEN) {
		closedir(ftp_data.dp);
		ftp_data.dp = NULL;
	}
	ftp_data.e_open = E_FTP_NOTHING_OPEN;
}

//------------------------------------------------
static void ftp_close_filesystem_on_error (void) {
	ftp_close_files_dir();
	if (ftp_data.fp) {
		fclose(ftp_data.fp);
		ftp_data.fp = NULL;
	}
	if (ftp_data.dp) {
		closedir(ftp_data.dp);
		ftp_data.dp = NULL;
	}
}

//---------------------------------------------------------------------------------------------
static ftp_result_t ftp_read_file (char *filebuf, uint32_t desiredsize, uint32_t *actualsize) {
	ftp_result_t result = E_FTP_RESULT_CONTINUE;
	*actualsize = fread(filebuf, 1, desiredsize, ftp_data.fp);
	if (*actualsize == 0) {
		if (feof(ftp_data.fp))
			result = E_FTP_RESULT_OK;
		else
			result = E_FTP_RESULT_FAILED;
		ftp_close_files_dir();
	} else if (*actualsize < desiredsize) {
		ftp_close_files_dir();
		result = E_FTP_RESULT_OK;
	}
	return result;
}

//-----------------------------------------------------------------
static ftp_result_t ftp_write_file(char *filebuf,uint32_t size){
 uint64_t before=trace_clock();
 bool queued=discard_file||pearl_writer_append(writer,filebuf,size);
 unsigned duration=trace_clock()-before;
 atomic_fetch_add(&write_calls,1);atomic_fetch_add(&write_us,duration);trace_max(&write_max_us,duration);
 if(queued){atomic_fetch_add(&received_bytes,size);return E_FTP_RESULT_OK;}
 return E_FTP_RESULT_FAILED;
}

//---------------------------------------------------------------
static ftp_result_t ftp_open_dir_for_listing (const char *path) {
	if (ftp_data.dp) {
		closedir(ftp_data.dp);
		ftp_data.dp = NULL;
	}
	ESP_LOGI(FTP_TAG, "ftp_open_dir_for_listing path=[%s] MOUNT_POINT=[%s]", path, MOUNT_POINT);
	char fullname[256];
	if(snprintf(fullname,sizeof(fullname),"%s%s",MOUNT_POINT,path)>=(int)sizeof(fullname))return E_FTP_RESULT_FAILED;
    snprintf(listing_path,sizeof(listing_path),"%s",fullname);
	ESP_LOGI(FTP_TAG, "ftp_open_dir_for_listing: %s", fullname);
	ftp_data.dp = opendir(fullname);  // Open the directory
	if (ftp_data.dp == NULL) {
		return E_FTP_RESULT_FAILED;
	}
	ftp_data.e_open = E_FTP_DIR_OPEN;
	ftp_data.listroot = false;
	return E_FTP_RESULT_CONTINUE;
}

//---------------------------------------------------------------------------------
static int ftp_get_eplf_item (char *dest, uint32_t destsize, struct dirent *de) {

	char *type = (de->d_type & DT_DIR) ? "d" : "-";

	// Get full file path needed for stat function
	char fullname[256];
	if(snprintf(fullname,sizeof(fullname),"%s/%s",listing_path,de->d_name)>=(int)sizeof(fullname))return 0;

	struct stat buf;
	int res = stat(fullname, &buf);
	ESP_LOGI(FTP_TAG, "ftp_get_eplf_item res=%d buf.st_size=%ld", res, buf.st_size);
	if (res < 0) {
		buf.st_size = 0;
		buf.st_mtime = 946684800; // Jan 1, 2000
	}

	char str_time[64];
	struct tm *tm_info;
	time_t now;
	if (time(&now) < 0) now = 946684800;	// get the current time from the RTC
	tm_info = localtime(&buf.st_mtime);		// get broken-down file time

	// if file is older than 180 days show dat,month,year else show month, day and time
	if ((buf.st_mtime + FTP_UNIX_SECONDS_180_DAYS) < now) strftime(str_time, sizeof(str_time), "%b %d %Y", tm_info);
	else strftime(str_time, 63, "%b %d %H:%M", tm_info);

    int addsize;
    if (ftp_nlist) addsize = snprintf(dest, destsize, "%s\r\n", de->d_name);
    else addsize = snprintf(dest, destsize, "%srw-rw-rw-   1 root  root %9"PRIu32" %s %s\r\n", type, (uint32_t)buf.st_size, str_time, de->d_name);
    if (addsize < 0 || (uint32_t)addsize >= destsize) return 0;

	return addsize;
}

#if 0
//---------------------------------------------------------------------------
static int ftp_get_eplf_drive (char *dest, uint32_t destsize, char *name) {
	char *type = "d";
	struct tm *tm_info;
	time_t seconds;
	time(&seconds); // get the time from the RTC
	tm_info = gmtime(&seconds);
	char str_time[64];
	strftime(str_time, 63, "%b %d %Y", tm_info);

	return snprintf(dest, destsize, "%srw-rw-rw-   1 root  root %9u %s %s\r\n", type, 0, str_time, name);
}
#endif

//--------------------------------------------------------------------------------------
static ftp_result_t ftp_list_dir(char *list, uint32_t maxlistsize, uint32_t *listsize) {
	uint next = 0;
	uint listcount = 0;
	ftp_result_t result = E_FTP_RESULT_CONTINUE;
	struct dirent *de;

	// read up to 8 directory items
	while (((maxlistsize - next) > 64) && (listcount < 8)) {
		de = readdir(ftp_data.dp);															// Read a directory item
		ESP_LOGI(FTP_TAG, "readdir de=%p", de);
		if (de == NULL) {
			result = E_FTP_RESULT_OK;
			break;																			// Break on error or end of dp
		}
		if (de->d_name[0] == '.' && de->d_name[1] == 0) continue;							// Ignore . entry
		if (de->d_name[0] == '.' && de->d_name[1] == '.' && de->d_name[2] == 0) continue;	// Ignore .. entry

		// add the entry to the list
		ESP_LOGI(FTP_TAG, "Add to dir list: %s", de->d_name);
		next += ftp_get_eplf_item((list + next), (maxlistsize - next), de);
		listcount++;
	}
	if (result == E_FTP_RESULT_OK) {
		ftp_close_files_dir();
	}
	*listsize = next;
	return result;
}

// ==== Socket functions ==============================================================

//------------------------------------
static void ftp_close_cmd_data(void) {
	closesocket(ftp_data.c_sd);
	closesocket(ftp_data.d_sd);
	ftp_data.c_sd  = -1;
	ftp_data.d_sd  = -1;
	ftp_close_filesystem_on_error ();
}

//----------------------------
static void _ftp_reset(void) {
	// close all connections and start all over again
	ESP_LOGW(FTP_TAG, "FTP RESET");
	closesocket(ftp_data.lc_sd);
	closesocket(ftp_data.ld_sd);
	ftp_data.lc_sd = -1;
	ftp_data.ld_sd = -1;
	ftp_close_cmd_data();

	ftp_data.e_open = E_FTP_NOTHING_OPEN;
	ftp_data.state = E_FTP_STE_START;
	ftp_data.substate = E_FTP_STE_SUB_DISCONNECTED;
}

//-------------------------------------------------------------------------------------
static bool ftp_create_listening_socket (int32_t *sd, uint32_t port, uint8_t backlog) {
	struct sockaddr_in sServerAddress;
	int32_t _sd;
	int32_t result;

	// open a socket for ftp data listen
	*sd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	_sd = *sd;

	if (_sd > 0) {
		// enable non-blocking mode
		uint32_t option = fcntl(_sd, F_GETFL, 0);
		option |= O_NONBLOCK;
		fcntl(_sd, F_SETFL, option);

		// enable address reusing
		option = 1;
		result = setsockopt(_sd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

		// bind the socket to a port number
		sServerAddress.sin_family = AF_INET;
		sServerAddress.sin_addr.s_addr = INADDR_ANY;
		sServerAddress.sin_len = sizeof(sServerAddress);
		sServerAddress.sin_port = htons(port);

		result |= bind(_sd, (const struct sockaddr *)&sServerAddress, sizeof(sServerAddress));

		// start listening
		result |= listen (_sd, backlog);

		if (!result) {
			return true;
		}
		closesocket(*sd);
	}
	return false;
}

//--------------------------------------------------------------------------------------------
static ftp_result_t ftp_wait_for_connection (int32_t l_sd, int32_t *n_sd, uint32_t *ip_addr) {
	struct sockaddr_in	sClientAddress;
	socklen_t  in_addrSize;

	// accepts a connection from a TCP client, if there is any, otherwise returns EAGAIN
	*n_sd = accept(l_sd, (struct sockaddr *)&sClientAddress, (socklen_t *)&in_addrSize);
	int32_t _sd = *n_sd;
	if (_sd < 0) {
		if (errno == EAGAIN) {
			return E_FTP_RESULT_CONTINUE;
		}
		// error
		_ftp_reset();
		return E_FTP_RESULT_FAILED;
	}

	if (ip_addr) {
		// check on which network interface the client was connected and save the IP address
		struct sockaddr_in clientAddr, serverAddr;
		in_addrSize = sizeof(struct sockaddr_in);
		getpeername(_sd, (struct sockaddr *)&clientAddr, (socklen_t *)&in_addrSize);
		getsockname(_sd, (struct sockaddr *)&serverAddr, (socklen_t *)&in_addrSize);
		ESP_LOGI(FTP_TAG, "Client IP: 0x%08"PRIx32, clientAddr.sin_addr.s_addr);
		ESP_LOGI(FTP_TAG, "Server IP: 0x%08"PRIx32, serverAddr.sin_addr.s_addr);
		*ip_addr = serverAddr.sin_addr.s_addr;
	}

	// enable non-blocking mode if not data channel connection
	uint32_t option = fcntl(_sd, F_GETFL, 0);
	option |= O_NONBLOCK; /* Data reads must not block cancellation or timeout checks. */
	fcntl(_sd, F_SETFL, option);

	// client connected, so go on
	return E_FTP_RESULT_OK;
}

//-----------------------------------------------------------
static void ftp_send_reply (uint32_t status, char *message) {
	if (!message) {
		message = "";
	}
	snprintf((char *)ftp_cmd_buffer, 4, "%"PRIu32, status);
	strcat ((char *)ftp_cmd_buffer, " ");
	strcat ((char *)ftp_cmd_buffer, message);
	strcat ((char *)ftp_cmd_buffer, "\r\n");

    atomic_fetch_add(&reply_count,1);last_reply=status;
	int32_t timeout = 200;
	ftp_result_t result;
	//uint32_t size = strlen((char *)ftp_cmd_buffer);
	size_t size = strlen((char *)ftp_cmd_buffer);

	ESP_LOGI(FTP_TAG, "Send reply: [%.*s]", size-2, ftp_cmd_buffer);

	while (1) {
		result = send(ftp_data.c_sd, ftp_cmd_buffer, size, 0);
		if (result == size) {
			if (status == 221) {
				closesocket(ftp_data.d_sd);
				ftp_data.d_sd = -1;
				closesocket(ftp_data.ld_sd);
				ftp_data.ld_sd = -1;
				closesocket(ftp_data.c_sd);
				ftp_data.substate = E_FTP_STE_SUB_DISCONNECTED;
				ftp_close_filesystem_on_error();
			}
			else if (status == 426 || status == 451 || status == 550) {
				closesocket(ftp_data.d_sd);
				ftp_data.d_sd = -1;
				ftp_close_filesystem_on_error();
			}
			ESP_LOGI(FTP_TAG, "Send reply: OK (%u)", size);
			break;
		}
		else {
			vTaskDelay(1);
			if ((timeout <= 0) || (errno != EAGAIN)) {
				// error
				_ftp_reset();
				ESP_LOGW(FTP_TAG, "Error sending command reply.");
				break;
			}
		}
		timeout -= portTICK_PERIOD_MS;
	}
}

//------------------------------------------
static void ftp_send_list(uint32_t datasize)
{
	int32_t timeout = 200;
	ftp_result_t result;

	ESP_LOGI(FTP_TAG, "Send list data: (%"PRIu32")", datasize);

	while (1) {
		result = send(ftp_data.d_sd, ftp_data.dBuffer, datasize, 0);
		if (result == datasize) {
			ESP_LOGI(FTP_TAG, "Send OK");
			break;
		}
		else {
			vTaskDelay(1);
			if ((timeout <= 0) || (errno != EAGAIN)) {
				// error
				_ftp_reset();
				ESP_LOGW(FTP_TAG, "Error sending list data.");
				break;
			}
		}
		timeout -= portTICK_PERIOD_MS;
	}
}

//-----------------------------------------------
static void ftp_send_file_data(uint32_t datasize)
{
	ftp_result_t result;
	uint32_t timeout = 200;

	ESP_LOGI(FTP_TAG, "Send file data: (%"PRIu32")", datasize);

	while (1) {
		result = send(ftp_data.d_sd, ftp_data.dBuffer, datasize, 0);
		if (result == datasize) {
			ESP_LOGI(FTP_TAG, "Send OK");
			break;
		}
		else {
			vTaskDelay(1);
			if ((timeout <= 0) || (errno != EAGAIN)) {
				// error
				_ftp_reset();
				ESP_LOGW(FTP_TAG, "Error sending file data.");
				break;
			}
		}
		timeout -= portTICK_PERIOD_MS;
	}
}

//------------------------------------------------------------------------------------------------
static ftp_result_t ftp_recv_non_blocking (int32_t sd, void *buff, int32_t Maxlen, int32_t *rxLen)
{
	if (sd < 0) return E_FTP_RESULT_FAILED;

    uint64_t before=trace_clock();
    *rxLen = recv(sd, buff, Maxlen, 0);
    if(sd==ftp_data.d_sd) {
        unsigned duration=trace_clock()-before;
        atomic_fetch_add(&receive_calls,1);atomic_fetch_add(&receive_us,duration);trace_max(&receive_max_us,duration);
        if(*rxLen<0 && (errno==EAGAIN||errno==EWOULDBLOCK))atomic_fetch_add(&empty_reads,1);
    }
    if (*rxLen > 0) return E_FTP_RESULT_OK;
    if (*rxLen == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) return E_FTP_RESULT_FAILED;

	return E_FTP_RESULT_CONTINUE;
}

// ==== Directory functions =======================

//-----------------------------------
#if 0
static void ftp_fix_path(char *pwd) {
	ESP_LOGI(FTP_TAG, "ftp_fix_path: pwd=[%s]", pwd);
	char ph_path[128];
	uint len = strlen(pwd);

	if (len == 0) {
		strcpy (pwd, "/");
	}
	else if ((len > 1) && (pwd[len-1] == '/')) pwd[len-1] = '\0';

	// Convert to physical path
	if (strstr(pwd, VFS_NATIVE_INTERNAL_MP) == pwd) {
		ESP_LOGI(FTP_TAG, "ftp_fix_path: VFS_NATIVE_INTERNAL_MP=[%s]", VFS_NATIVE_INTERNAL_MP);
		sprintf(ph_path, "%s%s", VFS_NATIVE_MOUNT_POINT, pwd+strlen(VFS_NATIVE_INTERNAL_MP));
		if (strcmp(ph_path, VFS_NATIVE_MOUNT_POINT) == 0) strcat(ph_path, "/");
		ESP_LOGI(FTP_TAG, "ftp_fix_path: ph_path=[%s]", ph_path);
		strcpy(pwd, ph_path);
	}
	else if (strstr(pwd, VFS_NATIVE_EXTERNAL_MP) == pwd) {
		ESP_LOGI(FTP_TAG, "ftp_fix_path: VFS_NATIVE_EXTERNAL_MP=[%s]", VFS_NATIVE_EXTERNAL_MP);
		sprintf(ph_path, "%s%s", VFS_NATIVE_SDCARD_MOUNT_POINT, pwd+strlen(VFS_NATIVE_EXTERNAL_MP));
		if (strcmp(ph_path, VFS_NATIVE_SDCARD_MOUNT_POINT) == 0) strcat(ph_path, "/");
		ESP_LOGI(FTP_TAG, "ftp_fix_path: ph_path=[%s]", ph_path);
		strcpy(pwd, ph_path);
	}
}
#endif


/*
 * Add directory or file name to the current path
 * Initially, pwd is set to "/" (file system root)
 * There are two possible entries in root:
 *	 "flash"	which can be fatfs or spiffs
 *	 "sd"		sd card
 * flash and sd entries have to be translated to their VFS names
 * trailing '/' is required for flash&sd root entries (translated)
 */
//-------------------------------------------------
static void ftp_open_child(char *pwd, char *dir) {
    char joined[256];
    int n=snprintf(joined,sizeof(joined),"%s%s%s",dir[0]=='/'?"":pwd,
                   dir[0]=='/'||!pwd[0]||pwd[strlen(pwd)-1]=='/'?"":"/",dir);
    if(n<0||n>=220||(strstr(joined,"/../")||!strcmp(joined,"/.."))){strcpy(pwd,"/invalid");return;}
    strcpy(pwd,joined);
}

// Return to parent directory
//---------------------------------------
static void ftp_close_child (char *pwd) {
	ESP_LOGI(FTP_TAG, "close_child: %s", pwd);
	uint len = strlen(pwd);
	if (pwd[len-1] == '/') {
		pwd[len-1] = '\0';
		len--;
		if ((len == 0) || (strcmp(pwd, VFS_NATIVE_MOUNT_POINT) == 0) || (strcmp(pwd, VFS_NATIVE_SDCARD_MOUNT_POINT) == 0)) {
			strcpy(pwd, "/");
		}
	}
	else {
		while (len) {
			if (pwd[len-1] == '/') {
				pwd[len-1] = '\0';
				len--;
				break;
			}
			len--;
		}

		if (len == 0) {
			strcpy (pwd, "/");
		}
		else if ((strcmp(pwd, VFS_NATIVE_MOUNT_POINT) == 0) || (strcmp(pwd, VFS_NATIVE_SDCARD_MOUNT_POINT) == 0)) {
			strcat(pwd, "/");
		}
	}
	ESP_LOGI(FTP_TAG, "close_child, New pwd: %s", pwd);
}

// Remove file name from path
//-----------------------------------------------------------
static void remove_fname_from_path (char *pwd, char *fname) {
	ESP_LOGI(FTP_TAG, "remove_fname_from_path: %s - %s", pwd, fname);
	if (strlen(fname) == 0) return;
	char *xpwd = strstr(pwd, fname);
	if (xpwd == NULL) return;

	xpwd[0] = '\0';
    if(!pwd[0])strcpy(pwd,"/");

#if 0
	ftp_fix_path(pwd);
#endif
	ESP_LOGI(FTP_TAG, "remove_fname_from_path: New pwd: %s", pwd);
}

// ==== Param functions =================================================

//------------------------------------------------------------------------------------------
static void ftp_pop_param(char **str, char *param, size_t maxlen, bool stop_on_space, bool stop_on_newline)
{
	char lastc = '\0';
	size_t copied = 0;
	while (**str != '\0') {
		if (stop_on_space && (**str == ' ')) break;
		if ((**str == '\r') || (**str == '\n')) {
			if (!stop_on_newline) {
				(*str)++;
				continue;
			}
			else break;
		}
		if ((**str == '/') && (lastc == '/')) {
			(*str)++;
			continue;
		}
		lastc = **str;
		if (copied + 1 < maxlen) {
			*param++ = **str;
			copied++;
		}
		(*str)++;
	}
	*param = '\0';
}

//--------------------------------------------------
static ftp_cmd_index_t ftp_pop_command(char **str) {
	char _cmd[FTP_CMD_SIZE_MAX];
	ftp_pop_param (str, _cmd, FTP_CMD_SIZE_MAX, true, true);
	stoupper (_cmd);
	for (ftp_cmd_index_t i = 0; i < E_FTP_NUM_FTP_CMDS; i++) {
		if (!strcmp (_cmd, ftp_cmd_table[i].cmd)) {
			// Skip an actual space; commands without parameters end at NUL.
			if (**str == ' ') (*str)++;
			return i;
		}
	}
	return E_FTP_CMD_NOT_SUPPORTED;
}

// Get file name from parameter and append to ftp_path
//-------------------------------------------------------
static void ftp_get_param_and_open_child(char **bufptr) {
#if 0
	ftp_pop_param(bufptr, ftp_scratch_buffer, FTP_MAX_PARAM_SIZE, false, false);
	ftp_open_child(ftp_path, ftp_scratch_buffer);
	ftp_data.closechild = true;
#endif
	// Optional commands are truncated
	do {
		ftp_pop_param(bufptr, ftp_scratch_buffer, FTP_MAX_PARAM_SIZE, false, false);
		if (ftp_scratch_buffer[0] == '\0') {
			return;
		}
	} while (ftp_scratch_buffer[0] == '-');
	ftp_open_child(ftp_path, ftp_scratch_buffer);
	ftp_data.closechild = true;
}

// ==== Ftp command processing =====

//----------------------------------
static void ftp_process_cmd (void) {
	int32_t len;
	char *bufptr = (char *)ftp_cmd_buffer;
	ftp_result_t result;
	struct stat buf;
	int res;

	memset(bufptr, 0, FTP_MAX_PARAM_SIZE + FTP_CMD_SIZE_MAX);
	ftp_data.closechild = false;

	// use the reply buffer to receive new commands
	result = ftp_recv_non_blocking(ftp_data.c_sd, ftp_cmd_buffer, FTP_MAX_PARAM_SIZE + FTP_CMD_SIZE_MAX, &len);
	if (result == E_FTP_RESULT_OK) {
		ftp_cmd_buffer[len] = '\0';
		// bufptr is moved as commands are being popped
		ftp_cmd_index_t cmd = ftp_pop_command(&bufptr);
		if ((cmd >= 0) && (cmd < E_FTP_NUM_FTP_CMDS)) {
			ESP_LOGI(FTP_TAG, "CMD: %s", ftp_cmd_table[cmd].cmd);
		}
		else {
			ESP_LOGI(FTP_TAG, "CMD: %d", cmd);
		}
        if (strlen(bufptr)>220 || (strstr(bufptr,"/../")||!strncmp(bufptr,"../",3)||!strcmp(bufptr,"..")) || strchr(bufptr,'\\')) {
            ftp_send_reply(550,"Invalid managed path");return;
        }
		char fullname[256];
		char fullname2[256];
		strcpy(fullname, MOUNT_POINT);
		strcpy(fullname2, MOUNT_POINT);

		switch (cmd) {
		case E_FTP_CMD_FEAT:
			ftp_send_reply(502, "no-features");
			break;
		case E_FTP_CMD_SYST:
			ftp_send_reply(215, "UNIX Type: L8");
			break;
		case E_FTP_CMD_CDUP:
			ftp_close_child(ftp_path);
			ftp_send_reply(250, NULL);
			break;
		case E_FTP_CMD_CWD:
			ftp_pop_param (&bufptr, ftp_scratch_buffer, FTP_MAX_PARAM_SIZE, false, false);

			if (strlen(ftp_scratch_buffer) > 0) {
				if ((ftp_scratch_buffer[0] == '.') && (ftp_scratch_buffer[1] == '\0')) {
					ftp_data.dp = NULL;
					ftp_send_reply(250, NULL);
					break;
				}
				if ((ftp_scratch_buffer[0] == '.') && (ftp_scratch_buffer[1] == '.') && (ftp_scratch_buffer[2] == '\0')) {
					ftp_close_child (ftp_path);
					ftp_send_reply(250, NULL);
					break;
				}
				else ftp_open_child (ftp_path, ftp_scratch_buffer);
			}

			if ((ftp_path[0] == '/') && (ftp_path[1] == '\0')) {
				ftp_data.dp = NULL;
				ftp_send_reply(250, NULL);
			}
			else {
				strcat(fullname, ftp_path);
				ESP_LOGI(FTP_TAG, "E_FTP_CMD_CWD fullname=[%s]", fullname);
				//ftp_data.dp = opendir(ftp_path);
				ftp_data.dp = opendir(fullname);
				if (ftp_data.dp != NULL) {
					closedir(ftp_data.dp);
					ftp_data.dp = NULL;
					ftp_send_reply(250, NULL);
				}
				else {
					ftp_close_child (ftp_path);
					ftp_send_reply(550, NULL);
				}
			}
			break;
		case E_FTP_CMD_PWD:
		case E_FTP_CMD_XPWD:
			{
				char lpath[256];
#if 0
				if (strstr(ftp_path, VFS_NATIVE_MOUNT_POINT) == ftp_path) {
					sprintf(lpath, "%s%s", VFS_NATIVE_INTERNAL_MP, ftp_path+strlen(VFS_NATIVE_MOUNT_POINT));
				}
				else if (strstr(ftp_path, VFS_NATIVE_SDCARD_MOUNT_POINT) == ftp_path) {
					sprintf(lpath, "%s%s", VFS_NATIVE_EXTERNAL_MP, ftp_path+strlen(VFS_NATIVE_SDCARD_MOUNT_POINT));
				}
				else strcpy(lpath,ftp_path);
#endif
				snprintf(lpath,sizeof(lpath),"\"%s\"",ftp_path);

				ftp_send_reply(257, lpath);
			}
			break;
		case E_FTP_CMD_SIZE:
			{
				ftp_get_param_and_open_child (&bufptr);
				strcat(fullname, ftp_path);
				ESP_LOGI(FTP_TAG, "E_FTP_CMD_SIZE fullname=[%s]", fullname);
				//int res = stat(ftp_path, &buf);
				int res = stat(fullname, &buf);
				if (res == 0) {
					// send the file size
					snprintf((char *)ftp_data.dBuffer, ftp_buff_size, "%"PRIu32, (uint32_t)buf.st_size);
					ftp_send_reply(213, (char *)ftp_data.dBuffer);
				} else {
					ftp_send_reply(550, NULL);
				}
			}
			break;
		case E_FTP_CMD_MDTM:
			ftp_get_param_and_open_child (&bufptr);
			strcat(fullname, ftp_path);
			ESP_LOGI(FTP_TAG, "E_FTP_CMD_MDTM fullname=[%s]", fullname);
			//res = stat(ftp_path, &buf);
			res = stat(fullname, &buf);
			if (res == 0) {
				// send the file modification time
				//snprintf((char *)ftp_data.dBuffer, ftp_buff_size, "%u", (uint32_t)buf.st_mtime);
				//snprintf((char *)ftp_data.dBuffer, ftp_buff_size, "20210212010203");
				//time_t time = buf.st_mtime + (CONFIG_LOCAL_TIMEZONE*60*60);
				time_t time = buf.st_mtime;
				struct tm *ptm = localtime(&time);
				//char buf[128];
				//strftime(buf, sizeof(buf), "%Y%m%d%H%M%S", ptm);
				strftime((char *)ftp_data.dBuffer, ftp_buff_size, "%Y%m%d%H%M%S", ptm);
				ESP_LOGI(FTP_TAG, "E_FTP_CMD_MDTM ftp_data.dBuffer=[%s]", ftp_data.dBuffer);
				ftp_send_reply(213, (char *)ftp_data.dBuffer);
			} else {
				ftp_send_reply(550, NULL);
			}
			break;
		case E_FTP_CMD_TYPE:
			ftp_send_reply(200, NULL);
			break;
        case E_FTP_CMD_USER: ftp_send_reply(331,"Anonymous sync");break;
        case E_FTP_CMD_PASS: ftp_send_reply(230,"Ready");break;
		case E_FTP_CMD_PASV:
			{
				// some servers (e.g. google chrome) send PASV several times very quickly
				closesocket(ftp_data.d_sd);
				ftp_data.d_sd = -1;
				ftp_data.substate = E_FTP_STE_SUB_DISCONNECTED;
				bool socketcreated = true;
				if (ftp_data.ld_sd < 0) {
					socketcreated = ftp_create_listening_socket(&ftp_data.ld_sd, FTP_PASIVE_DATA_PORT, FTP_DATA_CLIENTS_MAX - 1);
				}
				if (socketcreated) {
					uint8_t *pip = (uint8_t *)&ftp_data.ip_addr;
					ftp_data.dtimeout = 0;
					snprintf((char *)ftp_data.dBuffer, ftp_buff_size, "(%u,%u,%u,%u,%u,%u)",
							 pip[0], pip[1], pip[2], pip[3], (FTP_PASIVE_DATA_PORT >> 8), (FTP_PASIVE_DATA_PORT & 0xFF));
					ftp_data.substate = E_FTP_STE_SUB_LISTEN_FOR_DATA;
					ESP_LOGI(FTP_TAG, "Data socket created");
					ftp_send_reply(227, (char *)ftp_data.dBuffer);
				}
				else {
					ESP_LOGW(FTP_TAG, "Error creating data socket");
					ftp_send_reply(425, NULL);
				}
			}
			break;
		case E_FTP_CMD_LIST:
		case E_FTP_CMD_NLST:
			ftp_get_param_and_open_child(&bufptr);
			if (cmd == E_FTP_CMD_LIST) ftp_nlist = 0;
			else ftp_nlist = 1;
			if (ftp_open_dir_for_listing(ftp_path) == E_FTP_RESULT_CONTINUE) {
				ftp_data.state = E_FTP_STE_CONTINUE_LISTING;
				ftp_send_reply(150, NULL);
			}
			else ftp_send_reply(550, NULL);
			break;
		case E_FTP_CMD_RETR:
			ftp_data.total = 0;
			ftp_data.time = 0;
			ftp_get_param_and_open_child(&bufptr);
			if ((strlen(ftp_path) > 0) && (ftp_path[strlen(ftp_path)-1] != '/')) {
				if (ftp_open_file(ftp_path, "rb")) {
					ftp_data.state = E_FTP_STE_CONTINUE_FILE_TX;
					ftp_send_reply(150, NULL);
				}
				else {
					ftp_data.state = E_FTP_STE_END_TRANSFER;
					ftp_send_reply(550, NULL);
				}
			}
			else {
				ftp_data.state = E_FTP_STE_END_TRANSFER;
				ftp_send_reply(550, NULL);
			}
			break;
		case E_FTP_CMD_APPE:
			ftp_data.total = 0;
			ftp_data.time = 0;
			ftp_get_param_and_open_child(&bufptr);
			if ((strlen(ftp_path) > 0) && (ftp_path[strlen(ftp_path)-1] != '/')) {
				if (ftp_open_file(ftp_path, "ab")) {
					ftp_data.state = E_FTP_STE_CONTINUE_FILE_RX;
					ftp_send_reply(150, NULL);
				}
				else {
					ftp_data.state = E_FTP_STE_END_TRANSFER;
					ftp_send_reply(550, NULL);
				}
			}
			else {
				ftp_data.state = E_FTP_STE_END_TRANSFER;
				ftp_send_reply(550, NULL);
			}
			break;
		case E_FTP_CMD_STOR:
			ftp_data.total = 0;
			ftp_data.time = 0;
			ftp_get_param_and_open_child(&bufptr);
			if ((strlen(ftp_path) > 0) && (ftp_path[strlen(ftp_path)-1] != '/')) {
				ESP_LOGI(FTP_TAG, "E_FTP_CMD_STOR ftp_path=[%s]", ftp_path);
				if (ftp_open_file(ftp_path, "wb")) {
					ftp_data.state = E_FTP_STE_CONTINUE_FILE_RX;
					ftp_send_reply(150, NULL);
				}
				else {
					ftp_data.state = E_FTP_STE_END_TRANSFER;
					ftp_send_reply(550, NULL);
				}
			}
			else {
				ftp_data.state = E_FTP_STE_END_TRANSFER;
				ftp_send_reply(550, NULL);
			}
			break;
		case E_FTP_CMD_DELE:
			ftp_get_param_and_open_child(&bufptr);
			if ((strlen(ftp_path) > 0) && (ftp_path[strlen(ftp_path)-1] != '/')) {
				ESP_LOGI(FTP_TAG, "E_FTP_CMD_DELE ftp_path=[%s]", ftp_path);

				strcat(fullname, ftp_path);
				ESP_LOGI(FTP_TAG, "E_FTP_CMD_DELE fullname=[%s]", fullname);

				//if (unlink(ftp_path) == 0) {
				if (unlink(fullname) == 0) {
					ftp_send_reply(250, NULL);
				}
				else ftp_send_reply(550, NULL);
			}
			else ftp_send_reply(250, NULL);
			break;
		case E_FTP_CMD_RMD:
			ftp_get_param_and_open_child(&bufptr);
			if ((strlen(ftp_path) > 0) && (ftp_path[strlen(ftp_path)-1] != '/')) {
				ESP_LOGI(FTP_TAG, "E_FTP_CMD_RMD ftp_path=[%s]", ftp_path);

				strcat(fullname, ftp_path);
				ESP_LOGI(FTP_TAG, "E_FTP_CMD_MKD fullname=[%s]", fullname);

				//if (rmdir(ftp_path) == 0) {
				if (rmdir(fullname) == 0) {
					ftp_send_reply(250, NULL);
				}
				else ftp_send_reply(550, NULL);
			}
			else ftp_send_reply(250, NULL);
			break;
		case E_FTP_CMD_MKD:
			ftp_get_param_and_open_child(&bufptr);
			if ((strlen(ftp_path) > 0) && (ftp_path[strlen(ftp_path)-1] != '/')) {
				ESP_LOGI(FTP_TAG, "E_FTP_CMD_MKD ftp_path=[%s]", ftp_path);

				strcat(fullname, ftp_path);
				ESP_LOGI(FTP_TAG, "E_FTP_CMD_MKD fullname=[%s]", fullname);

				//if (mkdir(ftp_path, 0755) == 0) {
				if (mkdir(fullname, 0755) == 0) {
					ftp_send_reply(250, NULL);
				}
				else ftp_send_reply(550, NULL);
			}
			else ftp_send_reply(250, NULL);
			break;
		case E_FTP_CMD_RNFR:
			ftp_get_param_and_open_child(&bufptr);
			ESP_LOGI(FTP_TAG, "E_FTP_CMD_RNFR ftp_path=[%s]", ftp_path);

			strcat(fullname, ftp_path);
			ESP_LOGI(FTP_TAG, "E_FTP_CMD_MKD fullname=[%s]", fullname);

			//res = stat(ftp_path, &buf);
			res = stat(fullname, &buf);
			if (res == 0) {
				ftp_send_reply(350, NULL);
				// save the path of the file to rename
				strcpy((char *)ftp_data.dBuffer, ftp_path);
			} else {
				ftp_send_reply(550, NULL);
			}
			break;
		case E_FTP_CMD_RNTO:
			ftp_get_param_and_open_child(&bufptr);
			// the path of the file to rename was saved in the data buffer
			ESP_LOGI(FTP_TAG, "E_FTP_CMD_RNTO ftp_path=[%s], ftp_data.dBuffer=[%s]", ftp_path, (char *)ftp_data.dBuffer);
			strcat(fullname, (char *)ftp_data.dBuffer);
			ESP_LOGI(FTP_TAG, "E_FTP_CMD_RNTO fullname=[%s]", fullname);
			strcat(fullname2, ftp_path);
			ESP_LOGI(FTP_TAG, "E_FTP_CMD_RNTO fullname2=[%s]", fullname2);

			//if (rename((char *)ftp_data.dBuffer, ftp_path) == 0) {
			if (rename(fullname, fullname2) == 0) {
				ftp_send_reply(250, NULL);
			} else {
				ftp_send_reply(550, NULL);
			}
			break;
		case E_FTP_CMD_NOOP:
			ftp_send_reply(200, NULL);
			break;
		case E_FTP_CMD_QUIT:
			ftp_send_reply(221, NULL);
			break;
		default:
			// command not implemented
			ftp_send_reply(502, NULL);
			break;
		}

		if (ftp_data.closechild) {
			remove_fname_from_path(ftp_path, ftp_scratch_buffer);
		}
	}
	else if (result == E_FTP_RESULT_CONTINUE) {
		if (ftp_data.ctimeout > ftp_timeout) {
			ftp_send_reply(221, NULL);
			ESP_LOGW(FTP_TAG, "Connection timeout");
		}
	}
	else {
		ftp_close_cmd_data();
	}
}

//---------------------------------------
static void ftp_wait_for_enabled (void) {
	// Check if the telnet service has been enabled
	if (ftp_data.enabled) {
		ftp_data.state = E_FTP_STE_START;
	}
}

// ==== PUBLIC FUNCTIONS ===================================================================

//---------------------
void ftp_deinit(void) {
    if(writer){
        pearl_writer_trace(writer,last_writer_trace,sizeof(last_writer_trace));
        if(!pearl_writer_destroy(writer))return;
        writer=NULL;
    }
	if(file_buffer) free(file_buffer);
    file_buffer=NULL;
	if (ftp_path) free(ftp_path);
	if (ftp_cmd_buffer) free(ftp_cmd_buffer);
	if (ftp_data.dBuffer) free(ftp_data.dBuffer);
	if (ftp_scratch_buffer) free(ftp_scratch_buffer);
	ftp_path = NULL;
	ftp_cmd_buffer = NULL;
	ftp_data.dBuffer = NULL;
	ftp_scratch_buffer = NULL;
}

//-------------------
bool ftp_init(void) {
	ftp_stop = 0;
	// Allocate memory for the data buffer, and the file system structures (from the RTOS heap)
	ftp_deinit();
    if(writer)return false;
    last_writer_trace[0]=0;
    writer=pearl_writer_create(2u*1024u*1024u);
    if(!writer)return false;

	memset(&ftp_data, 0, sizeof(ftp_data_t));
    #ifdef PEARL_FTP_HOST
    ftp_data.dBuffer=malloc(ftp_buff_size+1);
    file_buffer=malloc(32768);
#else
    ftp_data.dBuffer=heap_caps_malloc(ftp_buff_size+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    file_buffer=heap_caps_malloc(32768,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#endif
    ftp_path=malloc(FTP_MAX_PARAM_SIZE);
    ftp_scratch_buffer=malloc(FTP_MAX_PARAM_SIZE);
    ftp_cmd_buffer=malloc(FTP_MAX_PARAM_SIZE+FTP_CMD_SIZE_MAX+1);
    if(!file_buffer||!ftp_data.dBuffer||!ftp_path||!ftp_scratch_buffer||!ftp_cmd_buffer){ftp_deinit();return false;}

	//SOCKETFIFO_Init((void *)ftp_fifoelements, FTP_SOCKETFIFO_ELEMENTS_MAX);

	ftp_data.c_sd  = -1;
	ftp_data.d_sd  = -1;
	ftp_data.lc_sd = -1;
	ftp_data.ld_sd = -1;
	ftp_data.e_open = E_FTP_NOTHING_OPEN;
	ftp_data.state = E_FTP_STE_DISABLED;
	ftp_data.substate = E_FTP_STE_SUB_DISCONNECTED;

	//if (ftp_mutex == NULL) ftp_mutex = xSemaphoreCreateMutex();
	return true;
}

//============================
int ftp_run (uint32_t elapsed)
{
	//if (xSemaphoreTake(ftp_mutex, FTP_MUTEX_TIMEOUT_MS / portTICK_PERIOD_MS) !=pdTRUE) return -1;
    trace_state=ftp_data.state;trace_substate=ftp_data.substate;
	if (ftp_stop) return -2;

	ftp_data.dtimeout += elapsed;
	ftp_data.ctimeout += elapsed;
	ftp_data.time += elapsed;

	switch (ftp_data.state) {
		case E_FTP_STE_DISABLED:
			ftp_wait_for_enabled();
			break;
		case E_FTP_STE_START:
			if (ftp_create_listening_socket(&ftp_data.lc_sd, FTP_CMD_PORT, FTP_CMD_CLIENTS_MAX - 1)) {
				ftp_data.state = E_FTP_STE_READY;
			}
			break;
		case E_FTP_STE_READY:
			if (ftp_data.c_sd < 0 && ftp_data.substate == E_FTP_STE_SUB_DISCONNECTED) {
				if (E_FTP_RESULT_OK == ftp_wait_for_connection(ftp_data.lc_sd, &ftp_data.c_sd, &ftp_data.ip_addr)) {
					ftp_data.txRetries = 0;
					ftp_data.ctimeout = 0;
					strcpy (ftp_path, "/");
					ESP_LOGI(FTP_TAG, "Connected.");
					//ftp_send_reply (220, "Micropython FTP Server");
					ftp_send_reply (220, "ESP32 FTP Server");
					break;
				}
			}
			if (ftp_data.c_sd > 0 && ftp_data.substate != E_FTP_STE_SUB_LISTEN_FOR_DATA) {
				ftp_process_cmd();
				if (ftp_data.state != E_FTP_STE_READY) {
					break;
				}
			}
			break;
		case E_FTP_STE_END_TRANSFER:
			if (ftp_data.d_sd >= 0) {
				closesocket(ftp_data.d_sd);
				ftp_data.d_sd = -1;
			}
			break;
		case E_FTP_STE_CONTINUE_LISTING:
			// go on with listing
			{
				uint32_t listsize = 0;
				ftp_result_t list_res = ftp_list_dir((char *)ftp_data.dBuffer, ftp_buff_size, &listsize);
				if (listsize > 0) ftp_send_list(listsize);
				if (list_res == E_FTP_RESULT_OK) {
					ftp_send_reply(226, NULL);
					ftp_data.state = E_FTP_STE_END_TRANSFER;
				}
				ftp_data.ctimeout = 0;
			}
			break;
		case E_FTP_STE_CONTINUE_FILE_TX:
			// read and send the next block from the file
			{
				uint32_t readsize;
				ftp_result_t result;
				ftp_data.ctimeout = 0;
				result = ftp_read_file ((char *)ftp_data.dBuffer, ftp_buff_size, &readsize);
				if (result == E_FTP_RESULT_FAILED) {
					ftp_send_reply(451, NULL);
					ftp_data.state = E_FTP_STE_END_TRANSFER;
				}
				else {
					if (readsize > 0) {
						ftp_send_file_data(readsize);
						ftp_data.total += readsize;
						ESP_LOGI(FTP_TAG, "Sent %"PRIu32", total: %"PRIu32, readsize, ftp_data.total);
					}
					if (result == E_FTP_RESULT_OK) {
						ftp_send_reply(226, NULL);
						ftp_data.state = E_FTP_STE_END_TRANSFER;
						ESP_LOGI(FTP_TAG, "File sent (%"PRIu32" bytes in %"PRIu32" msec).", ftp_data.total, ftp_data.time);
					}
				}
			}
			break;
		case E_FTP_STE_CONTINUE_FILE_RX:
			{
                if(!discard_file&&pearl_writer_failed(writer)){ftp_close_files_dir();ftp_send_reply(552,NULL);ftp_data.state=E_FTP_STE_END_TRANSFER;break;}
                if(!discard_file&&pearl_writer_space(writer)<(unsigned)ftp_buff_size){pearl_writer_backpressure(writer);break;} /* Backpressure: leave bytes in TCP. */
				int32_t len;
				ftp_result_t result = E_FTP_RESULT_OK;

				ESP_LOGI(FTP_TAG, "ftp_buff_size=%d", ftp_buff_size);
				result = ftp_recv_non_blocking(ftp_data.d_sd, ftp_data.dBuffer, ftp_buff_size, &len);
				if (result == E_FTP_RESULT_OK) {
					// block of data received
					ftp_data.dtimeout = 0;
					ftp_data.ctimeout = 0;
					// save received data to file
					if (E_FTP_RESULT_OK != ftp_write_file ((char *)ftp_data.dBuffer, len)) {
						ftp_send_reply(451, NULL);
						ftp_data.state = E_FTP_STE_END_TRANSFER;
						ESP_LOGW(FTP_TAG, "Error writing to file");
					}
					else {
						ftp_data.total += len;
						ESP_LOGI(FTP_TAG, "Received %"PRIu32", total: %"PRIu32, len, ftp_data.total);
					}
				}
				else if (result == E_FTP_RESULT_CONTINUE) {
					// nothing received
					if (ftp_data.dtimeout > FTP_DATA_TIMEOUT_MS) {
						ftp_close_files_dir();
						ftp_send_reply(426, NULL);
						ftp_data.state = E_FTP_STE_END_TRANSFER;
						ESP_LOGW(FTP_TAG, "Receiving to file timeout");
					}
				}
				else {
					// File received (E_FTP_RESULT_FAILED)
                    if(async_file){
                        if(!pearl_writer_finish(writer,len<0))file_failed=true;
                        async_file=false;ftp_data.fp=NULL;
                    }
					ftp_close_files_dir();
                    ftp_send_reply(file_failed?552:len<0?451:226, NULL);
					ftp_data.state = E_FTP_STE_END_TRANSFER;
					ESP_LOGI(FTP_TAG, "File received (%"PRIu32" bytes in %"PRIu32" msec).", ftp_data.total, ftp_data.time);
					break;
				}
			}
			break;
		default:
			break;
	}

	switch (ftp_data.substate) {
	case E_FTP_STE_SUB_DISCONNECTED:
		break;
	case E_FTP_STE_SUB_LISTEN_FOR_DATA:
		if (E_FTP_RESULT_OK == ftp_wait_for_connection(ftp_data.ld_sd, &ftp_data.d_sd, NULL)) {
			ftp_data.dtimeout = 0;
			ftp_data.substate = E_FTP_STE_SUB_DATA_CONNECTED;
			ESP_LOGI(FTP_TAG, "Data socket connected");
		}
		else if (ftp_data.dtimeout > FTP_DATA_TIMEOUT_MS) {
			ESP_LOGW(FTP_TAG, "Waiting for data connection timeout (%"PRIi32")", ftp_data.dtimeout);
			ftp_data.dtimeout = 0;
			// close the listening socket
			closesocket(ftp_data.ld_sd);
			ftp_data.ld_sd = -1;
			ftp_data.substate = E_FTP_STE_SUB_DISCONNECTED;
		}
		break;
	case E_FTP_STE_SUB_DATA_CONNECTED:
		if (ftp_data.state == E_FTP_STE_READY && (ftp_data.dtimeout > FTP_DATA_TIMEOUT_MS)) {
			// close the listening and the data socket
			closesocket(ftp_data.ld_sd);
			closesocket(ftp_data.d_sd);
			ftp_data.ld_sd = -1;
			ftp_data.d_sd = -1;
			ftp_close_filesystem_on_error ();
			ftp_data.substate = E_FTP_STE_SUB_DISCONNECTED;
			ESP_LOGW(FTP_TAG, "Data connection timeout");
		}
		break;
	default:
		break;
	}

	// check the state of the data sockets
	if (ftp_data.d_sd < 0 && (ftp_data.state > E_FTP_STE_READY)) {
		ftp_data.substate = E_FTP_STE_SUB_DISCONNECTED;
		ftp_data.state = E_FTP_STE_READY;
		ESP_LOGI(FTP_TAG, "Data socket disconnected");
	}

	//xSemaphoreGive(ftp_mutex);
	return 0;
}

//----------------------
bool ftp_enable (void) {
	bool res = false;
	if (ftp_data.state == E_FTP_STE_DISABLED) {
		ftp_data.enabled = true;
		res = true;
	}
	return res;
}

//-------------------------
bool ftp_isenabled (void) {
	bool res = (ftp_data.enabled == true);
	return res;
}

//-----------------------
bool ftp_disable (void) {
	bool res = false;
	if (ftp_data.state == E_FTP_STE_READY) {
		_ftp_reset();
		ftp_data.enabled = false;
		ftp_data.state = E_FTP_STE_DISABLED;
		res = true;
	}
	return res;
}

//---------------------
bool ftp_reset (void) {
	_ftp_reset();
	return true;
}

// Return current ftp server state
//------------------
int ftp_getstate() {
	int fstate = ftp_data.state | (ftp_data.substate << 8);
	if ((ftp_data.state == E_FTP_STE_READY) && (ftp_data.c_sd > 0)) fstate = E_FTP_STE_CONNECTED;
	return fstate;
}

//-------------------------
bool ftp_terminate (void) {
	bool res = false;
	if (ftp_data.state == E_FTP_STE_READY) {
		ftp_stop = 1;
		_ftp_reset();
		res = true;
	}
	return res;
}

//-------------------------
bool ftp_stop_requested() {
	bool res = (ftp_stop == 1);
	return res;
}

#if 0
//-------------------------------
int32_t ftp_get_maxstack (void) {
	int32_t maxstack = ftp_stack_size - uxTaskGetStackHighWaterMark(NULL);
	return maxstack;
}
#endif


void pearl_ftp_close(void){ftp_reset();ftp_deinit();}
