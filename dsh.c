#include "dsh.h"
#include <string.h>
#include <errno.h>
void seize_tty(pid_t callingprocess_pgid); /* Grab control of the terminal for the calling process pgid.  */
void continue_job(job_t *j); /* resume a stopped job */
void spawn_job(job_t *j, bool fg); /* spawn a new job */

bool free_job(job_t*);

void free_the_program();

void stable_delete_job(job_t* j);
job_t* first_j = NULL;
job_t* last_j = NULL;

process_t* find_process(pid_t pid) {
  job_t* j;
  process_t* p;
  for (j = first_j; j; j = j->next) {
    for (p = j->first_process; p; p = p->next) {
      if (p->pid == pid) return p;
    }
  }
  perror("Cannot find process");
  exit(EXIT_FAILURE);
}

job_t* find_job(pid_t pgid) {
  job_t* j;
  for (j = first_j; j; j = j->next) {
    if (j->pgid == pgid) return j;
  }
  perror("Cannot find job");
  exit(EXIT_FAILURE);
}

job_t* find_stopped_job() {
  job_t* j;
  for (j = first_j; j; j = j->next) {
    if (job_is_stopped(j)) return j;
  }
  return NULL;
}

void wait_job(job_t* j) {
  pid_t t;
  process_t* child_p;
  process_t* p;
  int status;
  while ((t = waitpid(-j->pgid, &status, WUNTRACED)) > 0) {
    //printf("hanged by %d, status = %d\n", t, status);
    for (p = j->first_process; p; p = p->next) if (p->pid == t) child_p = p;
    if (WIFSTOPPED(status)) {
      printf("child stopped\n");
      printf("[%d]+ Stopped    %s\n", j->pgid, j->commandinfo);
      child_p -> status = status;
      child_p -> stopped = true;
      child_p -> completed = false;
      break;
    }

    child_p->status = status;
    child_p->completed = true;
    child_p->stopped = false;
  }
}

/* Sets the process group id for a given job and process */
int set_child_pgid(job_t *j, process_t *p)
{
    if (j->pgid < 0) /* first child: use its pid for job pgid */
        j->pgid = p->pid;
    return(setpgid(p->pid,j->pgid));
}

/* Creates the context for a new child by setting the pid, pgid and tcsetpgrp */
void new_child(job_t *j, process_t *p, bool fg)
{
         /* establish a new process group, and put the child in
          * foreground if requested
          */

         /* Put the process into the process group and give the process
          * group the terminal, if appropriate.  This has to be done both by
          * the dsh and in the individual child processes because of
          * potential race conditions.
          * */

         p->pid = getpid();

         /* also establish child process group in child to avoid race (if parent has not done it yet). */
         set_child_pgid(j, p);

         if(fg) // if fg is set
		seize_tty(j->pgid); // assign the terminal

         /* Set the handling for job control signals back to the default. */
         signal(SIGTTOU, SIG_DFL);
}

/* Spawning a process with job control. fg is true if the
 * newly-created process is to be placed in the foreground.
 * (This implicitly puts the calling process in the background,
 * so watch out for tty I/O after doing this.) pgid is -1 to
 * create a new job, in which case the returned pid is also the
 * pgid of the new job.  Else pgid specifies an existing job's
 * pgid: this feature is used to start the second or
 * subsequent processes in a pipeline.
 * */

void spawn_job(job_t *j, bool fg)
{

	pid_t pid;
	process_t *p;
  // printf("%d\n", j->pgid);

  int input = STDIN_FILENO;
	for(p = j->first_process; p; p = p->next) {

	  /* YOUR CODE HERE? */
	  /* Builtin commands are already taken care earlier */
    int fd[2];
    pipe(fd);
    // printf("after pipe %d,%d\n", fd[0], fd[1]);
	  switch (pid = fork()) {

      case -1: /* fork failure */
        perror("fork");
        exit(EXIT_FAILURE);

      case 0: /* child process  */
        p->pid = getpid();
        // if(p == j->first_process || p->next == NULL){
        //   new_child(j, p, fg);
        // }else{
        //   new_child(j, p, false);
        // }
        new_child(j, p, fg);

        if(input != STDIN_FILENO){
          dup2(input, STDIN_FILENO);
          close(input);
        }

        if(p->next != NULL){
          // printf("%d, %d\n", fd[0], fd[1]);
          close(fd[0]);
          dup2(fd[1], STDOUT_FILENO);
          close(fd[1]);
        }

    /* YOUR CODE HERE?  Child-side code for new process. */

        if(j->mystdin == INPUT_FD && p->ifile != NULL){
          int in;
          if ((in = open(p->ifile, O_RDONLY, 0)) < 0) {
            perror("Couldn't open input file");
            exit(EXIT_FAILURE);
          }
          dup2(in, STDIN_FILENO);
          close(in);
        }


        if(j->mystdout == OUTPUT_FD && p->ofile != NULL && p->next == NULL){
          int out;
          if ((out = open(p->ofile, O_WRONLY | O_APPEND | O_CREAT, 0644)) < 0){
            perror("Couldn't open the output file");
            exit(EXIT_FAILURE);
          }
          dup2(out, STDOUT_FILENO);
          close(out);
        }
        execvp(*p->argv, p->argv);

        perror("New child should have done an exec");
        exit(EXIT_FAILURE);  /* NOT REACHED */
        break;    /* NOT REACHED */

      default: /* parent */
        /* establish child process group */
        p->pid = pid;
        set_child_pgid(j, p);
        if(p != j->first_process){
          close(input);
        }
        close(fd[1]);
        if(p->next != NULL){
          input = fd[0];
          // printf("In parent, %d, %d\n", fd[0], fd[1]);
        }else{
          close(fd[0]);
        }
        // printf("%d\n", j->pgid);
        /* YOUR CODE HERE?  Parent-side code for new process.  */
        if (p == j->first_process) {
          fprintf(stdout, "%d(Lanuched): %s\n", j->pgid, j->commandinfo);
        }
    }
  }
  if (fg) {
    wait_job(j);
  }

            /* YOUR CODE HERE?  Parent-side code for new job.*/
  seize_tty(getpid()); // assign the terminal back to dsh
}

/* Sends SIGCONT signal to wake up the blocked job */
void continue_job(job_t *j)
{
     if(kill(-j->pgid, SIGCONT) < 0)
          perror("kill(SIGCONT)");
}

void brief_print_job(job_t* first_job) {
  job_t* j = first_job;
  char* running_status[3] = {"completed", "stopped", "running"};
  while (j) {
    bool completed = job_is_completed(j);
    bool stopped = job_is_stopped(j);
    fprintf(stdout, "%d(%s) ", j->pgid, running_status[!stopped + !completed]);
    fprintf(stdout, "%s\n", j->commandinfo);
    j = j->next;
  }
}

void delete_completed_job() {
  job_t* j;
  job_t* j_next;
  for(j = first_j; j;) {
    j_next = j->next;
    if(job_is_completed(j)) {
      stable_delete_job(j);
    }
    j = j_next;
  }
}

void list_jobs() {
  brief_print_job(first_j);
  delete_completed_job();
}


/*
 * builtin_cmd - If the user has typed a built-in command then execute
 * it immediately.
 */
bool builtin_cmd(job_t *last_job, int argc, char **argv)
{

	    /* check whether the cmd is a built in command
        */

        job_t* j = last_job;
        if (!strcmp(argv[0], "quit")) {
            /* Your code here */
            free_the_program();
            exit(EXIT_SUCCESS);
	      }
        else if (!strcmp("jobs", argv[0])) {
            /* Your code here */
            stable_delete_job(j);
            list_jobs();
            return true;
        }
        else if (!strcmp("cd", argv[0])) {
            /* Your code here */

            // delete_job(j, first_j);
            j->pgid = getpid();
            int res;
            res = chdir(argv[1]);
            if(res){
              perror("Error deteced when changing directory");
            }
            stable_delete_job(j);
            return true;
        }
        else if (!strcmp("bg", argv[0])) {
            /* Your code here */
            pid_t pgid = -1;
            if (argc == 2) {
              pgid = (pid_t)atoi(argv[1]);
            } else if (argc == 1) {
              job_t* j_stopped = find_stopped_job();
              if (j_stopped) pgid = j_stopped->pgid;
            } else {
              perror("too many arguments for bg");
              exit(EXIT_FAILURE);
            }
            if (kill( -pgid, SIGCONT) < 0)
              perror("kill (SIGCONT)");
            process_t* p;
            for (p = find_job(pgid)->first_process; p; p = p->next) {
              p->stopped = false;
            }
            stable_delete_job(j);
            return true;
        }
        else if (!strcmp("fg", argv[0])) {
            /* Your code here */
            pid_t pgid = -1;
            if (argc == 2) {
              pgid = (pid_t)atoi(argv[1]);
            } else if (argc == 1) {
              job_t* j_stopped = find_stopped_job();
              if (j_stopped) pgid = j_stopped->pgid;
            } else {
              perror("too many arguments for fg");
              exit(EXIT_FAILURE);
            }
            if (pgid == -1) return true;
            //printf("%d\n", pgid);
            seize_tty(pgid);
            if (kill( -pgid, SIGCONT) < 0)
              perror("kill (SIGCONT)");
            process_t* p;
            for (p = find_job(pgid)->first_process; p; p = p->next) {
              p->stopped = false;
            }
            wait_job(find_job(pgid));
            seize_tty(getpid());
            stable_delete_job(j);
            return true;
        }
        return false;       /* not a builtin command */
}

void stable_delete_job(job_t* j) {
  if (j == first_j) {
    job_t* j_next = j->next;
    delete_job(j, first_j);
    first_j = j_next;
  } else {
    delete_job(j, first_j);
  }
}

/* Build prompt messaage */
char* promptmsg()
{
    /* Modify this to include pid */
	static char buffer[20];
	if (isatty(STDIN_FILENO)) sprintf(buffer, "dsh-%d$ ", getpid());
	return buffer;
}

void run_job(job_t* j)
{
    // Suppose only one process
    process_t* p = j->first_process;
    if (! builtin_cmd(j, p->argc, p->argv)) {
        if (j->bg) {
            spawn_job(j, false);
        } else {
            spawn_job(j, true);
        }
    }
}

void append_jobs(job_t* j) {
  if (first_j == NULL) {
    first_j = j;
  } else {
    last_j = find_last_job(first_j);
    last_j->next = j;
  }
}

void signal_tstp(int p) {
  printf("tstp\n");
  //kill(getpid(), SIGKILL);
}

void signal_int(int p) {
  printf("kill\n");
  kill(getpid(), SIGKILL);
}

void signal_chld(int signum) {
  //signal(SIGTTOU, SIG_IGN);
  //signal(SIGTTIN, SIG_IGN);
  //seize_tty(getpid());
  //printf("signum: %d\n", signum);
  //printf("current pid:%d\n", getpid());
  //printf("catched stopped\n");
  //printf("current terminal foreground process group: %d\n", tcgetpgrp(STDIN_FILENO));
  //kill(getpid(), SIGCONT);

  int status;
  int pid;
  process_t* p;
  while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
    // printf("pid=%d\n", pid);
    p = find_process(pid);
    if (WIFSTOPPED(status)) p->stopped = true;
    if (WIFEXITED(status)) p->completed = true;
    if (WIFCONTINUED(status)) p->stopped = false;
  }
}

void free_the_program() {
  job_t* j;
  process_t* p;
  job_t* j_next;
  for( j = first_j; j != NULL; ) {
    j_next = j->next;
    if (job_is_completed(j)) free_job(j);
    else {
      for (p = j->first_process; p != NULL && p->pid != -1; p = p->next) {
      	printf("pid: %d\n", p->pid); kill(p->pid, SIGKILL);
      }
      free_job(j);
    }
    j = j_next;
  }
}

int main(int argc, char* argv[])
{
  //signal(SIGTSTP, &signal_tstp);
  signal(SIGCHLD, &signal_chld);
  //signal(SIGINT, &signal_int);

	init_dsh();
	DEBUG("Successfully initialized\n");

  // printf("%d\n", stdin->_fileno);

	while(1) {
    job_t *j = NULL;
		if(!(j = readcmdline(promptmsg()))) {
			if (feof(stdin)) { /* End of file (ctrl-d) */
				fflush(stdout);
				printf("\n");
        free_job(j);
        free_the_program();
				exit(EXIT_SUCCESS);
      }
			continue; /* NOOP; user entered return or spaces with return */
		}

        /* Only for debugging purposes to show parser output; turn off in the
         * final code */
        //if(PRINT_INFO) print_job(j);

        /* Your code goes here */
        /* You need to loop through jobs list since a command line can contain ;*/
        /* Check for built-in commands */
        /* If not built-in */
            /* If job j runs in foreground */
            /* spawn_job(j,true) */
            /* else */
            /* spawn_job(j,false) */
        append_jobs(j);
        job_t* j_next;
        for (job_t* ji = j; ji != NULL; ) {
            // printf("%ld\n", (long)ji->pgid);
            j_next = ji->next;
            run_job(ji);
            ji = j_next;
            // printf("%ld\n", (long)ji->pgid);
            //if(PRINT_INFO && ji != NULL) print_job(ji);
        }
    }
}
