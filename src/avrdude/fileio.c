/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Copyright (C) 2000-2004  Brian S. Dean <bsd@bsdhome.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/* $Id$ */

#include "ac_cfg.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <stdint.h>

#ifdef HAVE_LIBELF
#ifdef HAVE_LIBELF_H
#include <libelf.h>
#elif defined(HAVE_LIBELF_LIBELF_H)
#include <libelf/libelf.h>
#endif
#define EM_AVR32 0x18ad         /* inofficial */
#endif

#include "avrdude.h"
#include "libavrdude.h"


#define IHEX_MAXDATA 256

#define MAX_LINE_LEN 256  /* max line length for ASCII format input files */

#define MAX_MODE_LEN 32  // For fopen_and_seek()


struct ihexrec {
  unsigned char    reclen;
  unsigned int     loadofs;
  unsigned char    rectyp;
  unsigned char    data[IHEX_MAXDATA];
  unsigned char    cksum;
};


static int b2ihex(unsigned char * inbuf, int bufsize, 
             int recsize, int startaddr,
             char * outfile, FILE * outf);

static int ihex2b(char * infile, FILE * inf,
             AVRMEM * mem, int bufsize, unsigned int fileoffset);

static int b2srec(unsigned char * inbuf, int bufsize, 
           int recsize, int startaddr,
           char * outfile, FILE * outf);

static int srec2b(char * infile, FILE * inf,
             AVRMEM * mem, int bufsize, unsigned int fileoffset);

static int ihex_readrec(struct ihexrec * ihex, char * rec);

static int srec_readrec(struct ihexrec * srec, char * rec);

static int fileio_rbin(struct fioparms * fio,
                  char * filename, FILE * f, AVRMEM * mem, int size);

static int fileio_ihex(struct fioparms * fio, 
                  char * filename, FILE * f, AVRMEM * mem, int size);

static int fileio_srec(struct fioparms * fio,
                  char * filename, FILE * f, AVRMEM * mem, int size);

#ifdef HAVE_LIBELF
static int elf2b(char * infile, FILE * inf,
                 AVRMEM * mem, struct avrpart * p,
                 int bufsize, unsigned int fileoffset);

static int fileio_elf(struct fioparms * fio,
                      char * filename, FILE * f, AVRMEM * mem,
                      struct avrpart * p, int size);
#endif

static int fileio_num(struct fioparms * fio,
		char * filename, FILE * f, AVRMEM * mem, int size,
		FILEFMT fmt);

static int fmt_autodetect(char * fname, unsigned section);



static FILE *fopen_and_seek(const char *filename, const char *mode, unsigned section)
{
  FILE *file;
  // On Windows we need to convert the filename to UTF-16
#if defined(WIN32NATIVE)
  static wchar_t fname_buffer[PATH_MAX];
  static wchar_t mode_buffer[MAX_MODE_LEN];

  if (MultiByteToWideChar(CP_UTF8, 0, filename, -1, fname_buffer, PATH_MAX) == 0) { return NULL; }
  if (MultiByteToWideChar(CP_ACP, 0, mode, -1, mode_buffer, MAX_MODE_LEN) == 0) { return NULL; }

  file = _wfopen(fname_buffer, mode_buffer);
#else
  file = fopen(filename, mode);
#endif

  if (file == NULL) {
    return NULL;
  }

  // Seek to the specified 'section'
  static const char *hex_terminator = ":00000001FF\r";
  unsigned terms_seen = 0;
  char buffer[MAX_LINE_LEN + 1];

  while (terms_seen < section && fgets(buffer, MAX_LINE_LEN, file) != NULL) {
    size_t len = strlen(buffer);

    if (buffer[len - 1] == '\n') {
      len--;
      buffer[len] = 0;
    }
    if (buffer[len - 1] != '\r') {
      buffer[len] = '\r';
      len++;
      buffer[len] = 0;
    }

    if (strcmp(buffer, hex_terminator) == 0) {
      // Found a section terminator
      terms_seen++;
    }
  }

  if (feof(file)) {
    // Section not found
    fclose(file);
    return NULL;
  }

  return file;
}


char * fmtstr(FILEFMT format)
{
  switch (format) {
    case FMT_AUTO : return "auto-detect"; break;
    case FMT_SREC : return "Motorola S-Record"; break;
    case FMT_IHEX : return "Intel Hex"; break;
    case FMT_RBIN : return "raw binary"; break;
    case FMT_ELF  : return "ELF"; break;
    default       : return "invalid format"; break;
  };
}



static int b2ihex(unsigned char * inbuf, int bufsize, 
           int recsize, int startaddr,
           char * outfile, FILE * outf)
{
  unsigned char * buf;
  unsigned int nextaddr;
  int n, nbytes, n_64k;
  int i;
  unsigned char cksum;

  if (recsize > 255) {
    avrdude_message(MSG_INFO, "%s: recsize=%d, must be < 256\n",
              progname, recsize);
    return -1;
  }

  n_64k    = 0;
  nextaddr = startaddr;
  buf      = inbuf;
  nbytes   = 0;

  while (bufsize) {
    n = recsize;
    if (n > bufsize)
      n = bufsize;

    if ((nextaddr + n) > 0x10000)
      n = 0x10000 - nextaddr;

    if (n) {
      cksum = 0;
      fprintf(outf, ":%02X%04X00", n, nextaddr);
      cksum += n + ((nextaddr >> 8) & 0x0ff) + (nextaddr & 0x0ff);
      for (i=0; i<n; i++) {
        fprintf(outf, "%02X", buf[i]);
        cksum += buf[i];
      }
      cksum = -cksum;
      fprintf(outf, "%02X\n", cksum);
      
      nextaddr += n;
      nbytes   += n;
    }

    if (nextaddr >= 0x10000) {
      int lo, hi;
      /* output an extended address record */
      n_64k++;
      lo = n_64k & 0xff;
      hi = (n_64k >> 8) & 0xff;
      cksum = 0;
      fprintf(outf, ":02000004%02X%02X", hi, lo);
      cksum += 2 + 0 + 4 + hi + lo;
      cksum = -cksum;
      fprintf(outf, "%02X\n", cksum);
      nextaddr = 0;
    }

    /* advance to next 'recsize' bytes */
    buf += n;
    bufsize -= n;
  }

  /*-----------------------------------------------------------------
    add the end of record data line
    -----------------------------------------------------------------*/
  cksum = 0;
  n = 0;
  nextaddr = 0;
  fprintf(outf, ":%02X%04X01", n, nextaddr);
  cksum += n + ((nextaddr >> 8) & 0x0ff) + (nextaddr & 0x0ff) + 1;
  cksum = -cksum;
  fprintf(outf, "%02X\n", cksum);

  return nbytes;
}


static int ihex_readrec(struct ihexrec * ihex, char * rec)
{
  int i, j;
  char buf[8];
  int offset, len;
  char * e;
  unsigned char cksum;
  int rc;

  len    = strlen(rec);
  offset = 1;
  cksum  = 0;

  /* reclen */
  if (offset + 2 > len)
    return -1;
  for (i=0; i<2; i++)
    buf[i] = rec[offset++];
  buf[i] = 0;
  ihex->reclen = strtoul(buf, &e, 16);
  if (e == buf || *e != 0)
    return -1;

  /* load offset */
  if (offset + 4 > len)
    return -1;
  for (i=0; i<4; i++)
    buf[i] = rec[offset++];
  buf[i] = 0;
  ihex->loadofs = strtoul(buf, &e, 16);
  if (e == buf || *e != 0)
    return -1;

  /* record type */
  if (offset + 2 > len)
    return -1;
  for (i=0; i<2; i++)
    buf[i] = rec[offset++];
  buf[i] = 0;
  ihex->rectyp = strtoul(buf, &e, 16);
  if (e == buf || *e != 0)
    return -1;

  cksum = ihex->reclen + ((ihex->loadofs >> 8) & 0x0ff) + 
    (ihex->loadofs & 0x0ff) + ihex->rectyp;

  /* data */
  for (j=0; j<ihex->reclen; j++) {
    if (offset + 2 > len)
      return -1;
    for (i=0; i<2; i++)
      buf[i] = rec[offset++];
    buf[i] = 0;
    ihex->data[j] = strtoul(buf, &e, 16);
    if (e == buf || *e != 0)
      return -1;
    cksum += ihex->data[j];
  }

  /* cksum */
  if (offset + 2 > len)
    return -1;
  for (i=0; i<2; i++)
    buf[i] = rec[offset++];
  buf[i] = 0;
  ihex->cksum = strtoul(buf, &e, 16);
  if (e == buf || *e != 0)
    return -1;

  rc = -cksum & 0x000000ff;

  return rc;
}



/*
 * Intel Hex to binary buffer
 *
 * Given an open file 'inf' which contains Intel Hex formated data,
 * parse the file and lay it out within the memory buffer pointed to
 * by outbuf.  The size of outbuf, 'bufsize' is honored; if data would
 * fall outsize of the memory buffer outbuf, an error is generated.
 *
 * Return the maximum memory address within 'outbuf' that was written.
 * If an error occurs, return -1.
 *
 * */
static int ihex2b(char * infile, FILE * inf,
             AVRMEM * mem, int bufsize, unsigned int fileoffset)
{
  char buffer [ MAX_LINE_LEN ];
  unsigned int nextaddr, baseaddr, maxaddr;
  int i;
  int lineno;
  int len;
  struct ihexrec ihex;
  int rc;

  lineno   = 0;
  baseaddr = 0;
  maxaddr  = 0;
  nextaddr = 0;

  while (fgets((char *)buffer,MAX_LINE_LEN,inf)!=NULL) {
    lineno++;
    len = strlen(buffer);
    if (buffer[len-1] == '\n') 
      buffer[--len] = 0;
    if (buffer[0] != ':')
      continue;
    rc = ihex_readrec(&ihex, buffer);
    if (rc < 0) {
      avrdude_message(MSG_INFO, "%s: invalid record at line %d of \"%s\"\n",
              progname, lineno, infile);
      return -1;
    }
    else if (rc != ihex.cksum) {
      avrdude_message(MSG_INFO, "%s: ERROR: checksum mismatch at line %d of \"%s\"\n",
              progname, lineno, infile);
      avrdude_message(MSG_INFO, "%s: checksum=0x%02x, computed checksum=0x%02x\n",
              progname, ihex.cksum, rc);
      return -1;
    }

    switch (ihex.rectyp) {
      case 0: /* data record */
        if (fileoffset != 0 && baseaddr < fileoffset) {
          avrdude_message(MSG_INFO, "%s: ERROR: address 0x%04x out of range (below fileoffset 0x%x) at line %d of %s\n",
                          progname, baseaddr, fileoffset, lineno, infile);
          return -1;
        }
        nextaddr = ihex.loadofs + baseaddr - fileoffset;
        if (nextaddr + ihex.reclen > bufsize) {
          avrdude_message(MSG_INFO, "%s: ERROR: address 0x%04x out of range at line %d of %s\n",
                          progname, nextaddr+ihex.reclen, lineno, infile);
          return -1;
        }
        for (i=0; i<ihex.reclen; i++) {
          mem->buf[nextaddr+i] = ihex.data[i];
          mem->tags[nextaddr+i] = TAG_ALLOCATED;
        }
        if (nextaddr+ihex.reclen > maxaddr)
          maxaddr = nextaddr+ihex.reclen;
        break;

      case 1: /* end of file record */
        return maxaddr;
        break;

      case 2: /* extended segment address record */
        baseaddr = (ihex.data[0] << 8 | ihex.data[1]) << 4;
        break;

      case 3: /* start segment address record */
        /* we don't do anything with the start address */
        break;

      case 4: /* extended linear address record */
        baseaddr = (ihex.data[0] << 8 | ihex.data[1]) << 16;
        break;

      case 5: /* start linear address record */
        /* we don't do anything with the start address */
        break;

      default:
        avrdude_message(MSG_INFO, "%s: don't know how to deal with rectype=%d "
                        "at line %d of %s\n",
                        progname, ihex.rectyp, lineno, infile);
        return -1;
        break;
    }

  } /* while */

  if (maxaddr == 0) {
    avrdude_message(MSG_INFO, "%s: ERROR: No valid record found in Intel Hex "
                    "file \"%s\"\n",
                    progname, infile);

    return -1;
  }
  else {
    avrdude_message(MSG_INFO, "%s: WARNING: no end of file record found for Intel Hex "
                    "file \"%s\"\n",
                    progname, infile);

    return maxaddr;
  }
}

static int b2srec(unsigned char * inbuf, int bufsize, 
           int recsize, int startaddr,
           char * outfile, FILE * outf)
{
  unsigned char * buf;
  unsigned int nextaddr;
  int n, nbytes, addr_width;
  int i;
  unsigned char cksum;

  char * tmpl=0;

  if (recsize > 255) {
    avrdude_message(MSG_INFO, "%s: ERROR: recsize=%d, must be < 256\n",
            progname, recsize);
    return -1;
  }
  
  nextaddr = startaddr;
  buf = inbuf;
  nbytes = 0;    

  addr_width = 0;

  while (bufsize) {

    n = recsize;

    if (n > bufsize) 
      n = bufsize;

    if (n) {
      cksum = 0;
      if (nextaddr + n <= 0xffff) {
        addr_width = 2;
        tmpl="S1%02X%04X";
      }
      else if (nextaddr + n <= 0xffffff) {
        addr_width = 3;
        tmpl="S2%02X%06X";
      }
      else if (nextaddr + n <= 0xffffffff) {
        addr_width = 4;
        tmpl="S3%02X%08X";
      }
      else {
        avrdude_message(MSG_INFO, "%s: ERROR: address=%d, out of range\n",
                progname, nextaddr);
        return -1;
      }

      fprintf(outf, tmpl, n + addr_width + 1, nextaddr);

      cksum += n + addr_width + 1;

      for (i=addr_width; i>0; i--) 
        cksum += (nextaddr >> (i-1) * 8) & 0xff;

      for (i=nextaddr; i<nextaddr + n; i++) {
        fprintf(outf, "%02X", buf[i]);
        cksum += buf[i];
      }

      cksum = 0xff - cksum;
      fprintf(outf, "%02X\n", cksum);

      nextaddr += n;
      nbytes +=n;
    }

    /* advance to next 'recsize' bytes */
    bufsize -= n;
  }

  /*-----------------------------------------------------------------
    add the end of record data line
    -----------------------------------------------------------------*/
  cksum = 0;
  n = 0;
  nextaddr = 0;

  if (startaddr <= 0xffff) {
    addr_width = 2;
    tmpl="S9%02X%04X";
  }
  else if (startaddr <= 0xffffff) {
    addr_width = 3;
    tmpl="S9%02X%06X";
  }
  else if (startaddr <= 0xffffffff) {
    addr_width = 4;
    tmpl="S9%02X%08X";
  }

  fprintf(outf, tmpl, n + addr_width + 1, nextaddr);

  cksum += n + addr_width +1;
  for (i=addr_width; i>0; i--) 
    cksum += (nextaddr >> (i - 1) * 8) & 0xff;
  cksum = 0xff - cksum;
  fprintf(outf, "%02X\n", cksum);

  return nbytes; 
}


static int srec_readrec(struct ihexrec * srec, char * rec)
{
  int i, j;
  char buf[8];
  int offset, len, addr_width;
  char * e;
  unsigned char cksum;
  int rc;

  len = strlen(rec);
  offset = 1;
  cksum = 0;
  addr_width = 2;

  /* record type */
  if (offset + 1 > len) 
    return -1;
  srec->rectyp = rec[offset++];
  if (srec->rectyp == 0x32 || srec->rectyp == 0x38) 
    addr_width = 3;	/* S2,S8-record */
  else if (srec->rectyp == 0x33 || srec->rectyp == 0x37) 
    addr_width = 4;	/* S3,S7-record */

  /* reclen */
  if (offset + 2 > len) 
    return -1;
  for (i=0; i<2; i++) 
    buf[i] = rec[offset++];
  buf[i] = 0;
  srec->reclen = strtoul(buf, &e, 16);
  cksum += srec->reclen;
  srec->reclen -= (addr_width+1);
  if (e == buf || *e != 0) 
    return -1;

  /* load offset */
  if (offset + addr_width > len) 
    return -1;
  for (i=0; i<addr_width*2; i++) 
    buf[i] = rec[offset++];
  buf[i] = 0;
  srec->loadofs = strtoull(buf, &e, 16);
  if (e == buf || *e != 0) 
    return -1;

  for (i=addr_width; i>0; i--)
    cksum += (srec->loadofs >> (i - 1) * 8) & 0xff;

  /* data */
  for (j=0; j<srec->reclen; j++) {
    if (offset+2  > len) 
      return -1;
    for (i=0; i<2; i++) 
      buf[i] = rec[offset++];
    buf[i] = 0;
    srec->data[j] = strtoul(buf, &e, 16);
    if (e == buf || *e != 0) 
      return -1;
    cksum += srec->data[j];
  }

  /* cksum */
  if (offset + 2 > len) 
    return -1;
  for (i=0; i<2; i++) 
    buf[i] = rec[offset++];
  buf[i] = 0;
  srec->cksum = strtoul(buf, &e, 16);
  if (e == buf || *e != 0) 
    return -1;

  rc = 0xff - cksum;
  return rc;
}


static int srec2b(char * infile, FILE * inf,
           AVRMEM * mem, int bufsize, unsigned int fileoffset)
{
  char buffer [ MAX_LINE_LEN ];
  unsigned int nextaddr, maxaddr;
  int i;
  int lineno;
  int len;
  struct ihexrec srec;
  int rc;
  int reccount;
  unsigned char datarec;

  char * msg = 0;

  lineno   = 0;
  maxaddr  = 0;
  reccount = 0;

  while (fgets((char *)buffer,MAX_LINE_LEN,inf)!=NULL) {
    lineno++;
    len = strlen(buffer);
    if (buffer[len-1] == '\n') 
      buffer[--len] = 0;
    if (buffer[0] != 0x53)
      continue;
    rc = srec_readrec(&srec, buffer);

    if (rc < 0) {
      avrdude_message(MSG_INFO, "%s: ERROR: invalid record at line %d of \"%s\"\n",
              progname, lineno, infile);
      return -1;
    }
    else if (rc != srec.cksum) {
      avrdude_message(MSG_INFO, "%s: ERROR: checksum mismatch at line %d of \"%s\"\n",
              progname, lineno, infile);
      avrdude_message(MSG_INFO, "%s: checksum=0x%02x, computed checksum=0x%02x\n",
              progname, srec.cksum, rc);
      return -1;
    }

    datarec=0; 
    switch (srec.rectyp) {
      case 0x30: /* S0 - header record*/
        /* skip */
        break;

      case 0x31: /* S1 - 16 bit address data record */
        datarec=1;
        msg="%s: ERROR: address 0x%04x out of range %sat line %d of %s\n";
        break;

      case 0x32: /* S2 - 24 bit address data record */
        datarec=1;
        msg="%s: ERROR: address 0x%06x out of range %sat line %d of %s\n";
        break;

      case 0x33: /* S3 - 32 bit address data record */
        datarec=1;
        msg="%s: ERROR: address 0x%08x out of range %sat line %d of %s\n";
        break;

      case 0x34: /* S4 - symbol record (LSI extension) */
        avrdude_message(MSG_INFO, "%s: ERROR: not supported record at line %d of %s\n",
                        progname, lineno, infile);
        return -1;

      case 0x35: /* S5 - count of S1,S2 and S3 records previously tx'd */
        if (srec.loadofs != reccount){
          avrdude_message(MSG_INFO, "%s: ERROR: count of transmitted data records mismatch "
                          "at line %d of \"%s\"\n",
                          progname, lineno, infile);
          avrdude_message(MSG_INFO, "%s: transmitted data records= %d, expected "
                  "value= %d\n",
                  progname, reccount, srec.loadofs);
          return -1;
        }
        break;

      case 0x37: /* S7 Record - end record for 32 bit address data */
      case 0x38: /* S8 Record - end record for 24 bit address data */
      case 0x39: /* S9 Record - end record for 16 bit address data */
        return maxaddr;

      default:
        avrdude_message(MSG_INFO, "%s: ERROR: don't know how to deal with rectype S%d "
                        "at line %d of %s\n",
                        progname, srec.rectyp, lineno, infile);
        return -1;
    }

    if (datarec == 1) {
      nextaddr = srec.loadofs;
      if (nextaddr < fileoffset) {
        avrdude_message(MSG_INFO, msg, progname, nextaddr,
                "(below fileoffset) ",
                lineno, infile);
        return -1;
      }
      nextaddr -= fileoffset;
      if (nextaddr + srec.reclen > bufsize) {
        avrdude_message(MSG_INFO, msg, progname, nextaddr+srec.reclen, "",
                lineno, infile);
        return -1;
      }
      for (i=0; i<srec.reclen; i++) {
        mem->buf[nextaddr+i] = srec.data[i];
        mem->tags[nextaddr+i] = TAG_ALLOCATED;
      }
      if (nextaddr+srec.reclen > maxaddr)
        maxaddr = nextaddr+srec.reclen;
      reccount++;      
    }

  }

  avrdude_message(MSG_INFO, "%s: WARNING: no end of file record found for Motorola S-Records "
                  "file \"%s\"\n",
                  progname, infile);

  return maxaddr;
}

#ifdef HAVE_LIBELF
/*
 * Determine whether the ELF file section pointed to by `sh' fits
 * completely into the program header segment pointed to by `ph'.
 *
 * Assumes the section has been checked already before to actually
 * contain data (SHF_ALLOC, SHT_PROGBITS, sh_size > 0).
 *
 * Sometimes, program header segments might be larger than the actual
 * file sections.  On VM architectures, this is used to allow mmapping
 * the entire ELF file "as is" (including things like the program
 * header table itself).
 */
static inline
int is_section_in_segment(Elf32_Shdr *sh, Elf32_Phdr *ph)
{
    if (sh->sh_offset < ph->p_offset)
        return 0;
    if (sh->sh_offset + sh->sh_size > ph->p_offset + ph->p_filesz)
        return 0;
    return 1;
}

/*
 * Return the ELF section descriptor that corresponds to program
 * header `ph'.  The program header is expected to be of p_type
 * PT_LOAD, and to have a nonzero p_filesz.  (PT_LOAD sections with a
 * zero p_filesz are typically RAM sections that are not initialized
 * by file data, e.g. ".bss".)
 */
static Elf_Scn *elf_get_scn(Elf *e, Elf32_Phdr *ph, Elf32_Shdr **shptr)
{
  Elf_Scn *s = NULL;

  while ((s = elf_nextscn(e, s)) != NULL) {
    Elf32_Shdr *sh;
    size_t ndx = elf_ndxscn(s);
    if ((sh = elf32_getshdr(s)) == NULL) {
      avrdude_message(MSG_INFO, "%s: ERROR: Error reading section #%u header: %s\n",
                      progname, (unsigned int)ndx, elf_errmsg(-1));
      continue;
    }
    if ((sh->sh_flags & SHF_ALLOC) == 0 ||
        sh->sh_type != SHT_PROGBITS)
      /* we are only interested in PROGBITS, ALLOC sections */
      continue;
    if (sh->sh_size == 0)
      /* we are not interested in empty sections */
      continue;
    if (is_section_in_segment(sh, ph)) {
      /* yeah, we found it */
      *shptr = sh;
      return s;
    }
  }

  avrdude_message(MSG_INFO, "%s: ERROR: Cannot find a matching section for "
                  "program header entry @p_vaddr 0x%x\n",
                  progname, ph->p_vaddr);
  return NULL;
}

static int elf_mem_limits(AVRMEM *mem, struct avrpart * p,
                          unsigned int *lowbound,
                          unsigned int *highbound,
                          unsigned int *fileoff)
{
  int rv = 0;

  if (p->flags & AVRPART_AVR32) {
    if (strcmp(mem->desc, "flash") == 0) {
      *lowbound = 0x80000000;
      *highbound = 0xffffffff;
      *fileoff = 0;
    } else {
      rv = -1;
    }
  } else {
    if (strcmp(mem->desc, "flash") == 0 ||
        strcmp(mem->desc, "boot") == 0 ||
        strcmp(mem->desc, "application") == 0 ||
        strcmp(mem->desc, "apptable") == 0) {
      *lowbound = 0;
      *highbound = 0x7ffff;       /* max 8 MiB */
      *fileoff = 0;
    } else if (strcmp(mem->desc, "eeprom") == 0) {
      *lowbound = 0x810000;
      *highbound = 0x81ffff;      /* max 64 KiB */
      *fileoff = 0;
    } else if (strcmp(mem->desc, "lfuse") == 0) {
      *lowbound = 0x820000;
      *highbound = 0x82ffff;
      *fileoff = 0;
    } else if (strcmp(mem->desc, "hfuse") == 0) {
      *lowbound = 0x820000;
      *highbound = 0x82ffff;
      *fileoff = 1;
    } else if (strcmp(mem->desc, "efuse") == 0) {
      *lowbound = 0x820000;
      *highbound = 0x82ffff;
      *fileoff = 2;
    } else if (strncmp(mem->desc, "fuse", 4) == 0 &&
               (mem->desc[4] >= '0' && mem->desc[4] <= '9')) {
      /* Xmega fuseN */
      *lowbound = 0x820000;
      *highbound = 0x82ffff;
      *fileoff = mem->desc[4] - '0';
    } else if (strncmp(mem->desc, "lock", 4) == 0) {
      *lowbound = 0x830000;
      *highbound = 0x83ffff;
      *fileoff = 0;
    } else {
      rv = -1;
    }
  }

  return rv;
}


static int elf2b(char * infile, FILE * inf,
                 AVRMEM * mem, struct avrpart * p,
                 int bufsize, unsigned int fileoffset)
{
  Elf *e;
  int rv = -1;
  unsigned int low, high, foff;

  if (elf_mem_limits(mem, p, &low, &high, &foff) != 0) {
    avrdude_message(MSG_INFO, "%s: ERROR: Cannot handle \"%s\" memory region from ELF file\n",
                    progname, mem->desc);
    return -1;
  }

  /*
   * The Xmega memory regions for "boot", "application", and
   * "apptable" are actually sub-regions of "flash".  Refine the
   * applicable limits.  This allows to select only the appropriate
   * sections out of an ELF file that contains section data for more
   * than one sub-segment.
   */
  if ((p->flags & AVRPART_HAS_PDI) != 0 &&
      (strcmp(mem->desc, "boot") == 0 ||
       strcmp(mem->desc, "application") == 0 ||
       strcmp(mem->desc, "apptable") == 0)) {
    AVRMEM *flashmem = avr_locate_mem(p, "flash");
    if (flashmem == NULL) {
      avrdude_message(MSG_INFO, "%s: ERROR: No \"flash\" memory region found, "
                      "cannot compute bounds of \"%s\" sub-region.\n",
                      progname, mem->desc);
      return -1;
    }
    /* The config file offsets are PDI offsets, rebase to 0. */
    low = mem->offset - flashmem->offset;
    high = low + mem->size - 1;
  }

  if (elf_version(EV_CURRENT) == EV_NONE) {
    avrdude_message(MSG_INFO, "%s: ERROR: ELF library initialization failed: %s\n",
                    progname, elf_errmsg(-1));
    return -1;
  }
  if ((e = elf_begin(fileno(inf), ELF_C_READ, NULL)) == NULL) {
    avrdude_message(MSG_INFO, "%s: ERROR: Cannot open \"%s\" as an ELF file: %s\n",
                    progname, infile, elf_errmsg(-1));
    return -1;
  }
  if (elf_kind(e) != ELF_K_ELF) {
    avrdude_message(MSG_INFO, "%s: ERROR: Cannot use \"%s\" as an ELF input file\n",
                    progname, infile);
    goto done;
  }

  size_t i, isize;
  const char *id = elf_getident(e, &isize);

  if (id == NULL) {
    avrdude_message(MSG_INFO, "%s: ERROR: Error reading ident area of \"%s\": %s\n",
                    progname, infile, elf_errmsg(-1));
    goto done;
  }

  const char *endianname;
  unsigned char endianess;
  if (p->flags & AVRPART_AVR32) {
    endianess = ELFDATA2MSB;
    endianname = "little";
  } else {
    endianess = ELFDATA2LSB;
    endianname = "big";
  }
  if (id[EI_CLASS] != ELFCLASS32 ||
      id[EI_DATA] != endianess) {
    avrdude_message(MSG_INFO, "%s: ERROR: ELF file \"%s\" is not a "
                    "32-bit, %s-endian file that was expected\n",
                    progname, infile, endianname);
    goto done;
  }

  Elf32_Ehdr *eh;
  if ((eh = elf32_getehdr(e)) == NULL) {
    avrdude_message(MSG_INFO, "%s: ERROR: Error reading ehdr of \"%s\": %s\n",
                    progname, infile, elf_errmsg(-1));
    goto done;
  }

  if (eh->e_type != ET_EXEC) {
    avrdude_message(MSG_INFO, "%s: ERROR: ELF file \"%s\" is not an executable file\n",
                    progname, infile);
    goto done;
  }

  const char *mname;
  uint16_t machine;
  if (p->flags & AVRPART_AVR32) {
    machine = EM_AVR32;
    mname = "AVR32";
  } else {
    machine = EM_AVR;
    mname = "AVR";
  }
  if (eh->e_machine != machine) {
    avrdude_message(MSG_INFO, "%s: ERROR: ELF file \"%s\" is not for machine %s\n",
                    progname, infile, mname);
    goto done;
  }
  if (eh->e_phnum == 0xffff /* PN_XNUM */) {
    avrdude_message(MSG_INFO, "%s: ERROR: ELF file \"%s\" uses extended "
                    "program header numbers which are not expected\n",
                    progname, infile);
    goto done;
  }

  Elf32_Phdr *ph;
  if ((ph = elf32_getphdr(e)) == NULL) {
    avrdude_message(MSG_INFO, "%s: ERROR: Error reading program header table of \"%s\": %s\n",
                    progname, infile, elf_errmsg(-1));
    goto done;
  }

  size_t sndx;
  if (elf_getshdrstrndx(e, &sndx) != 0) {
    avrdude_message(MSG_INFO, "%s: ERROR: Error obtaining section name string table: %s\n",
                    progname, elf_errmsg(-1));
    sndx = 0;
  }

  /*
   * Walk the program header table, pick up entries that are of type
   * PT_LOAD, and have a non-zero p_filesz.
   */
  for (i = 0; i < eh->e_phnum; i++) {
    if (ph[i].p_type != PT_LOAD ||
        ph[i].p_filesz == 0)
      continue;

    avrdude_message(MSG_NOTICE2, "%s: Considering PT_LOAD program header entry #%d:\n"
                    "    p_vaddr 0x%x, p_paddr 0x%x, p_filesz %d\n",
                    progname, i, ph[i].p_vaddr, ph[i].p_paddr, ph[i].p_filesz);

    Elf32_Shdr *sh;
    Elf_Scn *s = elf_get_scn(e, ph + i, &sh);
    if (s == NULL)
      continue;

    if ((sh->sh_flags & SHF_ALLOC) && sh->sh_size) {
      const char *sname;

      if (sndx != 0) {
        sname = elf_strptr(e, sndx, sh->sh_name);
      } else {
        sname = "*unknown*";
      }

      unsigned int lma;
      lma = ph[i].p_paddr + sh->sh_offset - ph[i].p_offset;

      avrdude_message(MSG_NOTICE2, "%s: Found section \"%s\", LMA 0x%x, sh_size %u\n",
                      progname, sname, lma, sh->sh_size);

      if (lma >= low &&
          lma + sh->sh_size < high) {
        /* OK */
      } else {
        avrdude_message(MSG_NOTICE2, "    => skipping, inappropriate for \"%s\" memory region\n",
                        mem->desc);
        continue;
      }
      /*
       * 1-byte sized memory regions are special: they are used for fuse
       * bits, where multiple regions (in the config file) map to a
       * single, larger region in the ELF file (e.g. "lfuse", "hfuse",
       * and "efuse" all map to ".fuse").  We silently accept a larger
       * ELF file region for these, and extract the actual byte to write
       * from it, using the "foff" offset obtained above.
       */
      if (mem->size != 1 &&
          sh->sh_size > mem->size) {
        avrdude_message(MSG_INFO, "%s: ERROR: section \"%s\" does not fit into \"%s\" memory:\n"
                        "    0x%x + %u > %u\n",
                        progname, sname, mem->desc,
                        lma, sh->sh_size, mem->size);
        continue;
      }

      Elf_Data *d = NULL;
      while ((d = elf_getdata(s, d)) != NULL) {
        avrdude_message(MSG_NOTICE2, "    Data block: d_buf %p, d_off 0x%x, d_size %d\n",
                        d->d_buf, (unsigned int)d->d_off, d->d_size);
        if (mem->size == 1) {
          if (d->d_off != 0) {
            avrdude_message(MSG_INFO, "%s: ERROR: unexpected data block at offset != 0\n",
                            progname);
          } else if (foff >= d->d_size) {
            avrdude_message(MSG_INFO, "%s: ERROR: ELF file section does not contain byte at offset %d\n",
                            progname, foff);
          } else {
            avrdude_message(MSG_NOTICE2, "    Extracting one byte from file offset %d\n",
                            foff);
            mem->buf[0] = ((unsigned char *)d->d_buf)[foff];
            mem->tags[0] = TAG_ALLOCATED;
            rv = 1;
          }
        } else {
          unsigned int idx;

          idx = lma - low + d->d_off;
          if ((int)(idx + d->d_size) > rv)
            rv = idx + d->d_size;
          avrdude_message(MSG_DEBUG, "    Writing %d bytes to mem offset 0x%x\n",
                          d->d_size, idx);
          memcpy(mem->buf + idx, d->d_buf, d->d_size);
          memset(mem->tags + idx, TAG_ALLOCATED, d->d_size);
        }
      }
    }
  }
done:
  (void)elf_end(e);
  return rv;
}
#endif  /* HAVE_LIBELF */

/*
 * Simple itoa() implementation.  Caller needs to allocate enough
 * space in buf.  Only positive integers are handled.
 */
static char *itoa_simple(int n, char *buf, int base)
{
  div_t q;
  char c, *cp, *cp2;

  cp = buf;
  /*
   * Divide by base until the number disappeared, but ensure at least
   * one digit will be emitted.
   */
  do {
    q = div(n, base);
    n = q.quot;
    if (q.rem >= 10)
      c = q.rem - 10 + 'a';
    else
      c = q.rem + '0';
    *cp++ = c;
  } while (q.quot != 0);

  /* Terminate the string. */
  *cp-- = '\0';

  /* Now revert the result string. */
  cp2 = buf;
  while (cp > cp2) {
    c = *cp;
    *cp-- = *cp2;
    *cp2++ = c;
  }

  return buf;
}



static int fileio_rbin(struct fioparms * fio,
                  char * filename, FILE * f, AVRMEM * mem, int size)
{
  int rc;
  unsigned char *buf = mem->buf;

  switch (fio->op) {
    case FIO_READ:
      rc = fread(buf, 1, size, f);
      if (rc > 0)
        memset(mem->tags, TAG_ALLOCATED, rc);
      break;
    case FIO_WRITE:
      rc = fwrite(buf, 1, size, f);
      break;
    default:
      avrdude_message(MSG_INFO, "%s: fileio: invalid operation=%d\n",
              progname, fio->op);
      return -1;
  }

  if (rc < 0 || (fio->op == FIO_WRITE && rc < size)) {
    avrdude_message(MSG_INFO, "%s: %s error %s %s: %s; %s %d of the expected %d bytes\n",
                    progname, fio->iodesc, fio->dir, filename, strerror(errno),
                    fio->rw, rc, size);
    return -1;
  }

  return rc;
}


static int fileio_imm(struct fioparms * fio,
               char * filename, FILE * f, AVRMEM * mem, int size)
{
  int rc = 0;
  char * e, * p;
  unsigned long b;
  int loc;

  switch (fio->op) {
    case FIO_READ:
      loc = 0;
      p = strtok(filename, " ,");
      while (p != NULL && loc < size) {
        b = strtoul(p, &e, 0);
	/* check for binary formated (0b10101001) strings */
	b = (strncmp (p, "0b", 2))?
	    strtoul (p, &e, 0):
	    strtoul (p + 2, &e, 2);
        if (*e != 0) {
          avrdude_message(MSG_INFO, "%s: invalid byte value (%s) specified for immediate mode\n",
                          progname, p);
          return -1;
        }
        mem->buf[loc] = b;
        mem->tags[loc++] = TAG_ALLOCATED;
        p = strtok(NULL, " ,");
        rc = loc;
      }
      break;
    default:
      avrdude_message(MSG_INFO, "%s: fileio: invalid operation=%d\n",
              progname, fio->op);
      return -1;
  }

  if (rc < 0 || (fio->op == FIO_WRITE && rc < size)) {
    avrdude_message(MSG_INFO, "%s: %s error %s %s: %s; %s %d of the expected %d bytes\n",
                    progname, fio->iodesc, fio->dir, filename, strerror(errno),
                    fio->rw, rc, size);
    return -1;
  }

  return rc;
}


static int fileio_ihex(struct fioparms * fio, 
                  char * filename, FILE * f, AVRMEM * mem, int size)
{
  int rc;

  switch (fio->op) {
    case FIO_WRITE:
      rc = b2ihex(mem->buf, size, 32, fio->fileoffset, filename, f);
      if (rc < 0) {
        return -1;
      }
      break;

    case FIO_READ:
      rc = ihex2b(filename, f, mem, size, fio->fileoffset);
      if (rc < 0)
        return -1;
      break;

    default:
      avrdude_message(MSG_INFO, "%s: invalid Intex Hex file I/O operation=%d\n",
              progname, fio->op);
      return -1;
      break;
  }

  return rc;
}


static int fileio_srec(struct fioparms * fio,
                  char * filename, FILE * f, AVRMEM * mem, int size)
{
  int rc;

  switch (fio->op) {
    case FIO_WRITE:
      rc = b2srec(mem->buf, size, 32, fio->fileoffset, filename, f);
      if (rc < 0) {
        return -1;
      }
      break;

    case FIO_READ:
      rc = srec2b(filename, f, mem, size, fio->fileoffset);
      if (rc < 0)
        return -1;
      break;

    default:
      avrdude_message(MSG_INFO, "%s: ERROR: invalid Motorola S-Records file I/O "
              "operation=%d\n",
              progname, fio->op);
      return -1;
      break;
  }

  return rc;
}


#ifdef HAVE_LIBELF
static int fileio_elf(struct fioparms * fio,
                      char * filename, FILE * f, AVRMEM * mem,
                      struct avrpart * p, int size)
{
  int rc;

  switch (fio->op) {
    case FIO_WRITE:
      avrdude_message(MSG_INFO, "%s: ERROR: write operation not (yet) "
              "supported for ELF\n",
              progname);
      return -1;
      break;

    case FIO_READ:
      rc = elf2b(filename, f, mem, p, size, fio->fileoffset);
      return rc;

    default:
      avrdude_message(MSG_INFO, "%s: ERROR: invalid ELF file I/O "
              "operation=%d\n",
              progname, fio->op);
      return -1;
      break;
  }
}

#endif

static int fileio_num(struct fioparms * fio,
	       char * filename, FILE * f, AVRMEM * mem, int size,
	       FILEFMT fmt)
{
  const char *prefix;
  char cbuf[20];
  int base, i, num;

  switch (fmt) {
    case FMT_HEX:
      prefix = "0x";
      base = 16;
      break;

    default:
    case FMT_DEC:
      prefix = "";
      base = 10;
      break;

    case FMT_OCT:
      prefix = "0";
      base = 8;
      break;

    case FMT_BIN:
      prefix = "0b";
      base = 2;
      break;

  }

  switch (fio->op) {
    case FIO_WRITE:
      break;
    default:
      avrdude_message(MSG_INFO, "%s: fileio: invalid operation=%d\n",
              progname, fio->op);
      return -1;
  }

  for (i = 0; i < size; i++) {
    if (i > 0) {
      if (putc(',', f) == EOF)
	goto writeerr;
    }
    num = (unsigned int)(mem->buf[i]);
    /*
     * For a base of 8 and a value < 8 to convert, don't write the
     * prefix.  The conversion will be indistinguishable from a
     * decimal one then.
     */
    if (prefix[0] != '\0' && !(base == 8 && num < 8)) {
      if (fputs(prefix, f) == EOF)
	goto writeerr;
    }
    itoa_simple(num, cbuf, base);
    if (fputs(cbuf, f) == EOF)
      goto writeerr;
  }
  if (putc('\n', f) == EOF)
    goto writeerr;

  return 0;

 writeerr:
  avrdude_message(MSG_INFO, "%s: error writing to %s: %s\n",
	  progname, filename, strerror(errno));
  return -1;
}


int fileio_setparms(int op, struct fioparms * fp,
                    struct avrpart * p, AVRMEM * m)
{
  fp->op = op;

  switch (op) {
    case FIO_READ:
      fp->mode   = "r";
      fp->iodesc = "input";
      fp->dir    = "from";
      fp->rw     = "read";
      break;

    case FIO_WRITE:
      fp->mode   = "w";
      fp->iodesc = "output";
      fp->dir    = "to";
      fp->rw     = "wrote";
      break;

    default:
      avrdude_message(MSG_INFO, "%s: invalid I/O operation %d\n",
              progname, op);
      return -1;
      break;
  }

  /*
   * AVR32 devices maintain their load offset within the file itself,
   * but AVRDUDE maintains all memory images 0-based.
   */
  if ((p->flags & AVRPART_AVR32) != 0)
  {
    fp->fileoffset = m->offset;
  }
  else
  {
    fp->fileoffset = 0;
  }

  return 0;
}



static int fmt_autodetect(char * fname, unsigned section)
{
  FILE * f;
  unsigned char buf[MAX_LINE_LEN];
  int i;
  int len;
  int found;
  int first = 1;

#if defined(WIN32NATIVE)
  f = fopen_and_seek(fname, "r", section);
#else
  f = fopen_and_seek(fname, "rb", section);
#endif

  if (f == NULL) {
    avrdude_message(MSG_INFO, "%s: error opening %s: %s\n",
            progname, fname, strerror(errno));
    return -1;
  }

  while (fgets((char *)buf, MAX_LINE_LEN, f)!=NULL) {
    /* check for ELF file */
    if (first &&
        (buf[0] == 0177 && buf[1] == 'E' &&
         buf[2] == 'L' && buf[3] == 'F')) {
      fclose(f);
      return FMT_ELF;
    }

    buf[MAX_LINE_LEN-1] = 0;
    len = strlen((char *)buf);
    if (buf[len-1] == '\n')
      buf[--len] = 0;

    /* check for binary data */
    found = 0;
    for (i=0; i<len; i++) {
      if (buf[i] > 127) {
        found = 1;
        break;
      }
    }
    if (found) {
      fclose(f);
      return FMT_RBIN;
    }

    /* check for lines that look like intel hex */
    if ((buf[0] == ':') && (len >= 11)) {
      found = 1;
      for (i=1; i<len; i++) {
        if (!isxdigit(buf[1])) {
          found = 0;
          break;
        }
      }
      if (found) {
        fclose(f);
        return FMT_IHEX;
      }
    }

    /* check for lines that look like motorola s-record */
    if ((buf[0] == 'S') && (len >= 10) && isdigit(buf[1])) {
      found = 1;
      for (i=1; i<len; i++) {
        if (!isxdigit(buf[1])) {
          found = 0;
          break;
        }
      }
      if (found) {
        fclose(f);
        return FMT_SREC;
      }
    }

    first = 0;
  }

  fclose(f);
  return -1;
}



int fileio(int op, char * filename, FILEFMT format, 
             struct avrpart * p, char * memtype, int size, unsigned section)
{
  int rc;
  FILE * f;
  char * fname;
  struct fioparms fio;
  AVRMEM * mem;
  int using_stdio;

  mem = avr_locate_mem(p, memtype);
  if (mem == NULL) {
    avrdude_message(MSG_INFO, "fileio(): memory type \"%s\" not configured for device \"%s\"\n",
                    memtype, p->desc);
    return -1;
  }

  rc = fileio_setparms(op, &fio, p, mem);
  if (rc < 0)
    return -1;

  if (fio.op == FIO_READ)
    size = mem->size;

  if (fio.op == FIO_READ) {
    /* 0xff fill unspecified memory */
    memset(mem->buf, 0xff, size);
  }
  memset(mem->tags, 0, size);

  using_stdio = 0;

  if (strcmp(filename, "-")==0) {
    return -1;
    // Note: we don't want to read stdin or write to stdout as part of Slic3r
    // if (fio.op == FIO_READ) {
    //   fname = "<stdin>";
    //   f = stdin;
    // }
    // else {
    //   fname = "<stdout>";
    //   f = stdout;
    // }
    // using_stdio = 1;
  }
  else {
    fname = filename;
    f = NULL;
  }

  if (format == FMT_AUTO) {
    int format_detect;

    if (using_stdio) {
      avrdude_message(MSG_INFO, "%s: can't auto detect file format when using stdin/out.\n"
                      "%s  Please specify a file format and try again.\n",
                      progname, progbuf);
      return -1;
    }

    format_detect = fmt_autodetect(fname, section);
    if (format_detect < 0) {
      avrdude_message(MSG_INFO, "%s: can't determine file format for %s, specify explicitly\n",
                      progname, fname);
      return -1;
    }
    format = format_detect;

    if (quell_progress < 2) {
      avrdude_message(MSG_INFO, "%s: %s file %s auto detected as %s\n",
              progname, fio.iodesc, fname, fmtstr(format));
    }
  }

#if defined(WIN32NATIVE)
  /* Open Raw Binary and ELF format in binary mode on Windows.*/
  if(format == FMT_RBIN || format == FMT_ELF)
  {
      if(fio.op == FIO_READ)
      {
          fio.mode = "rb";
      }
      if(fio.op == FIO_WRITE)
      {
          fio.mode = "wb";
      }
  }
#endif

  if (format != FMT_IMM) {
    if (!using_stdio) {
      f = fopen_and_seek(fname, fio.mode, section);
      if (f == NULL) {
        avrdude_message(MSG_INFO, "%s: can't open %s file %s: %s\n",
                progname, fio.iodesc, fname, strerror(errno));
        return -1;
      }
    }
  }

  switch (format) {
    case FMT_IHEX:
      rc = fileio_ihex(&fio, fname, f, mem, size);
      break;

    case FMT_SREC:
      rc = fileio_srec(&fio, fname, f, mem, size);
      break;

    case FMT_RBIN:
      rc = fileio_rbin(&fio, fname, f, mem, size);
      break;

    case FMT_ELF:
#ifdef HAVE_LIBELF
      rc = fileio_elf(&fio, fname, f, mem, p, size);
#else
      avrdude_message(MSG_INFO, "%s: can't handle ELF file %s, "
                      "ELF file support was not compiled in\n",
                      progname, fname);
      rc = -1;
#endif
      break;

    case FMT_IMM:
      rc = fileio_imm(&fio, fname, f, mem, size);
      break;

    case FMT_HEX:
    case FMT_DEC:
    case FMT_OCT:
    case FMT_BIN:
      rc = fileio_num(&fio, fname, f, mem, size, format);
      break;

    default:
      avrdude_message(MSG_INFO, "%s: invalid %s file format: %d\n",
              progname, fio.iodesc, format);
      return -1;
  }

  if (rc > 0) {
    if ((op == FIO_READ) && (strcasecmp(mem->desc, "flash") == 0 ||
                             strcasecmp(mem->desc, "application") == 0 ||
                             strcasecmp(mem->desc, "apptable") == 0 ||
                             strcasecmp(mem->desc, "boot") == 0)) {
      /*
       * if we are reading flash, just mark the size as being the
       * highest non-0xff byte
       */
      rc = avr_mem_hiaddr(mem);
    }
  }
  if (format != FMT_IMM && !using_stdio) {
    fclose(f);
  }

  return rc;
}

