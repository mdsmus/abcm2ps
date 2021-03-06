/*
 * abcm2ps: a program to typeset tunes written in abc format using PostScript
 *
 * Copyright (C) 1998-2008 Jean-Fran�ois Moine
 *
 * Adapted from abc2ps-1.2.5:
 *  Copyright (C) 1996,1997  Michael Methfessel
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Original site:
 *	http://moinejf.free.fr/
 *
 * Original abc2ps:
 *	Michael Methfessel
 *	msm@ihp-ffo.de
 *	Institute for Semiconductor Physics, PO Box 409,
 *	D-15204 Frankfurt (Oder), Germany
 */

/* Main program abcm2ps.c */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#ifdef linux
#include <unistd.h>
#endif

#include "abcparse.h"
#include "abc2ps.h"

/* -- global variables -- */

INFO info, default_info;
unsigned char deco_glob[256], deco_tune[256];
struct SYMBOL *sym;		/* (points to the symbols of the current voice) */

int tunenum;			/* number of current tune */
int pagenum = 1;		/* current page in output file */

int in_page;

				/* switches modified by flags: */
int pagenumbers;		/* write page numbers ? */
int epsf;			/* for EPSF postscript output */
int showerror;		/* show the errors */

char outfn[STRL1];		/* output file name */
int  file_initialized;		/* for output file */
FILE *fout;			/* output file */
char *in_fname;			/* current input file name */
char *styd = DEFAULT_FDIR;	/* format search directory */
time_t mtime;			/* last modification time of the input file */

int s_argc;			/* command line arguments */
char **s_argv;

struct tblt_s *tblts[MAXTBLT];
struct cmdtblt_s cmdtblts[MAXCMDTBLT];
int ncmdtblt;

/* -- local variables -- */

static int def_fmt_done = 0;	/* default format read */
static struct SYMBOL notitle;

/* memory arena (for clrarena, lvlarena & getarena) */
#define MAXAREAL 2		/* max area levels:
				 * 0; global, 1: tune */
static int str_level;		/* current arena level */
static struct str_a {
	char	str[4096];	/* memory area */
	char	*p;		/* pointer in area */
	struct str_a *n;	/* next area */
	int	r;		/* remaining space in area */
} *str_r[MAXAREAL], *str_c[MAXAREAL];	/* root and current area pointers */

/* -- local functions -- */
static void do_filter(struct abctune *t,
		      char *sel);
static void do_select(struct abctune *t,
		      int first_tune,
		      int last_tune);
static void output_file(char *sel);
static void read_def_format(void);
static char *read_file(void);
static void set_page_format(void);
static void usage(void);
#ifdef linux
static void wherefmtdir(void);
#endif
static struct cmdtblt_s *cmdtblt_parse(char *p);
static void write_version(void);

/* -- main program -- */
int main(int argc,
	 char **argv)
{
	int j;
	char *p, *sel, c, *aaa;

	fprintf(stderr, "abcm2ps-" VERSION " (" VDATE ")\n");
	if (argc <= 1)
		usage();

	/* initialize */
	outfn[0] = '\0';
	clrarena(0);			/* global */
	clrarena(1);			/* tune */
	clear_buffer();
	abc_init((void *(*)(int size)) getarena, /* alloc */
		0,				/* free */
		(void (*)(int level)) lvlarena, /* new level */
		sizeof(struct SYMBOL) - sizeof(struct abcsym),
		0);				/* don't keep comments */
	memset(&info, 0, sizeof info);
	info['T' - 'A'] = &notitle;
	notitle.as.text = "T:";
	set_format();
	memcpy(&default_info, &info, sizeof default_info);
	s_argc = argc;
	s_argv = argv;

#ifdef linux
	/* if not set, try to find where is the default format directory */
	if (styd[0] == '\0')
		wherefmtdir();
#endif

	/* parse the arguments - finding a new file, treat the previous one */
	sel = 0;
	while (--argc > 0) {
		argv++;
		p = *argv;
		if ((c = *p) == '\0')
			continue;
		if (c == '-') {
			if (p[1] == '\0') {		/* '-' alone */
				if (in_fname != 0) {
					output_file(sel);
					sel = 0;
				}
				in_fname = "";		/* read from stdin */
				continue;
			}
			if (p[1] != '-' && p[strlen(p) - 1] == '-')
				c = '+'; /* switch off flags with '-x-' */
		}
		if (c == '+') {		/* switch off flags with '+' */
			while (*++p != '\0') {
				switch (*p) {
				case '-':
					break;
				case 'B':
					cfmt.barsperstaff = 0;
					lock_fmt(&cfmt.barsperstaff);
					break;
				case 'c':
					cfmt.continueall = 0;
					lock_fmt(&cfmt.continueall);
					break;
				case 'E': epsf = 0; break;
				case 'F': def_fmt_done = 1; break;
				case 'G':
					cfmt.graceslurs = 1;
					lock_fmt(&cfmt.graceslurs);
					break;
				case 'i':
					showerror = 0;
					break;
				case 'j':
				case 'k':
					cfmt.measurenb = -1;
					lock_fmt(&cfmt.measurenb);
					break;
				case 'l':
					cfmt.landscape = 0;
					lock_fmt(&cfmt.landscape);
					break;
				case 'M':
					cfmt.musiconly = 0;
					lock_fmt(&cfmt.musiconly);
					break;
				case 'N':
					pagenumbers = 0;
					break;
				case 'n':
					cfmt.writehistory = 0;
					lock_fmt(&cfmt.writehistory);
					break;
				case 'O':
					outfn[0] = '\0';
					break;
				case 'Q':
					cfmt.printtempo = 0;
					lock_fmt(&cfmt.printtempo);
					break;
				case 'r':
					cfmt.wordsoverrests = 0;
					break;
				case 'T': {
					struct cmdtblt_s *cmdtblt;
					aaa = p + 1;
					if (*aaa == '\0') {
						if (argc > 1
						    && argv[1][0] != '-') {
							aaa = *++argv;
							argc--;
						}
					} else {
						while (p[1] != '\0')	/* stop */
							p++;
						if (*p == '-')
							*p-- = '\0';	/* (not clean) */
					}
					cmdtblt = cmdtblt_parse(aaa);
					if (cmdtblt != 0)
						cmdtblt->active = 0;
					break;
				   }
				case 'x':
					cfmt.withxrefs = 0;
					lock_fmt(&cfmt.withxrefs);
					break;
				case '0':
					cfmt.splittune = 0;
					lock_fmt(&cfmt.splittune);
					break;
				case '1':
					cfmt.oneperpage = 0;
					lock_fmt(&cfmt.oneperpage);
					break;
				default:
					fprintf(stderr,
						"++++ Cannot switch off flag: +%c\n",
						*p);
					severity = 1;
					break;
				}
			}
			continue;
		}

		if (c == '-') {		     /* interpret a flag with '-' */
			if (p[1] == '-') {		/* long argument */
				p += 2;
				if (--argc <= 0) {
					fprintf(stderr,
						"++++ No argument for '--'\n");
					return 2;
				}
				argv++;
				interpret_fmt_line(p, *argv, 1);
				continue;
			}
			while ((c = *++p) != '\0') {
				switch (*p) {

					/* simple flags */
				case 'c':
					cfmt.continueall = 1;
					lock_fmt(&cfmt.continueall);
					break;
				case 'E': epsf = 1; break;
				case 'f':
					cfmt.flatbeams = 1;
					lock_fmt(&cfmt.flatbeams);
					break;
				case 'G':
					cfmt.graceslurs = 0;
					lock_fmt(&cfmt.graceslurs);
					break;
				case 'H': 
					if (fout == 0)
						set_page_format();
					print_format();
					return 0;
				case 'h': usage(); break;
				case 'i':
					showerror = 1;
					break;
				case 'l':
					cfmt.landscape = 1;
					lock_fmt(&cfmt.landscape);
					break;
				case 'M':
					cfmt.musiconly = 1;
					lock_fmt(&cfmt.musiconly);
					break;
				case 'n':
					cfmt.writehistory = 1;
					lock_fmt(&cfmt.writehistory);
					break;
				case 'Q':
					cfmt.printtempo = 1;
					lock_fmt(&cfmt.printtempo);
					break;
				case 'r':
					cfmt.wordsoverrests = 1;
					break;
				case 'u':
					cfmt.abc2pscompat = 1;
					lock_fmt(&cfmt.abc2pscompat);
					break;
				case 'V':
					write_version();
					return 0;
				case 'x':
					cfmt.withxrefs = 1;
					lock_fmt(&cfmt.withxrefs);
					break;
				case '0':
					cfmt.splittune = 1;
					lock_fmt(&cfmt.splittune);
					break;
				case '1':
					cfmt.oneperpage = 1;
					lock_fmt(&cfmt.oneperpage);
					break;
				case 'e':	/* filtering */
					if (sel != 0) {
						fprintf(stderr,
							"++++ Too many '-e'\n");
						return 2;
					}
					sel = p + 1;
					if (sel[0] == '\0') {
						if (--argc <= 0) {
							fprintf(stderr,
								"++++ No filter in '-e'\n");
							return 2;
						}
						argv++;
						sel = *argv;
					} else {
						while (p[1] != '\0')	/* stop */
							p++;
					}
					break;

					/* flag with optional parameter */
				case 'N':
					if (p[1] == 0
					    && (argc <= 1
					 	|| !isdigit((unsigned) argv[1][0]))) {
						pagenumbers = 2; /* old behaviour */
						break;
					}
					/* fall thru */
					/* flags with parameter.. */
				case 'a':
				case 'B':
				case 'b':
				case 'D':
				case 'd':
				case 'F':
				case 'I':
				case 'j':
				case 'k':
				case 'L':
				case 'm':
				case 'O':
				case 's':
				case 'T':
				case 'w':
					aaa = p + 1;
					if (*aaa == '\0') {
						aaa = *++argv;
						if (--argc <= 0
						    || (*aaa == '-' && c != 'O')) {
							fprintf(stderr,
								"++++ Missing parameter after flag -%c\n",
								c);
							return 2;
						}
					} else {
						while (p[1] != '\0')	/* stop */
							p++;
					}

					if (strchr("BbfjkLNsv", c)) {	    /* check num args */
						for (j = 0; j < strlen(aaa); j++)
							if (!strchr("0123456789.",
								    aaa[j])) {
								if (aaa[j] == 'b'
								    && aaa[j+1] == '\0'
								    && (c == 'j'
									|| c == 'k'))
									break;
								fprintf(stderr,
									"++++ Invalid parameter <%s> for flag -%c\n",
									aaa, c);
								return 2;
							}
					}

					switch (c) {
					case 'a':
						interpret_fmt_line("maxshrink",
								aaa, 1);
						break;
					case 'B':
						interpret_fmt_line("barsperstaff",
								aaa, 1);
						break;
					case 'b':
						interpret_fmt_line("measurefirst",
								aaa, 1);
						break;
					case 'D':
						styd = aaa;
						break;
					case 'd':
						interpret_fmt_line("staffsep",
								aaa, 1);
						break;
					case 'F':
						read_def_format();
						if (read_fmt_file(aaa) < 0) {
							fprintf(stderr,
								"++++ Cannot open format file: %s\n",
								aaa);
							severity = 1;
						}
						break;
					case 'I':
						interpret_fmt_line("indent",
								aaa, 1);
						break;
					case 'j':
					case 'k':
						sscanf(aaa, "%d", &cfmt.measurenb);
						lock_fmt(&cfmt.measurenb);
						if (aaa[strlen(aaa) - 1] == 'b')
							cfmt.measurebox = 1;
						else	cfmt.measurebox = 0;
						lock_fmt(&cfmt.measurebox);
						break;
					case 'L':
						interpret_fmt_line("encoding",
								aaa, 1);
						break;
					case 'm':
						interpret_fmt_line("leftmargin",
								aaa, 1);
						break;
					case 'N':
						sscanf(aaa, "%d", &pagenumbers);
						if (pagenumbers < 0 || pagenumbers > 4) {
							fprintf(stderr,
								"++++ '-N' value %s - changed to 2\n",
								aaa);
							pagenumbers = 2;
						}
						break;
					case 'O':
						strcpy(outfn, aaa);
						break;
					case 's':
						interpret_fmt_line("scale",
								aaa, 1);
						break;
					case 'T': {
						struct cmdtblt_s *cmdtblt;

						cmdtblt = cmdtblt_parse(aaa);
						if (cmdtblt != 0)
							cmdtblt->active = 1;
						break;
					    }
					case 'w':
						interpret_fmt_line("staffwidth",
								aaa, 1);
						break;
					}
					break;
				default:
					fprintf(stderr,
						"++++ Unknown flag: -%c ignored\n", c);
					severity = 1;
					break;
				}
			}
			continue;
		}

		if (strstr(p, ".fmt")) {	/* implicit -F */
			read_def_format();
			if (read_fmt_file(p) < 0) {
				fprintf(stderr,
					"++++ Cannot open format file: %s\n",
					p);
				severity = 1;
			}
			continue;
		}

		if (in_fname != 0) {
			output_file(sel);
			sel = 0;
		}
		in_fname = p;
	}

	if (in_fname != 0)
		output_file(sel);
	if (!epsf && fout == 0) {
		fprintf(stderr, "No input file specified\n");
		return 1;
	}
	close_output_file();
	return severity;
}

/* -- generate the current ABC file -- */
static void output_file(char *sel)
{
	struct abctune *t;
	char *file;

	/* read the file into memory */
	if ((file = read_file()) == 0) {
#if defined(unix) || defined(__unix__)
		perror("read_file");
#endif
		fprintf(stderr, "++++ Cannot read input file '%s'\n",
			in_fname);
		severity = 1;
		return;
	}

	/* initialize if not already done */
	if (fout == 0)
		set_page_format();
	memcpy(&info, &default_info, sizeof info);
	reset_deco();
	memcpy(&deco_tune, &deco_glob, sizeof deco_tune);
	if (!epsf)
		open_output_file();
	fprintf(stderr, "File %s\n", in_fname);
	t = abc_parse(file);
	free(file);

	if (sel)
		do_filter(t, sel);
	else	do_select(t,
			  -32000,
			  (int) ((unsigned) (~0) >> 1));
	/*abc_free(t);	(useless) */
}

/* -- do filtering on an input file -- */
static void do_filter(struct abctune *t, char *sel)
{
	int cur_sel, end_sel, n;

	for (;;) {
		if (sscanf(sel, "%d%n", &cur_sel, &n) != 1)
			break;
		sel += n;
		if (*sel == '-') {
			sel++;
			end_sel = (int) ((unsigned) (~0) >> 1);
			if (sscanf(sel, "%d%n", &end_sel, &n) != 1)
				end_sel = (unsigned) ~0 >> 1;
			else	sel += n;
		} else	end_sel = cur_sel;
		do_select(t, cur_sel, end_sel);
		if (*sel != ',')
			break;
		sel++;
	}
}

/* -- do a tune selection -- */
static void do_select(struct abctune *t,
		      int first_tune,
		      int last_tune)
{
	struct abcsym *s;
	int print_tune, i;

	while (t != 0) {
		i = first_tune - 1;
		for (s = t->first_sym; s != 0; s = s->next) {
			if (s->type == ABC_T_INFO
			    && s->text[0] == 'X') {
				if (sscanf(s->text, "X:%d", &i) != 1)
					i = 0;
				break;
			}
		}
		print_tune = i >= first_tune && i <= last_tune;
		if (print_tune
		    || !t->client_data) {	/* (parse the global symbols) */
			do_tune(t, !print_tune);
			t->client_data = (void *) 1;	/* treated */
		}
		t = t->next;
	}
}

/* -- read the default format -- */
static void read_def_format(void)
{
	if (def_fmt_done)
		return;
	def_fmt_done = 1;
	read_fmt_file("default.fmt");
}

/* -- read an input file -- */
static char *read_file(void)
{
	int fsize;
	FILE *fin;
	char *file;
static char new_file[256];

	if (in_fname[0] == '\0') {
		int offset;

		in_fname = "stdin";
		fsize = 8096;
		file = malloc(fsize);
		offset = 0;
		for (;;) {
			if (fread(&file[offset], 1, 8096, stdin) != 8096)
				break;
			fsize += 8096;
			file = realloc(file, fsize);
			offset += 8096;
		}
		if (ferror(stdin) != 0) {
			free(file);
			return 0;
		}
		time(&mtime);
	} else {
		struct stat sbuf;

		if ((fin = fopen(in_fname, "rb")) == 0) {
			if (strlen(in_fname) >= sizeof new_file - 4)
				return 0;
			sprintf(new_file, "%s.abc", in_fname);
			if ((fin = fopen(new_file, "rb")) == 0)
				return 0;
			in_fname = new_file;
		}
		if (fseek(fin, 0L, SEEK_END) < 0) {
			fclose(fin);
			return 0;
		}
		fsize = ftell(fin);
		rewind(fin);
		if ((file = malloc(fsize + 2)) == 0) {
			fclose(fin);
			return 0;
		}

		if (fread(file, 1, fsize, fin) != fsize) {
			fclose(fin);
			free(file);
			return 0;
		}
		file[fsize] = '\0';
		fstat(fileno(fin), &sbuf);
		memcpy(&mtime, &sbuf.st_mtime, sizeof mtime);
		fclose(fin);
	}
	return file;
}

/* -- set_page_format --- */
static void set_page_format(void)
{
	read_def_format();
	make_font_list();
}

/* -- set extension on a file identifier -- */
void strext(char *fid, char *ext)
{
	char *p, *q;

	if ((p = strrchr(fid, DIRSEP)) == 0)
		p = fid;
	if ((q = strrchr(p, '.')) == 0)
		strcat(p, ".");
	else	q[1] = '\0';
	strcat(p, ext);
}

/* -- display usage and exit -- */
static void usage(void)
{
	printf(	"ABC to Postscript translator.\n"
		"Usage: abcm2ps [options] file [file_options] ..\n"
		"where:\n"
		" file        input ABC file, or '-'\n"
		" options and file_options:\n"
		"  .output file options:\n"
		"     -E      produce EPSF output, one tune per file\n"
		"     -O fff  set outfile name to fff\n"
		"     -O =    make outfile name from infile/title\n"
		"     -i      indicate where are the errors\n"
		"  .output formatting:\n"
		"     -s xx   set scale factor to xx\n"
		"     -w xx   set staff width (cm/in/pt)\n"
		"     -m xx   set left margin (cm/in/pt)\n"
		"     -d xx   set staff separation (cm/in/pt)\n"
		"     -a xx   set max shrinkage to xx (between 0 and 1)\n"
		"     -F foo  read format file \"foo.fmt\"\n"
		"     -D bar  look for format files in directory \"bar\"\n"
		"  .output options:\n"
		"     -l      landscape mode\n"
		"     -I xx   indent 1st line (cm/in/pt)\n"
		"     -x      add xref numbers in titles\n"
		"     -M      don't output the lyrics\n"
		"     -n      include notes and history in output\n"
		"     -N n    set page numbering mode to n=\n"
		"             0=off 1=left 2=right 3=even left,odd right 4=even right,odd left\n"
		"     -1      write one tune per page\n"
		"     -G      no slur in grace notes\n"
		"     -j n[b] number the measures every n bars (or on the left if n=0)\n"
		"             if 'b', display in a box\n"
		"     -k n[b] same as '-j' (abc2ps compatibility)\n"
		"     -b n    set the first measure number to n\n"
		"     -f      have flat beams when bagpipe tunes\n"
		"     -T n[v]   output the tablature 'n' for voice 'v' / all voices\n"
		"  .line breaks:\n"
		"     -c      auto line break\n"
		"     -B n    break every n bars\n"
		"  .input file selection/options:\n"
		"     -e pattern\n"
		"             xref list of tunes to select\n"
		"     -u      abc2ps behaviour\n"
		"     -L n    set char encoding to Latin number n\n"
		"     -r      allow lyrics to be printed with rests\n"
		"  .help/configuration:\n"
		"     -V      show program version\n"
		"     -h      show this command summary\n"
		"     -H      show the format parameters\n");
	exit(0);
}

#ifdef linux
/* -- where is the default format directory -- */
static void wherefmtdir(void)
{
	char exe[512], *p;
	FILE *f;
	int l;

	if ((l = readlink("/proc/self/exe", exe, sizeof exe)) <= 0)
		return;
	if ((p = strrchr(exe, '/')) == 0)
		return;
	p++;
	if (p > &exe[5] && strncmp(p - 5, "/bin", 4) == 0) {
		strcpy(p - 4, "share/abcm2ps/");
		p += -4 + 14;
	} /* else, assume this is the source directory */

	/* check if a format file is present */
	strcpy(p, "tight.fmt");
	if ((f = fopen(exe, "r")) == 0)
		return;
	fclose(f);

	/* change the format directory */
	p[-1] = '\0';
	styd = strdup(exe);
}
#endif

/* -- parse the tablature command ('-T n[v]') -- */
static struct cmdtblt_s *cmdtblt_parse(char *p)
{
	struct cmdtblt_s *cmdtblt;
	short val;

	if (ncmdtblt >= MAXCMDTBLT) {
		fprintf(stderr, "++++ Too many '-T'\n");
		return 0;
	}
	if (*p == '\0')
		val = -1;
	else {
		val = *p++ - '0' - 1;
		if ((unsigned) val > MAXTBLT) {
			fprintf(stderr, "++++ Bad tablature number in '-T'\n");
			return 0;
		}
	}
	cmdtblt = &cmdtblts[ncmdtblt++];
	cmdtblt->index = val;
	cmdtblt->vn = p;
	return cmdtblt;
}

/* -- write the program version -- */
static void write_version(void)
{
	fprintf(stderr, "Compiled: " __DATE__ "\n"
	       "Options:"
#ifdef A4_FORMAT
		" A4_FORMAT"
#endif
#ifdef DECO_IS_ROLL
		" DECO_IS_ROLL"
#endif
#if !defined(A4_FORMAT) && !defined(DECO_IS_ROLL)
		" NONE"
#endif
		"\n");
	if (strlen(styd) > 0)
		fprintf(stderr, "Default format directory: %s\n", styd);
}

/* -- arena routines -- */
void clrarena(int level)
{
	struct str_a *a_p;

	if ((a_p = str_r[level]) == 0) {
		str_r[level] = a_p = malloc(sizeof *str_r[0]);
		a_p->n = 0;
	}
	str_c[level] = a_p;
	a_p->p = a_p->str;
	a_p->r = sizeof a_p->str;
}

int lvlarena(int level)
{
	int old_level;

	old_level = str_level;
	str_level = level;
	return old_level;
}

/* The area is 8 bytes aligned to handle correctly int and pointers access
 * on some machines as Sun Sparc. */
char *getarena(int len)
{
	char *p;
	struct str_a *a_p;

	a_p = str_c[str_level];
	len = (len + 7) & ~7;		/* align at 64 bits boundary */
	if (a_p->r < len) {
		if (a_p->n == 0) {
			a_p->n = malloc(sizeof *str_r[0]);
			a_p->n->n = 0;
		}
		str_c[str_level] = a_p = a_p->n;
		a_p->p = a_p->str;
		a_p->r = sizeof a_p->str;
	}
	p = a_p->p;
	a_p->p += len;
	a_p->r -= len;
	return p;
}
