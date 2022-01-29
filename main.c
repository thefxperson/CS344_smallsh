// Parker Carlson - Assignment 3 smallsh
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

short get_cmd_type(char* full_cmd){
  //process string like csv with spaces as separator
  //preserve orginal string though
  //thus, no strtok, so use ptr math :)

  //find first space
  char* iter = full_cmd;
  // for some reason fgets places a CR (ASCII 10) before \0
  while((*iter) != ' ' && (*iter) !=  '\0' && (*iter) != 10){
    iter++;
  }

  //extract cmd w/o arguments
  short cmd_len =  1 + (iter - full_cmd); // word len + null
  char* cmd = malloc(cmd_len);

  strncpy(cmd, full_cmd, cmd_len-1);
  cmd[cmd_len-1] = '\0';

  // check if it's an internal cmd
  //exit -> 0; cd -> 1; status -> 2; other -> 3;
  short cmd_type = 3;
  if(strcmp("exit", cmd) == 0)
    cmd_type = 0;

  if(strcmp("cd", cmd) == 0)
    cmd_type = 1;

  if(strcmp("status", cmd) == 0)
    cmd_type = 2;

  // free memory
  free(cmd);
  
  return cmd_type;
}


//returns the number of characters a number takes to represent.
int get_num_length(int number){

  int char_len = 1;
  for(int i = number; number > 9; number /= 10){
    char_len++;
  }

  return char_len;
}


// reallocs passed cmd -- max length may change
char* expand_money(char* cmd){
  // iterate through string and expand first occurance of $$
  char* iter = cmd + 1;
  while((*iter) != '\0'){
    // check for duplicates in a trailing manner -> this prevents accessing memory
    // beyond bounds. also why iter is cmd +1 to start.
    if((*iter) == '$' && (*(iter - 1)) == '$'){
      // get current process_id
      int proc_id = getpid();

      // new string length = old string length - 2 ($$) + PID num length + 1 (\0)
      //                   = old string length + PID num length - 1
      int new_len = strlen(cmd) + get_num_length(proc_id) - 1;
      char* new_cmd = malloc(new_len);

      // copy until expansion point
      // first $ is at (iter-1) -> last char to copy is (iter-2), but num of chars is one more cause math
      strncpy(new_cmd, cmd, (iter-cmd)-1);
      sprintf(new_cmd+(iter-cmd)-1, "%d%s", proc_id, (iter+1));   // now print proc id and rest of string (to null char)

      free(cmd);
      return expand_money(new_cmd); // exit from loop as other $$ will be caught in recursion
    }

    iter++;
  }

  // if we make it here, there is no $$ in string, return OG
  return cmd;
}


// returns true if line is blank (comment or all spaces)
bool check_blank_line(char* cmd){
  char* iter = cmd;

  // check for comment
  if(*iter == '#')
    return true;

  // handle general blank line
  while(*iter != '\0'){
    // new line, carriage return, space
    if(*iter != '\n' && *iter != 10 && *iter != ' ')
      return false;

    iter++;
  }

  return true;
}


struct cmd_info {
  char** cmd_args;      //cmd_args[0] is actual command
  int argc;             // number of args
  char* input_file;     // null if not present in cmd
  char* output_file;    // null if not present in cmd
  bool background;      // true if cmd should be run in bg
};


// function to process info in a cmd to easy to use struct
// NOT SAFE -- "consumes" and modifies cmd
struct cmd_info* process_cmd(char* cmd){
  // create new empty struct
  struct cmd_info* procd_cmd = malloc(sizeof(struct cmd_info));
  procd_cmd->cmd_args = NULL;
  procd_cmd->argc = 0;
  procd_cmd->input_file = NULL;
  procd_cmd->output_file = NULL;
  procd_cmd->background = false;

  // count number of spaces to make tokenization easier
  // idgaf about speed
  char* iter = cmd;
  int num_toks = 1;           // avoid fencepost problem
  while((*iter) != '\0'){
    if((*iter) == ' ')
      num_toks++;

    iter++;
  }

  // then, process rest of input into array
  char** tokens = malloc(num_toks * sizeof(char*));
  char* token;
  char* saveptr = NULL;
  int next_in = 2050, next_out = 2050;      // use as bool check but also to store index of char
  // want false value to be large so we can take index minimum later for array copying
  // because cmd is max 2048 char (except for arg expansion), pick a number that's large
  // enough to be greater than the number of possible tokens as false, anything smaller is true
  iter = cmd;
  for(int i = 0; i < num_toks; i++){
    // for first call to strtok_r
    if(i == 0){
      token = strtok_r(cmd, " ", &saveptr);
    }else {
      token = strtok_r(NULL, " ", &saveptr);
    }
    tokens[i] = malloc(strlen(token)+1);
    strcpy(tokens[i], token);

    // check if current token is an input or output filename
    if(next_in != 2050 && procd_cmd->input_file == NULL) {
      procd_cmd->input_file = malloc(strlen(token)+1);
      strcpy(procd_cmd->input_file, token);
      // leave next_in as non-(-1) so it doesn't get processed twice
    }
    if(next_out != 2050 && procd_cmd->output_file == NULL) {
      procd_cmd->output_file = malloc(strlen(token)+1);
      strcpy(procd_cmd->output_file, token);
      // leave next_out as non-(-1) so it doesn't get processed twice
    }

    // now, check if there are input or output files to redirect to
    if(strlen(token) == 1 && (*token) == '<' && next_in == 2050){
      // next token is input filename
      next_in = i;
      continue;             // in case user inputs "< > > out.txt"
    }
    if(strlen(token) == 1 && (*token) == '>' && next_out == 2050){
      // next token is output filename
      next_out = i;
    }

    // detect if last token is bg indicator
    if(i == (num_toks-1) && (*token) == '&')
      procd_cmd->background = true;
  }

  //finally, copy tokens into procd_cmd
  procd_cmd->argc = num_toks;

  // adjust in case task is in background but not w/ redirection
  if(procd_cmd->background)
    procd_cmd->argc--;

  if(next_in != 2050 || next_out != 2050)
    procd_cmd->argc = (next_in < next_out) ? next_in : next_out;   // stop copying args at input/output file redirection

  // allocate and copy args from tokens to cmd struct
  procd_cmd->cmd_args = malloc((procd_cmd->argc+1)*sizeof(char*));
  for(int i = 0; i < procd_cmd->argc; i++){
    //realloc tokens so cleaning up original processing array is cleaner cause i'm lazy
    procd_cmd->cmd_args[i] = malloc(strlen(tokens[i])+1);
    strcpy(procd_cmd->cmd_args[i], tokens[i]);
  }
  procd_cmd->cmd_args[procd_cmd->argc] = NULL; // for execvp

  // clean memory
  for(int i = 0; i < num_toks; i++){
    free(tokens[i]);
  }
  free(tokens);

  return procd_cmd;
}


//safely cleans all memory from passed cmd_info struct
//requires cmd->argc to be correct
void delete_cmd_info(struct cmd_info* cmd){
  if(cmd == NULL)
    return;

  if(cmd->cmd_args != NULL){
    for(int i = 0; i < cmd->argc; i++){
      free(cmd->cmd_args[i]);
    }
    free(cmd->cmd_args);
  }

  if(cmd->input_file != NULL)
    free(cmd->input_file);

  if(cmd->output_file != NULL)
    free(cmd->output_file);

  free(cmd);
}


void print_cmd_info(struct cmd_info* cmd){
  if(cmd == NULL)
    return;

  printf("=========================\nNumber of Args: %d\nCMD: ", cmd->argc);
  for(int i = 0; i < cmd->argc; i++){
    printf("%s ", cmd->cmd_args[i]);
  }
  printf("\nInput redirect: %s\nOutput redirect: %s\nBackground %s\n\n",\
         cmd->input_file, cmd->output_file, (cmd->background) ? "true" : "false");
  fflush(stdout);
}


void my_cd(struct cmd_info* cmd){
  // check if default cd or if arg passed
  char* target = NULL;
  if(cmd->argc == 1){
    // cd to $HOME environment variable
    target = getenv("HOME");
  }else{
    target = cmd->cmd_args[1];      // ignore further args
  }

  // attempt to change dir
  int cd_result = chdir(target);
  char cwd[300];                    // for printing result
  if(cd_result == 0){
    printf("%s\n", getcwd(cwd, 300));
  }else{
    perror("ERROR - cd failed");
  }
  fflush(stdout);
}


void my_status(int status_code){
  printf("Exit value: %d\n", status_code);
}


// will redirect input from stdin to input_file 
// if input_file == NULL, redirect from dev/null
// return 0 on success, 1 on error
int redirect_input(char* input_file, int* fd){
  char target[2048];
  if(input_file == NULL){
    // redirect from dev/null
    target = "/dev/null";
  else{
    strcpy(target, input_file);
  }

  // open source file
  *fd = open(target, O_RDONLY);
  if(fd == -1){
    perror("redirect in open()");
    return 1;
  }

  // redirect input
  int result = dup2(fd, 0);
  if(result == -1){
    perror("redirect in dup2()");
    return 1;
  }

  return 0;
}


// will redirect output from stdout to output_file
// if output_file == NULL, redirect from dev/null
// return 0 on success, 1 on error
int redirect_output(char* output_file, int* fd){
  char target[2048];
  if(output_file == NULL){
    // redirect from dev/null
    target = "/dev/null";
  else{
    strcpy(target, output_file);
  }

  // open source file
  *fd = open(target, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if(fd == -1){
    perror("redirect out open()");
    return 1;
  }

  // redirect input
  int result = dup2(fd, 1);
  if(result == -1){
    perror("redirect out dup2()");
    return 1;
  }

  return 0;
}


// function to handle spawning and management of external (non-native) commands
// if return 0 -> fine
// else -> error, clean memory and exit main
int handle_extern(struct cmd_info* cmd, int* status){
  // change behavior based on if cmd is ran in foreground or background
  // handle input/output redirection
  if(!cmd->background){
    // run in foreground
    pid_t spawn_pid = -5;
    spawn_pid = fork();
    switch(spawn_pid){
      case -1:
        // fork failed
        perror("fork() failed");
        return 1;
      case 0:
        // child process
        execvp(cmd->cmd_args[0], cmd->cmd_args);
        // exec only returns on error
        perror("execvp");
        return 1;
      default:
        // parent process
        waitpid(spawn_pid, status, 0);
        break;
    }
  }else{
    // run in background
  }
  return 0;
}

int main(int argc, char** argv){
  // main loop for smallsh
  bool running = true;
  char* new_cmd = malloc(0);    //malloc placeholder so realloc works
  struct cmd_info* cmd = NULL;
  short cmd_code = 0;
  int status_code = 0;
  int proc_success = 0;
  while(running){
    // realloc new_cmd with proper length
    new_cmd = realloc(new_cmd, 2049); //2048 max plus null

    //display terminal prompt and get user input
    printf(": ");
    fflush(stdout);
    fgets(new_cmd, 2049, stdin);
    new_cmd[strlen(new_cmd)-1] = '\0';   //fgets adds carriage return '10' -- overwrite

    // check to see if line should be ignored
    if(check_blank_line(new_cmd))
      continue;

    // expand $$ into current process id
    new_cmd = expand_money(new_cmd);
    
    // determine if cmd is native or extern
    cmd_code = get_cmd_type(new_cmd);

    // process cmd and extract info
    cmd = process_cmd(new_cmd);

    // execute cmd
    switch(cmd_code) {
      case 0:
        // exit cmd
        // TODO: kill all runing processes
        running = false;
        break;
      case 1:
        /// cd cmd
        my_cd(cmd);
        break;
      case 2:
        // status cmd
        my_status(status_code);
        break;
      case 3:
        // external command
        print_cmd_info(cmd);
        proc_success = handle_extern(cmd, &status_code);
        if (proc_success != 0)   // this normally means execvp failed, clear mem in child proc and exit
          running = false;

        break;
      default:
        break;
    }

    // free memory
    delete_cmd_info(cmd);
  }

  // clean up memory
  free(new_cmd);
  return 0;
}
