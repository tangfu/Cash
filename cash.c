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
#include "job_control.h"

/*Informational stuff*/
char *version_string = "cash-0.1";
const char *help_string = "Cash, version 0.1, Linux\n--help\t\t-h\tshow this help screen\n"
"--version\t-v\tshow version info\n"
"--restricted\t-r\trun restricted shell (no cd)\n"
"--no-history\t-n\tdont write history to file for this session\n"
"--logging\t-l\tthe shell will keep a log in /var/log/messages for this session\n"
"--verbose\t-V\tthe shell will write the log to both /var/log/messages and stderr.\n\t\t\tlogging will be turned on with verbose\n"
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
char *default_hist_name = "/.cash_history";
char *history_filename;

FILE *rc_file;
char *PS1;

/*Shell stuff*/
pid_t cash_pid, cash_pgid;
int cash_interactive, cash_terminal;

_Bool restricted;         /*For restricted shell, 1 is restricted, 0 is not. default 0*/
_Bool logging;            /*logging option, 1 is on, 0 is off. default 0*/
_Bool verbose;            /*verbose option, 1 is on, 0 is off. default 0*/
_Bool history;            /*History file on/off. 1 is on, 0 is off. default 1*/
_Bool specified_PS1;      /*The prompt that is specified in ~/.cashrc*/
_Bool read_rc;            /*Read rc on/off. 1 is yes, 0 is no. default 1*/

/* This is our structure to hold environment variables */
ENVIRONMENT *env;

/* Used to get status of input history*/
HIST_ENTRY **history_entry;
HISTORY_STATE * history_status;

/* Custom shell option strings */
const char* shell_user = "\\u";
const char* shell_cwd = "\\w";
const char* shell_host = "\\h";
const char* shell_version = "\\v";

const char* default_prompt = "\\v:\\w$ ";

extern int built_ins(char **);
extern void print_usage(FILE*, int, const char *);
extern void get_options(int, char **);

extern struct termios cash_tmodes;


void open_log(void){
  if(verbose){
    if(!logging)
      logging = 1;
    openlog(version_string, LOG_CONS|LOG_PID|LOG_PERROR, LOG_USER);
  }
  else
    openlog(version_string, LOG_CONS|LOG_PID, LOG_USER);  
}

/*
 * Here's our exit function. Just make sure everything
 * is freed, and any open files are closed. Added exit
 * message if you have anything to say, if you don't 
 * you can pass it null and nothing will be printed.
 */
void exit_clean(int ret_no, const char *exit_message){
  /*This is a bit hacky, try to append_history.
   *if that doesn't work, write history, which creates
   *the file, then writes to it. Should suffice for now. 
   */
  if(history){
    if(append_history(1000, history_filename) != 0) 
      if(write_history(history_filename) != 0){
	syslog(LOG_ERR,"could not write history: read or write error");
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
  if(logging){
    syslog(LOG_DEBUG, "shell exited");
    closelog();
  }
  if(exit_message)
    fprintf(stderr,"%s", exit_message);
  exit(ret_no);
}

void init_env(void){
  cash_terminal = STDIN_FILENO;
  cash_interactive = isatty(cash_terminal);
  if(cash_interactive){
    while(tcgetpgrp (cash_terminal) != (cash_pgid = getpgrp ()))
      kill (- cash_pgid, SIGTTIN);
    env = malloc(sizeof(ENVIRONMENT));
    if(!env){
      perror("could not allocate memory to environemnt structure\n");
      if(logging){
	syslog(LOG_CRIT,"could not allocate memory to environment structure");
	exit_clean(1, "init failure\n");
      }
    }else{
      signal (SIGINT, SIG_IGN);
      signal (SIGQUIT, SIG_IGN);
      signal (SIGTSTP, SIG_IGN);
      signal (SIGTTIN, SIG_IGN);
      signal (SIGTTOU, SIG_IGN);
      
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

      cash_pid = getpid();
      cash_pgid = getpid();
      if(setpgid(cash_pgid, cash_pgid) < 0){
	syslog(LOG_ERR,"could not spawn interactive shell");
	perror("could not spawn interactive shell, failed at init");
	exit_clean(1, NULL);
      }
      tcsetpgrp(cash_terminal, cash_pgid);
      tcgetattr(cash_terminal, &cash_tmodes);
      if(logging)
	syslog(LOG_DEBUG, "shell spawned");
    }
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
    syslog(LOG_DEBUG, "rc file was not found or could not be opened");
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

int execute(char **argv){
  pid_t pid;
  int status;
  if ((pid = fork()) < 0) {     
    perror("fork");
    if(logging)
      syslog(LOG_ERR,"failed to fork");
    return -1;
  }
  else if (pid == 0) {          
    if((execvp(*argv, argv)) < 0) {    
      if(logging)
	syslog(LOG_ERR,"failed to execute %s", *argv);
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

int main(int argc, char* arg[]){
  char fmt_PS1[4096];
  logging     = 0;
  restricted  = 0;
  verbose     = 0;
  read_rc     = 1;
  history     = 1;

  input = 0;

  get_options(argc,arg);
  
  if(history)
    using_history();
  if(logging){
    logging = 1;
    open_log();
  }

  init_env();
  
  if(read_rc)
    parse_rc();

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
	syslog(LOG_ERR, "could not get current working directory");
      else
	perror("get current directory");
    }
    memset(fmt_PS1, 0, 4096);
    format_prompt(fmt_PS1, 4096);
    input = readline(fmt_PS1);
    if(!input)
      exit_clean(1, "invalid input");
    if(strlen(input) < 1)
      continue;
    parse(input, argv);  
    if(history){
      if(!history_filename)
	get_history_filename();
      add_history(input);
    }
    if(built_ins(argv) == 1)
      continue;
    else
      execute(argv);

  }
}
