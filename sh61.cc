#include "sh61.hh"
#include <cstring>
#include <cerrno>
#include <vector>
#include <sys/stat.h>
#include <sys/wait.h>

// For the love of God
#undef exit
#define exit __DO_NOT_CALL_EXIT__READ_PROBLEM_SET_DESCRIPTION__


// struct command
//    Data structure describing a command. Add your own stuff.

struct command {
    std::vector<std::string> args;
    std::string stdin_file;
    std::string stdout_file;
    std::string stderr_file;
    command* next = nullptr;
    command* prev = nullptr;
    pid_t pid = -1;
    pid_t run();
    int op = TYPE_SEQUENCE;
    int read_end;
    bool stdin_redir = false;
    bool stdout_redir = false;
    bool stderr_redir = false;
    bool is_foreground = false;
    command();
    ~command();
};


// command::command()
//    This constructor function initializes a `command` structure. You may
//    add stuff to it as you grow the command structure.

command::command() {
}


// command::~command()
//    This destructor function is called to delete a command.

command::~command() {
    delete this->next;
}


// COMMAND EXECUTION

// command::run()
//    Creates a single child process running the command in `this`, and
//    sets `this->pid` to the pid of the child process.
//
//    If a child process cannot be created, this function should call
//    `_exit(EXIT_FAILURE)` (that is, `_exit(1)`) to exit the containing
//    shell or subshell. If this function returns to its caller,
//    `this->pid > 0` must always hold.
//
//    Note that this function must return to its caller *only* in the parent
//    process. The code that runs in the child process must `execvp` and/or
//    `_exit`.
//
//    PART 1: Fork a child process and run the command using `execvp`.
//       This will require creating a vector of `char*` arguments using
//       `this->args[N].c_str()`. Note that the last element of the vector
//       must be a `nullptr`.
//    PART 4: Set up a pipeline if appropriate. This may require creating a
//       new pipe (`pipe` system call), and/or replacing the child process's
//       standard input/output with parts of the pipe (`dup2` and `close`).
//       Draw pictures!
//    PART 7: Handle redirections.

pid_t command::run() {
    assert(this->pid == -1);
    assert(this->args.size() > 0);
    char* cargs[this->args.size() + 1];
    for (size_t i = 0; i < this->args.size(); ++i){
        cargs[i] = (char*) this->args[i].c_str();
    }
    cargs[this->args.size()] = nullptr;
    int pfd[2];
    if (this->op == TYPE_PIPE){
        int r = pipe(pfd);
        if (r == -1){
            fprintf(stderr, "Pipe syscall failed\n");
            return -1;
        }
        this->next->read_end = pfd[0];
    }
    if (this->args[0] == "cd"){
        int r = chdir(this->args[1].c_str());
        if (r != 0){
            return -1;
        }
        return 0;
    }
    int c = fork();
    if (c == -1){
        fprintf(stderr, "Fork syscall failed\n");
        return -1;
    }
    else if (c == 0){
        if (this->op == TYPE_PIPE){
            dup2(pfd[1], 1);
        }
        if (this->prev && this->prev->op == TYPE_PIPE){
            dup2(this->read_end, 0);
        }
        if (this->stdin_redir) {
            int fd = open(this->stdin_file.c_str(), O_RDONLY|O_CLOEXEC);
            if (fd == -1) {
                fprintf(stderr,"No such file or directory\n");
                _exit(1);
            }
            dup2(fd, 0);
        }
        if (this->stdout_redir) {
            int fd = open(this->stdout_file.c_str(), O_CREAT|O_WRONLY|O_TRUNC, S_IRWXU);
            if (fd == -1) {
                fprintf(stderr,"No such file or directory\n");
                _exit(1);
            }
            dup2(fd, 1);
        }
        if (this->stderr_redir) {
            int fd = open(this->stderr_file.c_str(), O_CREAT|O_WRONLY|O_TRUNC, S_IRWXU);
            if (fd == -1) {
                fprintf(stderr,"No such file or directory\n");
                _exit(1);
            }
            dup2(fd, 2);
        }
        execvp(cargs[0], cargs);
        fprintf(stderr, "Process %d exited abnormally\n", c);
        _exit(1);
    }
    if (this->op == TYPE_PIPE){
        close(pfd[1]);
    }
    if (this->prev && this->prev->op == TYPE_PIPE){
        close(this->read_end);
    }
    this->pid = c;
    return this->pid;
}


// run_list(c)
//    Run the command *list* starting at `c`. Initially this just calls
//    `c->run()` and `waitpid`; you’ll extend it to handle command lists,
//    conditionals, and pipelines.
//
//    It is possible, and not too ugly, to handle lists, conditionals,
//    *and* pipelines entirely within `run_list`, but many students choose
//    to introduce `run_conditional` and `run_pipeline` functions that
//    are called by `run_list`. It’s up to you.
//
//    PART 1: Start the single command `c` with `c->run()`,
//        and wait for it to finish using `waitpid`.
//    The remaining parts may require that you change `struct command`
//    (e.g., to track whether a command is in the background)
//    and write code in `command::run` (or in helper functions).
//    PART 2: Introduce a loop to run a list of commands, waiting for each
//       to finish before going on to the next.
//    PART 3: Change the loop to handle conditional chains.
//    PART 4: Change the loop to handle pipelines. Start all processes in
//       the pipeline in parallel. The status of a pipeline is the status of
//       its LAST command.
//    PART 5: Change the loop to handle background conditional chains.
//       This may require adding another call to `fork()`!

int run_pipe(command* c) {
    pid_t p = -1;
    int s;
    bool is_foreground = c->is_foreground;
    while (c != nullptr){
        p = c->run();
        while (c && c->op != TYPE_PIPE && c->op != TYPE_BACKGROUND && c->op != TYPE_SEQUENCE 
               && c->op != TYPE_AND && c->op != TYPE_OR){
            c = c->next;
        }
        if (!c || c->op == TYPE_BACKGROUND || c->op == TYPE_SEQUENCE || c->op == TYPE_AND 
            || c->op == TYPE_OR){
            break;
        }
        c = c->next;
    }
    if (p != -1){
        waitpid(p, &s, 0);
    }
    else{
        s = -1;
    }
    return s;
}

void run_cond(command* c) {
    int s;
    while (c){
        s = run_pipe(c);
        do{
            while (c && c->op != TYPE_AND && c->op != TYPE_OR
                   && c->op != TYPE_SEQUENCE && c->op !=TYPE_BACKGROUND){
                c = c->next;
            }
            if (!c || c->op == TYPE_SEQUENCE || c->op == TYPE_BACKGROUND){
                return;
            }
            c = c->next;
        } 
        while(!((WIFEXITED(s) && WEXITSTATUS(s) == EXIT_SUCCESS && c->prev->op == TYPE_AND)
              || (WIFEXITED(s) && WEXITSTATUS(s) != EXIT_SUCCESS && c->prev->op == TYPE_OR)
              || (!WIFEXITED(s) && c->prev->op == TYPE_OR)));
    }
}

void run_list(command* c) {
   while (c){
        command* tmpc = c;
        while (tmpc->op != TYPE_SEQUENCE && tmpc->op != TYPE_BACKGROUND) {
            tmpc = tmpc->next;
        }
        if (tmpc->op == TYPE_BACKGROUND){
            pid_t p = fork();
            if (p == 0){
                run_cond(c);
                _exit(0);
            }
        }
        else {
            run_cond(c);
        }
        while (c->op != TYPE_BACKGROUND && c->op != TYPE_SEQUENCE){
            c = c->next;
        }
        c = c->next;
    }
}


// parse_line(s)
//    Parse the command list in `s` and return it. Returns `nullptr` if
//    `s` is empty (only spaces). You’ll extend it to handle more token
//    types.

command* parse_line(const char* s) {
    shell_parser parser(s);
    command* chd = nullptr;
    command* ctl = nullptr; 
    command* c = nullptr; 
    for (shell_token_iterator it = parser.begin(); it != parser.end(); ++it) {
        if (it.type() == TYPE_REDIRECT_OP) {
            assert(c);
            if (it.str() == ">"){
                ++it;
                c->stdout_redir = true;
                c->stdout_file = it.str();
            }
            else if (it.str() == "<"){
                ++it;
                c->stdin_redir = true;
                c->stdin_file = it.str();
            }
            else{
                ++it;
                c->stderr_redir = true;
                c->stderr_file = it.str();
            }
        }
        else if (it.type() == TYPE_NORMAL) {
            if (c == nullptr) {
                c = new command;
                if (!ctl) {
                    chd = c;
                } else {
                    c->prev = ctl;
                    ctl->next = c;
                }
            }
            c->args.push_back(it.str());
        }       
        else {
                ctl = c;
                ctl->op = it.type();
                c = nullptr;
        }
    }
    return chd;
}


int main(int argc, char* argv[]) {
    FILE* command_file = stdin;
    bool quiet = false;

    // Check for `-q` option: be quiet (print no prompts)
    if (argc > 1 && strcmp(argv[1], "-q") == 0) {
        quiet = true;
        --argc, ++argv;
    }

    // Check for filename option: read commands from file
    if (argc > 1) {
        command_file = fopen(argv[1], "rb");
        if (!command_file) {
            perror(argv[1]);
            return 1;
        }
    }

    // - Put the shell into the foreground
    // - Ignore the SIGTTOU signal, which is sent when the shell is put back
    //   into the foreground
    claim_foreground(0);
    set_signal_handler(SIGTTOU, SIG_IGN);

    char buf[BUFSIZ];
    int bufpos = 0;
    bool needprompt = true;

    while (!feof(command_file)) {
        // Print the prompt at the beginning of the line
        if (needprompt && !quiet) {
            printf("sh61[%d]$ ", getpid());
            fflush(stdout);
            needprompt = false;
        }

        // Read a string, checking for error or EOF
        if (fgets(&buf[bufpos], BUFSIZ - bufpos, command_file) == nullptr) {
            if (ferror(command_file) && errno == EINTR) {
                // ignore EINTR errors
                clearerr(command_file);
                buf[bufpos] = 0;
            } else {
                if (ferror(command_file)) {
                    perror("sh61");
                }
                break;
            }
        }

        // If a complete command line has been provided, run it
        bufpos = strlen(buf);
        if (bufpos == BUFSIZ - 1 || (bufpos > 0 && buf[bufpos - 1] == '\n')) {
            if (command* c = parse_line(buf)) {
                run_list(c);
                delete c;
            }
            bufpos = 0;
            needprompt = 1;
        }

        // Handle zombie processes and/or interrupt requests
        
        int i;
        while (waitpid(-1, &i, 1) > 0){
            
        }
    }

    return 0;
}
