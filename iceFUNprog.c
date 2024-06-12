/*
 *
 *  Copyright(C) 2018 Gerald Coe, Devantech Ltd <gerry@devantech.co.uk>
 *
 *  Permission to use, copy, modify, and/or distribute this software for any purpose with or
 *  without fee is hereby granted, provided that the above copyright notice and
 *  this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO
 *  THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.
 *  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 *  DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 *  AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 *  CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>

#ifndef _FILEHASH_MD5SUM
#include <openssl/md5.h>
#else
#define STRINGIFY(X) #X
#define FILEHASH(X) STRINGIFY(X)
#endif

typedef enum {
  ERROR,
  WARNING,
  INFO,
  DEBUG,
} debuglvl;

enum cmds
{
    DONE = 0xb0, GET_VER, RESET_FPGA, ERASE_CHIP, ERASE_64k, PROG_PAGE, READ_PAGE, VERIFY_PAGE, GET_CDONE, RELEASE_FPGA
};

#define FLASHSIZE 1048576	// 1MByte because that is the size of the Flash chip
unsigned char FPGAbuf[FLASHSIZE];
unsigned char SerBuf[300];
char ProgName[30];
int fd;
char verify;
int rw_offset = 0;
debuglvl g_debuglvl = WARNING;


void dbgprintf(debuglvl level, const char* format, ...) {
  if (level <= g_debuglvl) {
    va_list args;
    va_start(args, format);
    switch (level) {
      case ERROR:	fprintf(stderr, "  ERROR!| "); break;
      case WARNING:	fprintf(stderr, " WARNING| "); break;
      case INFO: 	fprintf(stderr, "    INFO| "); break;
      case DEBUG: 	fprintf(stderr, "   DEBUG| "); break;
	default:	fprintf(stderr, "        | "); break;
    }
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");
  }
}

static void help(const char *progname)
{
	dbgprintf(DEBUG, "showing help");
	fprintf(stderr, "Programming tool for Devantech iceFUN board.\n");
	fprintf(stderr, "Usage: %s [-P] <SerialPort>\n", progname);
	fprintf(stderr, "       %s <input file>\n", progname);
	fprintf(stderr, "\n");
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "  -P <Serial Port>      use the specified USB device [default: /dev/ttyACM0]\n");
	fprintf(stderr, "  -h                    display this help and exit\n");
	fprintf(stderr, "  -o <offset in bytes>  start address for write [default: 0]\n");
	fprintf(stderr, "                        (append 'k' to the argument for size in kilobytes,\n");
	fprintf(stderr, "                         or 'M' for size in megabytes)\n");
	fprintf(stderr, "                         or prefix '0x' to the argument for size in hexdecimal\n");
	fprintf(stderr, "  --help\n");
	fprintf(stderr, "  -s                    skip verification\n");
	fprintf(stderr, "  -V                    show version and binary build hash\n");
	fprintf(stderr, "  -v                    increase verbosity\n");
	fprintf(stderr, "Example:\n");
	fprintf(stderr, "%s -P /dev/ttyACM1 blinky.bin\n", progname);
	fprintf(stderr, "\n");
	fprintf(stderr, "Exit status:\n");
	fprintf(stderr, "  0 on success,\n");
	fprintf(stderr, "  1 operation failed.\n");
	fprintf(stderr, "\n");
}

#ifndef _FILEHASH_MD5SUM
unsigned char* md5_sum(const char* filename) {
  unsigned char* digest = (unsigned char*)malloc(MD5_DIGEST_LENGTH * sizeof(unsigned char));
  FILE* file = fopen(filename, "rb");
  if (!file) {
    dbgprintf(ERROR, " Unable to open file %s", filename);
    return NULL;
  }

  dbgprintf(DEBUG, "calculating MD5 hash of: %s", filename);
  unsigned char buffer[1024];
  MD5_CTX md5;
  MD5_Init(&md5);
  int bytes = 0;
  int last_bytes = bytes;
  while (((bytes += fread(buffer, 1, 1024, file)) - last_bytes) != 0) {
    MD5_Update(&md5, buffer, bytes);
    last_bytes = bytes;
  }
  MD5_Final(digest, &md5);
  dbgprintf(DEBUG, "hash calculated from %d bytes", bytes);
  dbgprintf(INFO, "hash calculated: %s", md5);
  fclose(file);
  return digest;
}
#endif

int GetVersion(void)								// print firmware version number
{
	dbgprintf(DEBUG, "sending GET_VER message command: 0x%02x", GET_VER);
	SerBuf[0] = GET_VER;
	write(fd, SerBuf, 1);
//	tcdrain(fd);
	read(fd, SerBuf, 2);
	if(SerBuf[0] == 38) {
		dbgprintf(INFO, "iceFUN v%d, ",SerBuf[1]);
		return 0;
	}
	else {
		dbgprintf(ERROR, "%s: Error getting Version", ProgName);
		return EXIT_FAILURE;
	}
}

int resetFPGA(void)									// reset the FPGA and return the flash ID bytes
{
	dbgprintf(DEBUG, "sending RESET_FPGA message command: 0x%02x", RESET_FPGA);
	SerBuf[0] = RESET_FPGA;
	write(fd, SerBuf, 1);
	read(fd, SerBuf, 3);
	fprintf(stderr, "Flash ID %02X %02X %02X\n",SerBuf[0], SerBuf[1], SerBuf[2]);
	return 0;
}

int releaseFPGA(void)								// run the FPGA
{
	dbgprintf(DEBUG, "sending RELEASE_FPGA message command: 0x%02x", RELEASE_FPGA);
	SerBuf[0] = RELEASE_FPGA;
	write(fd, SerBuf, 1);
	read(fd, SerBuf, 1);
	fprintf(stderr, "Done.\n");
	return 0;
}


int main(int argc, char **argv)
{
const char *portPath = "/dev/serial/by-id/";
const char *filename = NULL;
char portName[300];
int length;
struct termios config;

	dbgprintf(DEBUG, "loading %s", __FILE__);
	// verify unless told not to
	verify = 1;

	// open the path
 	DIR * d = opendir(portPath);

	// for the directory entries
	struct dirent *dir;
	dbgprintf(DEBUG, "checking for port: %s", portName);
	// default serial port to use
	if(d==NULL) {
	       	strcpy(portName, "/dev/ttyACM0");
		dbgprintf(DEBUG, "using serial port: %s", portName);
	} else {
		// if we were able to read somehting from the directory
		while ((dir = readdir(d)) != NULL)
		{
			char *pF = strstr(dir->d_name, "iceFUN");
			if(pF) {
				// found iceFUN board so copy full symlink
				strcpy(portName, portPath);
				strcat(portName, dir->d_name);
				dbgprintf(INFO, "found port: %s", portName);
				break;
			}
		}
	}
	closedir(d);

	/* Decode command line parameters */
	static struct option long_options[] = {
		{"help", no_argument, NULL, -2},
		{NULL, 0, NULL, 0}
	};

	int opt;
	char *endptr;
	while ((opt = getopt_long(argc, argv, "P:o:svVh", long_options, NULL)) != -1) {
		switch (opt) {

		case 'P': /* Serial port */
			strcpy(portName, optarg);
			break;
		case 'h':
		case -2:
			help(argv[0]);
			close(fd);
			return EXIT_SUCCESS;
		case 's':
			verify = 0;
			break;
		case 'v':
			g_debuglvl++;
			dbgprintf(g_debuglvl == WARNING ? WARNING : g_debuglvl == INFO ? INFO : g_debuglvl == DEBUG ? DEBUG : ERROR, "increased debug level");
			break;
		case 'V':
			fprintf(stdout, "Build datetime: %s   T %s\n", __DATE__, __TIME__);
#ifndef _FILEHASH_MD5SUM
			int strsize = strlen("Build hash (): ") + strlen(__FILE__) + 1;
			char* progfile = malloc(strsize*sizeof(char));
		        sprintf(progfile, "%s", __FILE__);
			fprintf(stdout, "Build hash (%s): ", progfile);
			struct stat buffer;
  			if (stat(progfile, &buffer) == 0 && S_ISREG(buffer.st_mode)) {
    				unsigned char* digest = md5_sum(progfile);
	    			for (int i = 0; i < MD5_DIGEST_LENGTH; i++) {
	      				fprintf(stdout, "%02x", digest[i]);
	    			}
				free(digest);
			} else {
				fprintf(stdout, "[[ %s </not_found> ]]", __FILE__);
			}
			fprintf(stdout, "\n\n");
			free(progfile);
#else
			fprintf(stdout, "Build hash: %s\n\n", FILEHASH(_FILEHASH_MD5SUM));
#endif
			return 0;
    case 'o': /* set address offset */
      rw_offset = strtol(optarg, &endptr, 0);
      if (*endptr == '\0')
        /* ok */;
      else if (!strcmp(endptr, "k"))
        rw_offset *= 1024;
      else if (!strcmp(endptr, "M"))
        rw_offset *= 1024 * 1024;
      else {
        dbgprintf(ERROR, "'%s' is not a valid offset", optarg);
        return EXIT_FAILURE;
      }
      break;
		default:
			/* error message has already been printed */
			dbgprintf(ERROR, "Try `%s -h' for more information.", argv[0]);
			close(fd);
			return EXIT_FAILURE;
		}
	}

	fd = open(portName, O_RDWR | O_NOCTTY);
	if(fd == -1) {
		dbgprintf(ERROR, "%s: failed to open serial port.", argv[0]);
		return EXIT_FAILURE;
	}
	tcgetattr(fd, &config);
	cfmakeraw(&config);								// set options for raw data
	tcsetattr(fd, TCSANOW, &config);

	if (optind + 1 == argc) {
		filename = argv[optind];
	} else if (optind != argc) {
		dbgprintf(ERROR, "%s: too many arguments", argv[0]);
		dbgprintf(ERROR, "Try `%s --help' for more information.\n", argv[0]);
		close(fd);
		return EXIT_FAILURE;
	} else  {
		dbgprintf(ERROR, "%s: missing argument", argv[0]);
		dbgprintf(ERROR, "Try `%s --help' for more information.\n", argv[0]);
		close(fd);
		return EXIT_FAILURE;
	}

	FILE* fp = fopen(filename, "rb");
	if(fp==NULL) {
		dbgprintf(ERROR, "%s: failed to open file %s.", argv[0], filename);
		close(fd);
		return EXIT_FAILURE;
	}
	length = fread(&FPGAbuf[rw_offset], 1, FLASHSIZE, fp);

	strcpy(ProgName, argv[0]);
	if(!GetVersion()) resetFPGA();						// reset the FPGA
	else return EXIT_FAILURE;

  int endPage = ((rw_offset + length) >> 16) + 1;

  dbgprintf(DEBUG, "starting page erase operation, ERASE_64k command: 0x%02x", ERASE_64k);
  for (int page = (rw_offset >> 16); page < endPage; page++)			// erase sufficient 64k sectors
  {
    SerBuf[0] = ERASE_64k;
    SerBuf[1] = page;
    write(fd, SerBuf, 2);
    dbgprintf(INFO, "Erasing sector %02X0000", page);
    read(fd, SerBuf, 1);
  }
  dbgprintf(INFO, "file size: %d", (int)length);

	int addr = rw_offset;
	dbgprintf(INFO ,"Programming ");						// program the FPGA
	int cnt = 0;
  	int endAddr = addr + length; // no flashsize check
	dbgprintf(DEBUG, "starting programming. PROG_PAGE command is: 0x%02x", PROG_PAGE);
	while (addr < endAddr)
	{
		SerBuf[0] = PROG_PAGE;
		SerBuf[1] = (addr>>16);
		SerBuf[2] = (addr>>8);
		SerBuf[3] = (addr);
		for (int x = 0; x < 256; x++) SerBuf[x + 4] = FPGAbuf[addr++];
		write(fd, SerBuf, 260);
		read(fd, SerBuf, 4);
		if (SerBuf[0] != 0)
		{
			dbgprintf(ERROR, "\nProgram failed at %06X, %02X expected, %02X read.", addr - 256 + SerBuf[1] - 4, SerBuf[2], SerBuf[3]);
			return EXIT_FAILURE;
		}
		if (++cnt == 10)
		{
			cnt = 0;
			fprintf(stderr, ".");
		}
	}
	fprintf(stderr, "\n");

	if(verify) {
		addr = rw_offset;
		dbgprintf(INFO, "Verifying ");
		cnt = 0;

		dbgprintf(DEBUG, "starting page verification, VERIFY_PAGE command: 0x%02x", VERIFY_PAGE);
		while (addr < endAddr)
		{
			SerBuf[0] = VERIFY_PAGE;
			SerBuf[1] = (addr >> 16);
			SerBuf[2] = (addr >> 8);
			SerBuf[3] = addr;
			for (int x = 0; x < 256; x++) SerBuf[x + 4] = FPGAbuf[addr++];
			write(fd, SerBuf, 260);
			read(fd, SerBuf, 4);
			if (SerBuf[0] > 0)
			{
				dbgprintf(ERROR, "Verify failed at %06X, %02X expected, %02X read.", addr - 256 + SerBuf[1] - 4, SerBuf[2], SerBuf[3]);
				return EXIT_FAILURE;
			}
			if (++cnt == 10)
			{
				cnt = 0;
				fprintf(stderr, ".");
			}
		}
	}
	fprintf(stderr, "\n");

	releaseFPGA();
	return 0;
}


