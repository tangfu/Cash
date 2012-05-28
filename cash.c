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

#include "cash.h"

/*Informational stuff*/
char *version_string = "cash-1.0";
const char *help_string = "Cash, version 1.0, Linux\n--help\t\t-h\tshow this help screen\n"
"--version\t-v\tshow version info\n"
"--restricted\t-r\trun restricted shell (no cd)\n"
"--no-history\t-n\tdont write history to file for this session\n"
"--logging\t-l\tthe shell will keep a log in ~/.cash_log for this session\n"
"--verbose\t-V\tthe shell will write the log to both ~/cash_log and stderr.\n\t\t\tlogging will be turned on with verbose\n"
"--no-rc\t\t-R\tdon't read from the rc file for this session.\n";

const struct option long_options[] = {
  {"restricted", 0, NULL, 'r'},
  {"help",       0, NULL, 'h'},
  {"no-history", 0, NULL, 'n'},
  {"version",    0, NULL, 'v'},
  {"verbose",    0, NULL, 'V'},
  {"logging",    0, NULL, 'l'},
  {"no-rc",      0, NULL, 'R'},
  {NULL,         0, NULL,  0 }
};

/*For input*/
char line[4096];        
char *argv[4096];  
char* input;

const char *rc_filename = "/.cashrc";
const char *default_hist_name = "/.cash_history";
const char *log_filename = "/.cash_log";
char *history_filename;

FILE *log_file;
FILE *rc_file;
char *PS1;

/*Shell stuff*/
pid_t cash_pid, cash_pgid;
int cash_interactive, cash_terminal;

_Bool restricted;         /*For restricted shell, 1 is restricted, 0 is not. default 0*/
_Bool logging;            /*logging option, 1 is on, 0 is off. default 0*/
_Bool log_open;           /*log file open or not, 1 is open, 0 is, well, i'll let you figure that out*/
_Bool verbose;            /*verbose option, 1 is on, 0 is off. default 0*/
_Bool history;            /*History file on/off. 1 is on, 0 is off. default 1*/
_Bool specified_PS1;      /*The prompt that is specified in ~/.cashrc*/
_Bool read_rc;            /*Read rc on/off. 1 is yes, 0 is no. default 1*/

/* This is our structure to hold environment variables */
ENVIRONMENT *env;

/* Custom shell option strings */
const char* shell_user = "\\u";
const char* shell_cwd = "\\w";
const char* shell_host = "\\h";
const char* shell_version = "\\v";

const char* default_prompt = "\\v:\\w$ ";

extern int built_ins(char **);
extern void print_usage(FILE*, int, const char *);
extern void get_options(int, char **);

struct termios cash_tmodes;

#define SHELL_PANIC "1:PANIC"  /*Major error.*/
#define SHELL_OOPS  "2:OOPS"   /*Serious error.*/
#define SHELL_ALERT "3:ALERT"  /*Minor error.*/
#define SHELL_DEBUG "4:DEBUG"  /*Informational.*/

unsigned short int open_log(void){
  char *buf;
  if(!(buf = malloc(sizeof(char) * 4096)))
    return 1;
  strcpy(buf, env->home);
  strcat(buf, log_filename);
  if( !(log_file = fopen(buf, "a+"))){
    perror("log file");
    if(buf)
      free(buf);
    return 1;
  }
  else
    log_open = 1;
  if(buf)
    free(buf);
  return 0;
}

unsigned short int write_to_log(const char *priority, const char *message){
  if(!logging)
    return 1;
  else
    if(!log_open)
      open_log();
  if(verbose)
    fprintf(stderr,"%s  %s\n", priority, message);
  fprintf(log_file, "%s  %s\n", priority, message);
  return 0;
}

/*
 * Here's our exit function. Just make sure everything
 * is freed, and any open files are closed. Added exit
 * message if you have anything to say, if you don't 
 * you can pass it NULL and nothing will be printed.
 */
void exit_clean(int ret_no, const char *exit_message){
  /*This is a bit hacky, try to append_history.
   *if that doesn't work, write history, which creates
   *the file, then writes to it. Should suffice for now. 
   */
  if(history){
    if(append_history(1000, history_filename) != 0) 
      if(write_history(history_filename) != 0){
	write_to_log(SHELL_OOPS, "could not write history");
	perror("history");
      }
  }
  if(input)
    free(input);
  if(history_filename)
    free(history_filename);
  if(env)
    free(env);
  if(rc_file)
    fclose(rc_file);
  if(PS1 && specified_PS1 == 1)
    free(PS1);
  if(log_open)
    fclose(log_file);
  if(exit_message)
    fprintf(stderr,"%s", exit_message);
  exit(ret_no);
}

/*
 *This is where we get our environment variables and put them in env
 *with setenv. This is required for a functional shell, so this *can't* fail
 */

void init_env(void){
  if( !(env = malloc( sizeof( ENVIRONMENT ) )))   /* <- It almost looks like lisp!*/
    perror("malloc env");
  env->home    = getenv("HOME");
  env->logname = getenv("LOGNAME");
  env->path    = getenv("PATH");
  env->display = getenv("DISPLAY");
  env->term    = getenv("TERM");
  
  setenv("HOME",env->home,1);
  setenv("LOGNAME",env->logname,1);
  setenv("PATH",env->path,1);
  setenv("DISPLAY",env->display,1);
  setenv("TERM", env->term, 1);
}

void init_shell(void){
  cash_terminal = STDIN_FILENO;
  cash_interactive = isatty(cash_terminal);
  
  if(cash_interactive){
    while(tcgetpgrp (cash_terminal) != (cash_pgid = getpgrp ()))
      kill (- cash_pgid, SIGTTIN);

      signal (SIGINT, SIG_IGN);
      signal (SIGQUIT, SIG_IGN);
      signal (SIGTSTP, SIG_IGN);
      signal (SIGTTIN, SIG_IGN);
      signal (SIGTTOU, SIG_IGN);
      
      cash_pid = getpid();
      cash_pgid = getpid();
      if(setpgid(cash_pgid, cash_pgid) < 0){
	if(logging)
	  write_to_log(SHELL_PANIC, "could not spawn interactive shell");
	exit_clean(1, "could not spawn interactive shell\n");
      }
      else
	tcsetpgrp(cash_terminal, cash_pgid);
      tcgetattr(cash_terminal, &cash_tmodes);
  }
}


/* Replace the first instance of sub in dst with src, and return a pointer to that instance */
char* strrplc(char* dst, const char* sub, char* src){
  char* loc;
  char* buf;
  if((dst && sub && src) == 0)
    return NULL;
  loc = strstr(dst, sub);
  if(loc != NULL){
    buf = malloc(sizeof(char) * 8192);
    strcpy(buf, src);    
    strcat(buf, loc+strlen(sub));
    memcpy(loc, buf, strlen(buf));
    free(buf);
    return loc;
  } else
    return NULL;
}

void parse_rc(void){
  char* buf = malloc(sizeof(char) * 4096);
  int i = 0;
  char* p;
  if(env){
    strcpy(buf, env->home);
    strcat(buf, rc_filename);
  } else{
    free(buf);
    return;
  }
  if(!(rc_file = fopen(buf, "r"))){
    perror("rc file");
    write_to_log(SHELL_OOPS,"rc file was not found or could not be opened");
    free(buf);
    return;
  }
  memset(buf, 0, 4096);
  while(fgets(buf, 4096, rc_file)){
    if( (p = strstr(buf, "PS1 = \"")) ){
      p += strlen("PS1 = \"");
      while(p[i++] != '\"')
	continue;
      i--;
      if(!PS1)
	PS1 = malloc(sizeof(char) * (i+1));
      strncpy(PS1, p, i);
      PS1[i] = '\0';
      specified_PS1 = 1;
    }
  }
  free(buf);
}

void format_prompt(char* dst, int len){
  char buf[4096];
  strcpy(dst, PS1);
  strrplc(dst, shell_user, env->logname);
  memset(buf, 0, sizeof(buf));
  gethostname(buf, 4096);
  strrplc(dst, shell_host, buf);
  strrplc(dst, shell_cwd, env->cur_dir);
  strrplc(dst, shell_version, version_string);
}

void parse(char *line, char **argv){
  while (*line != '\0') {      
    while (*line == ' ' || *line == '\t' || *line == '\n')
      *line++ = '\0';   
    *argv++ = line;        
    while (*line != '\0' && *line != ' ' && 
	   *line != '\t' && *line != '\n') 
      line++;             
  }
  *argv = '\0'; 
}

unsigned short int execute(char **argv){
  pid_t pid;
  int status;
  if ((pid = fork()) < 0) {     
    perror("fork");
    if(logging)
      write_to_log(SHELL_OOPS,"failed to fork");
    return 1;
  }
  else if (pid == 0) {          
    if((execvp(*argv, argv)) < 0) {    
      if(logging)
	write_to_log(SHELL_OOPS, "failed to execute");
      perror("exec");
      abort();
    }
  }
  else {                               
    while (wait(&status) != pid)       
      ;
  }
  return 0;
}

void get_history_filename(void){
  history_filename = malloc(sizeof(char) * 4096);
  strcpy(history_filename, env->home);
  strcat(history_filename, default_hist_name);
}

int main(int argc, char* arg[]) {
  char fmt_PS1[4096];
  /*Defaults for switches, may or may not change after get_options*/
  logging     = 0;
  restricted  = 0;
  verbose     = 0;
  read_rc     = 1;
  history     = 1;
  input = 0;

  get_options(argc,arg);
  
  init_env();
  if(logging)
    open_log();
  init_shell();
  if(read_rc)
    parse_rc();
  if(history)
    using_history();
  if(!PS1){
    PS1 = (char *) default_prompt; 
    specified_PS1 = 0;
  }

  /*
   * Here's the main loop.
   * First we go ahead and get the current working directory
   * Next we see if the we're in a restricted shell, and change the
   * prompt accordingly. Then we get the string, and if all goes well 
   * with that, write the history. Then we remove the \n terminator and
   * feed the string to the parser. After everything is parsed we pass the
   * string to check for builtins. If it was a built in, there's no need 
   * to execute it, so we skip the execute function, otherwise we go ahead 
   * and execute it.
   */

  while(1){  
    if( (getcwd(env->cur_dir, sizeof(env->cur_dir)) == NULL)){
      if(logging)
	write_to_log(SHELL_OOPS,"could not get current working directory");
      perror("get current directory");
    }
    memset(fmt_PS1, 0, 4096);
    format_prompt(fmt_PS1, 4096);
    input = readline(fmt_PS1);
    if(!input)
      exit_clean(1, "invalid input");
    if(strlen(input) < 1)
      continue;
    if(history){
      if(!history_filename)
	get_history_filename();
      add_history(input);
    }
    parse(input, argv);  
    if(built_ins(argv) == 1)
      continue;
    execute(argv);
  }
}
