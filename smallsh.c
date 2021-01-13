/*********************************************************************
** Program: Program 3 CS 344 - smallsh
** Author:Phillip Wellheuser
** Date:11/20/19
** Description: A small shell program
*********************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

//global vars for convenience
int exitStatus = 0;
int fgMode = 0;

/*********************************************************************
** Prototypes
*********************************************************************/
void shLoop();
char *readLineIn();
char **parseArgs(char *lineIn);
pid_t exeArgs(char **args);
void shCatchSIGTSTP(int signo);
void exitProc(int numChildren, pid_t children[]);

/*********************************************************************
** main(): sets up the main process for dealting with signals and starts
**		the main loop
*********************************************************************/
int main(int argc, char **argv) {
	//create a sigaction struct for background process signal catching (ignore SIGINT
	struct sigaction ignore_action = { 0 };
	ignore_action.sa_handler = SIG_IGN;
	//tell the main shell to always ignore SIGINT
	sigaction(SIGINT, &ignore_action, NULL);

	//tell main process to handle SIGTSTP signals with 
	struct sigaction shCatchSIGTSTP_action = { 0 };
	shCatchSIGTSTP_action.sa_handler = shCatchSIGTSTP;
	sigfillset(&shCatchSIGTSTP_action.sa_mask);
	shCatchSIGTSTP_action.sa_flags = 0;
	shCatchSIGTSTP_action.sa_flags = SA_RESTART;
	sigaction(SIGTSTP, &shCatchSIGTSTP_action, NULL);

	shLoop();
	printf("\n");
	return 0;
}

/*********************************************************************
** shLoop(): loops thorough user input prompting the user at intervals 
**		until "exit" is used as a command
*********************************************************************/
void shLoop() {
	char *lineIn;
	char **args;
	int numChildren = 0;
	int maxChildren = 1000;
	pid_t children[maxChildren];
	pid_t result = getpid();

	while (result != 0) {
		//check for exited children
		if (numChildren > 0) {
			for (int i = 0; i < numChildren; i++) {
				int curChildStatus;
				pid_t curChild = waitpid(children[i], &curChildStatus, WNOHANG);
				if (curChild != 0) {
					//say something about the child
					if (WIFEXITED(curChildStatus) != 0) {//if proc term'd naturally
						printf("background pid %d is done: exit value %d\n", curChild, WEXITSTATUS(curChildStatus));
					}
					if (WIFSIGNALED(curChildStatus) != 0) {//if prok term'd by signal
						printf("background pid %d is done: terminated by signal %d\n", curChild, WTERMSIG(curChildStatus));
					}

					//replace the child with the most recent child
					children[i] = children[numChildren-1];
					numChildren--;
				}
			}
		}

		printf(": ");//print the prompt
		lineIn = readLineIn();//get the commmand line
		args = parseArgs(lineIn);//pass the command line into arguments
		result = exeArgs(args);//execute all of the arguments as necessary
		//if a child process is still running, add it to a list to check 
		if (result != getpid() && result != 0) {
			//if the maximum number of allowed processes at a time is reached, print an error
			if (numChildren < maxChildren - 1) {
				children[numChildren] = result;
				numChildren++;
			}
			else {
				printf("max processes reached\n");
			}
		}

		free(lineIn);
		free(args);
	}

	//clean up orphaned zombie children before exiting
	exitProc(numChildren, children);
	exit(0);
}

/*********************************************************************
** readLineIn(): gets the command line from the user and sets the last
**		character to \0
*********************************************************************/
char *readLineIn() {
	int bufferSize = 2048; //shell must support command lines with a maximum length of 2048 characters
	char *input;
	size_t inputSize = bufferSize;
	size_t characters;
	int numChars = 0;
	int c;

	input = (char *)malloc(inputSize * sizeof(char));
	if (input == NULL) {
		perror("Unable to allocate input buffer");
		exit(1);
	}
	while (numChars <= 0) {
		numChars = getline(&input, &inputSize, stdin);
		if (numChars == -1) {
			clearerr(stdin);
		}
		else if (numChars < -1) {
			perror("failed to read any chars");
		}
	}
	//replace extra chars w/ null char
	for (int i = 0; i < bufferSize; i++) {
		c = input[i];
		if (c == EOF || c == '\n' || c == '#') {
			input[i] = '\0';
			return input;
		}
	}
}

/*********************************************************************
** parseArgs(): parses lineIn by " \t\r\n", updates any instance of $$
**		to PID and sets final argument to NULL
*********************************************************************/
char **parseArgs(char *lineIn) {
	int bufferSize = 2048;
	char **args = malloc(bufferSize * sizeof(char*));
	char *arg;
	int myPidNum = getpid();
	char myPid[512];
	sprintf(myPid, "%d", myPidNum);
	
	if (!args) {
		perror("error allocating args in parseArgs\n");
		exit(EXIT_FAILURE);
	}

	//search through each argument and replace $$ with PID
	int curArg = 0;
	arg = strtok(lineIn, " \t\r\n");
	while (arg != NULL) {
		char *newArg = malloc(512 * sizeof(char));
		int newArgChar = 0;
		int argChar = 0;
		while (arg[argChar] != '\0') {
			if (arg[argChar] == '$' && arg[argChar + 1] == '$') {
				for (int pidChar = 0; pidChar < strlen(myPid); pidChar++) {
					newArg[newArgChar] = myPid[pidChar];
					newArgChar++;
				}
				argChar++;
			}
			else {
				newArg[newArgChar] = arg[argChar];
				newArgChar++;
			}
			argChar++;
		}
		args[curArg] = newArg;
		curArg++;
		arg = strtok(NULL, " \t\r\n");
		
		if (curArg >= 512) {//maximum of 512 arguments
			arg = NULL;
			write(1, "exceeded number of args", sizeof("exceeded number of args")); fflush(stdout);
		}
	}
	args[curArg] = NULL;//make sure the final argument is NULL for use in execvp()
	return args;
}

/*********************************************************************
** exeArgs(): compares the first argument of args[] to determine what 
**		commands to execute, processes built-in commands
**		then rearranges output if necessary for
**		non-built-in programs, executes non-built-in programs and 
**		prints the results for foreground processes or returns the PID
**		for background processes or 0 in the case of exit
*********************************************************************/
pid_t exeArgs(char **args) {
	pid_t spawnPid = getpid();
	pid_t waitChild;
	int fgChildStatus = 0;

	if (args[0] == NULL) {
		return spawnPid;
	}
	//[exit]	
	else if (strcmp(args[0], "exit") == 0) {//[exit]
		//kill off all the processes
		return 0;
	}
	//[cd]
	else if (strcmp(args[0], "cd") == 0) {//[cd]
		//make this a separate func later
		if (args[1] != NULL) {
			chdir(args[1]);
		}
		else {
			chdir(getenv("HOME"));
		}
	}
	//[status]
	else if (strcmp(args[0], "status") == 0) {
		if (exitStatus == 1) {
			printf("exit value %d\n", exitStatus);
		}
		else {
			//if a signal terminated it
			if (WIFEXITED(exitStatus) != 0) {//if proc term'd naturally
				printf("exit value %d\n", WEXITSTATUS(exitStatus));
			}
			if (WIFSIGNALED(exitStatus) != 0) {//if prok term'd by signal
				printf("terminated by signal %d\n", WTERMSIG(exitStatus));
			}
		}
	}
	//[external process]
	else {
		int changeOutput = 0;
		int changeInput = 0;
		int bgProc = 0;
		int curArg = 0;

		//create a mask to keep FG functions from being interrupted by SIGTSTP
		sigset_t temp_mask;
		sigemptyset(&temp_mask);
		sigaddset(&temp_mask, SIGTSTP);

		while (args[curArg] != NULL) {//check for special parameters
			if (strcmp(args[curArg], ">") == 0) {//if there's an ouput file
				changeOutput = curArg + 1;
				curArg++;
			}
			else if (strcmp(args[curArg], "<") == 0) {//if there's an input file
				changeInput = curArg + 1;
				curArg++;
			}
			else if (strcmp(args[curArg], "&") == 0 && args[curArg + 1] == NULL) {
				bgProc = curArg;
				sigprocmask(SIG_BLOCK, &temp_mask, NULL);
			}
			curArg++;
		}

		spawnPid = fork();
		switch (spawnPid) {
		case 0://this is the child process
			if (bgProc) {
				//if bgProc has not been assigned new input
				if (!changeInput) {
					int targetFD = open("/dev/null", 0);
					if (targetFD == -1) {
						perror("could not open /dev/null for input");
						exit(1);
					}

					int redirResult = dup2(targetFD, 0);
					if (redirResult == -1) {
						perror("could not redirect bg input");
					}
				}
				//if bgProc has not been assigned new output and fgMode is not turned on
				if (!changeOutput && !fgMode) {
					int targetFD = open("/dev/null", O_WRONLY);
					if (targetFD == -1) {
						perror("could not open /dev/null for output");
						exit(1);
					}
					int redirResult = dup2(targetFD, 1);
					if (redirResult == -1) {
						perror("could not redirect bg output");
					}
				}

				//remove background arg from args[]
				curArg = bgProc;
				while (args[curArg + 1] != NULL) {
					strcpy(args[curArg], args[curArg + 1]);
					curArg++;
				}
				args[curArg] = NULL;

				//tell bgProcs to ignore SIGINT
				struct sigaction ignore_action = { 0 };
				ignore_action.sa_handler = SIG_IGN;
				//begin igonring SIGINT
				sigaction(SIGINT, &ignore_action, NULL);
			}
			else {
				//set all foreground children to ignore ^c/SIGINT
				struct sigaction normal_action = { 0 };
				normal_action.sa_handler = SIG_DFL;

				sigaction(SIGINT, &normal_action, NULL);
			}

			//redirect output
			if (changeOutput) {
				int targetFD = open(args[changeOutput], O_WRONLY | O_CREAT | O_TRUNC, 0644);
				if (targetFD == -1) {
					//perror("could not open output file");
					printf("cannot open %s for input\n", args[changeOutput]);
					exit(1);
				}
				int redirResult = dup2(targetFD, 1);
				if (redirResult == -1) {
					perror("could not redirect output");
				}
				curArg = changeOutput - 1;
				while (args[curArg + 2] != NULL) {//remove redirect arguments
					strcpy(args[curArg], args[curArg + 2]);
					curArg++;
				}
				args[curArg] = NULL;
				args[curArg + 1] = NULL;
			}
			//redirect input
			if (changeInput) {
				int targetFD = open(args[changeInput], O_RDONLY);
				if (targetFD == -1) {
					//perror("could not open input file");
					printf("cannot open %s for input\n", args[changeInput]);
					exit(1);
				}
				int redirResult = dup2(targetFD, 0);
				if (redirResult == -1) {
					perror("could not redirect input");
				}
				curArg = changeInput - 1;
				while (args[curArg + 2] != NULL) {//remove redirect arguments
					strcpy(args[curArg], args[curArg + 2]);
					curArg++;
				}
				args[curArg] = NULL;
				args[curArg + 1] = NULL;
			}

			//all children should ignore SIGTSTP
			struct sigaction childSIGTSTP_action = { 0 };
			childSIGTSTP_action.sa_handler = SIG_IGN;
			sigaction(SIGTSTP, &childSIGTSTP_action, NULL);

			//if the only argment was <, >, or & and it was removed as such, don't exec() it
			if (args[0] == NULL) {
				return spawnPid;
			}

			//exec program
			if (execvp(args[0], args) == -1) {
				printf("%s: %s", args[0], strerror(errno));
				exit(1);
			}
			exit(EXIT_FAILURE);//this should never run, because of exec
			break;

		case -1://something has gone terribly wrong
			perror("failed to fork: ");
			return -1;
			break;

		default://this is the parent process
			if (!bgProc || fgMode) {//wait for this child to return before returning control
				waitpid(spawnPid, &fgChildStatus, 0);
				exitStatus = fgChildStatus;

				if (WIFSIGNALED(fgChildStatus) != 0) {//if prok term'd by signal
					printf("terminated by signal %d\n", WTERMSIG(fgChildStatus));
				}
				spawnPid = getpid();
				sigprocmask(SIG_UNBLOCK, &temp_mask, NULL);
			}
			else {
				printf("background pid is %d\n", spawnPid);
			}
			break;
		}
	}
	return spawnPid;
}

/*********************************************************************
** shCatchSIGTSTP(): prints a message when the SIGTSTP signal is 
**		is caught
*********************************************************************/
void shCatchSIGTSTP(int signo) {
	if (fgMode == 0) {
		write(1, "\nEntering foreground-only mode (& is now ignored)\n: ", 54);
		fflush(stdout);
		fgMode = 1;
	}
	else {
		write(1, "\nExiting foreground-only mode\n: ", 35);
		fflush(stdout);
		fgMode = 0;
	}
}

/*********************************************************************
** exitProc(): exits the shell program, ending or killing all child 
**		processes as necessary
*********************************************************************/
void exitProc(int numChildren, pid_t children[]) {
	//clean up orphaned zombie children before exiting
	if (numChildren > 0) {
		//send each child a SIGHUP
		for (int i = 0; i < numChildren; i++) {
			int curChildStatus;
			kill(children[i], SIGHUP);
			pid_t curChild = waitpid(children[i], &curChildStatus, 0);

			//if waitpid returns that child did not exit kill it
			if (curChild != children[i]) {
				kill(children[i], SIGKILL);
			}
		}
	}
}
