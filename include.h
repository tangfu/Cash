/*Copyright (C) 2012 Tyrell Keene, Max Rose

  This file is part of Cash.

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
#ifndef INCLUDE_H
#define INCLUDE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <termios.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <limits.h>
#include <pwd.h>
#include <getopt.h>
#include <readline/readline.h>
#include <readline/history.h>

/* Prototypes */
void format_prompt(char*, int);
void print_usage(FILE*, int, const char *);
void get_options(int, char **);
void init_env(void);
void parse(char *, char **);
void parse_rc(void);
void rm_nl(char *, int);
void exit_clean(int, const char *);
void get_history_filename(void);
int open_log(void);
int built_ins(char **);
int write_history_file(char *);
int write_to_log(const char *, const char *);
int execute(char **);
char* strrplc(char*, const char *, char*);
/* End Prototypes */

#endif
