#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>

#define DEBUG 0
#define TEST_ON 0
#define dbgprintf(...) if(DEBUG) {printf(__VA_ARGS__);}

typedef struct{
    char **argv;
}Command;

int lexer(char *line, char ***args, int *num_args, int *num_loops);
int is_valid_number(char *given);
int built_in_commands(char *arg);
void execute_input(char *input);
void fork_pipes(Command *pipe_list, int num_pipes);
int process_pipe(int input_fd, int output_fd, Command *cmd);
void split_arg(char **args, int num_args, Command **pipe_list, int *num_pipes);
void execute_pipeline(char **commands, int num_commands);
void execute_cd(char **args);
void execute_pwd();
void execute_exit();
void print_error();
void print_message(int code);


int is_terminated = 0;
char *redirection;

int max(int a, int b) {
    return (a > b) ? a : b;
}

int main(int argc, char* argv[]){
    if(argc != 1){
        print_error();
        exit(0);
    }

    char **init_path = malloc(sizeof(char*) * 2);
    init_path[0] = "cd";
    init_path[1] = "/bin";
    // execute_cd(init_path);
    free(init_path);

    char *line = NULL, *command, **args;
    size_t line_size = 0;
    ssize_t line_length;
    int num_lines = 0;

    while(is_terminated == 0){
        printf("smash> ");
        fflush(stdout);
        line_length = getline(&line, &line_size, stdin);
        if(line_length == -1){
            continue;
        }
        else if(line_length == 1){
            continue;
        }
        execute_input(line);
        // dbgprintf("executed\n");
    }
    free(line);
    return 0;
}

void execute_input(char *input){
    char *in = strdup(input);
    char *current_command = strtok(in, ";");
    int num_commands = 0;
    while(current_command != NULL){
        num_commands++;
        current_command = strtok(NULL, ";");
    }
    // printf("num_commands: %d\n", num_commands);
    char* commands[num_commands];
    free(in);
    current_command = strtok(input, ";");
    num_commands = 0;
    while(current_command != NULL){
        char *command_copy = strdup(current_command);
        // printf("%d\n", num_commands);
        if(command_copy == NULL){
            for(int i = 0; i < num_commands; i++){
                free(commands[i]);
            }
            print_error();
            return;
        }
        commands[num_commands++] = command_copy;
        current_command = strtok(NULL, ";");
    }

    // printf("commands read in finished\n");

    char **args;
    int num_args, result_code, has_error = 0, num_loops, redirect_index = -1;
    for(int i = 0; i < num_commands && (is_terminated != 1); i++){
        redirection = NULL;
        num_loops = 1;
        result_code = lexer(commands[i], &args, &num_args, &num_loops);
        if(result_code == -1){
            print_error();
            continue;
        }
        else if(num_args == 0){
            continue;
        }
        else{
            //printf("num_loops: %d\nnum_args: %d\n", num_loops, num_args);
            Command *pipe_list;
            int num_pipes;
            num_pipes = 1;
            if(!built_in_commands(args[0])) split_arg(args, num_args, &pipe_list, &num_pipes);
            int output_files[num_loops];
            for(int j = 0; j < num_loops && is_terminated != 1; j++){
                if(strcmp(args[0], "exit") == 0){
                    if(num_args == 1) execute_exit();
                    else print_error();
                    continue;
                }
                else if(strcmp(args[0], "pwd") == 0){
                    if(num_args == 1) execute_pwd();
                    else print_error();
                    continue;
                }
                else if(strcmp(args[0], "cd") == 0){
                    if(num_args == 2) execute_cd(args);
                    else print_error();
                    continue;
                }
                pid_t pid = fork();
                if(pid < 0){
                    print_error();
                    continue;
                }
                if(pid == 0){
                    if(redirection != NULL){
                        output_files[i] = open(redirection, O_WRONLY|O_CREAT|O_TRUNC, 0644);
                        if(output_files[i] == -1){
                            print_error();
                            exit(1);
                        }
                        dup2(output_files[i], STDOUT_FILENO);
                        close(output_files[i]);
                    }
                   if(num_pipes > 1) fork_pipes(pipe_list, num_pipes);
                   else execv(args[0], args);
                   print_error();
                   exit(1);
                }
                else{
                    int status;
                    waitpid(pid, &status, 0);
                }
            }
        }
        for(int j = 0; j < num_args; j++){
            free(args[j]);
        }
        free(args);
    }
}

void split_arg(char **args, int num_args, Command **pipe_list, int* num_pipes){
    int crt_len = 0, max_len = 0;
    for(int i = 0; i < num_args; i++){
        if(strcmp(args[i], "|") == 0){
            *num_pipes = (*num_pipes + 1);
            max_len = max(max_len, crt_len);
            crt_len = 0;
        }
        else{
            crt_len += 1;
        }
    }
    (*pipe_list) = malloc(sizeof(Command *) * (*num_pipes));
    for(int i = 0; i < (*num_pipes); i++){
        (*pipe_list)[i].argv = malloc(sizeof(char *) * (max_len + 1));
    }
    int pipe_counter = 0, arg_counter = 0;
    for(int i = 0; i < num_args; i++){
        // dbgprintf("pipe_counter: %d, arg_counter: %d\n", pipe_counter, arg_counter);
        if(strcmp(args[i], "|") == 0){
            (*pipe_list)[pipe_counter++].argv[arg_counter] = NULL;
            arg_counter = 0;
        }
        else{
            (*pipe_list)[pipe_counter].argv[arg_counter++] = args[i];
        }
    }
}

void fork_pipes(Command *pipe_list, int num_pipes){
    int input_fd = 0, output_fd;
    int fd[2];
    for(int i = 0; i < num_pipes - 1; i++){
        pipe(fd);
        process_pipe(input_fd, fd[1], pipe_list + i);
        close(fd[1]);
        input_fd = fd[0];
    }
    if(input_fd != 0){
        dup2(input_fd, 0);
    }
    execv(pipe_list[num_pipes-1].argv[0], pipe_list[num_pipes-1].argv);
}

int process_pipe(int input_fd, int output_fd, Command* cmd){
    pid_t pid = fork();
    // dbgprintf("pid: %d\n", pid);
    if(pid == 0){
        if (input_fd != 0) {
            dup2(input_fd, 0);
            close(input_fd);
        }
        if (output_fd != 1) {
            dup2(output_fd, 1);
            close(output_fd);
        }
        execv(cmd->argv[0], cmd->argv);
        exit(1);
    }
    return pid;
}

/// description: Takes a line and splits it into args similar to how argc
///              and argv work in main
/// line:        The line being split up. Will be mangled after completion
///              of the function<
/// args:        a pointer to an array of strings that will be filled and
///              allocated with the args from the line
/// num_args:    a point to an integer for the number of arguments in args
/// return:      returns 0 on success, and -1 on failure
int lexer(char *line, char ***args, int *num_args, int *num_loops){
    *num_args = 0;
    // count number of args
    char *l = strdup(line);
    if(l == NULL){
        return -1;
    }
    char *token = strtok(l, " \t\n");
    if(token == NULL){
        *num_args = 0;
        return 0;
    }
    while(token != NULL){
        *num_args = *num_args + 1;
        token = strtok(NULL, " \t\n");
    }
    free(l);

    // check if there is loop
    token = strtok(line, " \t\n");
    if(strcmp(token, "loop") == 0){
        token = strtok(NULL, " \t\n");
        if(*num_args >= 3 && is_valid_number(token)){
            *num_loops = atoi(token);
            *num_args = *num_args - 2;
            token = strtok(NULL, " \t\n");
        }
        else return -1;
    }
    dbgprintf("num_args: %d\n", *num_args + 1);
    // dbgprintf("num_loops: %d\n", *num_loops);
    // split line into args
    int current_arg = 0; 
    *args = malloc(sizeof(char **) * (*num_args + 1));
    while(token != NULL){
        
        dbgprintf("current_arg: %d, num_args: %d\n", current_arg, *num_args);
        char *token_copy = strdup(token);
        if(token_copy == NULL){
            for (int i = 0; i < current_arg; i++) {
                free((*args)[i]);
            }
            free(*args);
            return -1;
        }
        if(strcmp(token_copy, ">") == 0){
            if(current_arg != ((*num_args) - 2)){
                for (int i = 0; i < current_arg; i++) {
                    free((*args)[i]);
                }
                free(*args);
                return -1;
            }
            redirection = strtok(NULL, " \t\n");
            // dbgprintf("redirection: %s\n", redirection);
            *num_args = *num_args - 2;
            dbgprintf("current_arg: %d, num_args: %d\n", current_arg, *num_args);
            break;
        }
        else (*args)[current_arg++] = token_copy;
        token = strtok(NULL, " \t\n");
        // dbgprintf("0");
    }
    (*args)[*num_args] = NULL;
    // dbgprintf("len of args: %ld\n", sizeof(char**));
    // if(redirection != NULL){
    //     free((*args)[*num_args + 1]);
    //     dbgprintf("1\n");
    //     free((*args)[*num_args + 2]);
    //     dbgprintf("2\n");
    // }

    return 0;
}

int is_valid_number(char* given){
    if(given[0] == '-'){
        return 0;
    }
    for(int i = 0; given[i] != '\0'; i++){
        if(!isdigit(given[i])){
            return 0;
        }
    }
    return 1;
}

int built_in_commands(char *arg){
    return (strcmp(arg, "exit") == 0 || strcmp(arg, "pwd") == 0 || strcmp(arg, "cd") == 0);
}

void execute_cd(char **args) {
    if (args[1] == NULL) {
        print_error();
    } 
    else {
        if (chdir(args[1]) != 0) {
            print_error();
        }
    }
}

void execute_pwd() {
    char *cwd = getcwd(NULL, 0);
    if (cwd != NULL) {
        printf("%s\n", cwd);
        free(cwd);
    } 
    else {
        print_error();
    }
}

void execute_exit() {
    is_terminated = 1;
}

void print_message(int code){
    if(TEST_ON){
        char *info;
        sprintf(info, "%d\n", code);
        write(STDERR_FILENO, info, strlen(info));
    }
}

void print_error(){
    char error_message[30] = "An error has occurred\n";
    write(STDERR_FILENO, error_message, strlen(error_message)); 
}