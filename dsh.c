#include "dsh.h"
#include <string.h>
void seize_tty(pid_t callingprocess_pgid); /* Grab control of the terminal for the calling process pgid.  */
void continue_job(job_t *j); /* resume a stopped job */
void spawn_job(job_t *j, bool fg); /* spawn a new job */

job_t* first_j = NULL;
job_t* last_j = NULL;


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
	for(p = j->first_process; p; p = p->next) {

	  /* YOUR CODE HERE? */
	  /* Builtin commands are already taken care earlier */
	  switch (pid = fork()) {

      case -1: /* fork failure */
        perror("fork");
        exit(EXIT_FAILURE);

      case 0: /* child process  */
        p->pid = getpid();
        new_child(j, p, fg);
    /* YOUR CODE HERE?  Child-side code for new process. */
        if(j->mystdin == INPUT_FD){
          int in;
          if ((in = open(p->ifile, O_RDONLY, 0)) < 0) {
            perror("Couldn't open input file");
            exit(EXIT_FAILURE);
          }
          dup2(in, STDIN_FILENO);
          close(in);
        }else if(j->mystdout == OUTPUT_FD){
          int out;
          if ((out = creat(p->ofile, 0644)) < 0){
            perror("Couldn't open the output file");
            exit(EXIT_FAILURE);
          }
          creat(p->ofile, 0644);
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
        // printf("%d\n", j->pgid);
        /* YOUR CODE HERE?  Parent-side code for new process.  */
        if (p == j->first_process) {
          fprintf(stdout, "%d(Lanuched): %s\n", j->pgid, j->commandinfo);
        }
    }
  }
  if (fg) {
    pid_t t;
    process_t* child_p;
    int status;
    while ((t = waitpid(-1, &status, WUNTRACED)) > 0) {
      printf("hanged by %d, status = %d\n", t, status);            
      for (p = j->first_process; p; p = p->next) if (p->pid == t) child_p = p;
      if (WIFSTOPPED(status)) {
        printf("child stopped\n");
        printf("[%d]+ Stopped    %s", j->pgid, j->commandinfo); 
        break;
      }

      child_p->status = status;
      child_p->completed = true;

    }
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
      delete_job(j, first_j);
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
            exit(EXIT_SUCCESS);
	      }
        else if (!strcmp("jobs", argv[0])) {
            /* Your code here */
            delete_job(j, first_j);
            list_jobs();
            return true;
        }
        else if (!strcmp("cd", argv[0])) {
            /* Your code here */

            // delete_job(j, first_j);
            j->pgid = getpid();
            chdir(argv[1]);
            return true;
        }
        else if (!strcmp("bg", argv[0])) {
            /* Your code here */
            delete_job(j, first_j);
            return true;
        }
        else if (!strcmp("fg", argv[0])) {
            /* Your code here */
            pid_t jid = (pid_t)atoi(argv[1]);
            seize_tty(jid);
            delete_job(j, first_j);
            return true;
        }
        return false;       /* not a builtin command */
}

/* Build prompt messaage */
char* promptmsg()
{
    /* Modify this to include pid */
	static char buffer[20];
	sprintf(buffer, "dsh-%d$ ", getpid());
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

void signal_chld(int p) {
  signal(SIGTTOU, SIG_IGN);
  signal(SIGTTIN, SIG_IGN);
  seize_tty(getpid());
  printf("current pid:%d\n", getpid());
  printf("catched stopped\n");
  printf("current terminal foreground process group: %d\n", tcgetpgrp(STDIN_FILENO));
  kill(getpid(), SIGCONT);
}

int main(int argc, char* argv[])
{
  signal(SIGTSTP, &signal_tstp);
  signal(SIGCHLD, &signal_chld);
  signal(SIGINT, &signal_int);

	init_dsh();
	DEBUG("Successfully initialized\n");

  int in;
  if (argc == 2) {
    printf("file %s\n", argv[1]);
    if ((in = open(argv[1], O_RDONLY, 0)) < 0) {
      perror("Couldn't open input file");
      exit(EXIT_FAILURE);
    }
    dup2(in, STDIN_FILENO);
  }

  printf("%d\n", stdin->_fileno);

	while(1) {
    job_t *j = NULL;
		if(!(j = readcmdline(promptmsg()))) {
			if (feof(stdin)) { /* End of file (ctrl-d) */
				fflush(stdout);
				printf("\n");
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
        for (job_t* ji = j; ji != NULL; ji = ji->next) {
            // printf("%ld\n", (long)ji->pgid);
            run_job(ji);
            // printf("%ld\n", (long)ji->pgid);
            if(PRINT_INFO && ji != NULL) print_job(ji);
        }
    }
}
