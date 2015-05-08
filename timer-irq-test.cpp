//============================================================================
// Name        : gpio-irq-test.cpp
// Author      : Black Swift team
// Version     :
// Copyright   : (c) Black Swift team <info@black-swift.com>
// Description : Test application for the gpio-irq-handler module
//============================================================================

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

#include "../../include/_clock.h"

//#define SIG_TIMER_IRQ	(SIGRTMIN+11)	// SIGRTMIN is different in Kernel and User modes
#define SIG_TIMER_IRQ	43				// So we have to hardcode this value

////////////////////////////////////////////////////////////////////////////////

static unsigned int _counter=0;

void irq_handler(int n, siginfo_t *info, void *unused)
{
	++_counter;
//	printf("Received value 0x%X\n", info->si_int);
}

////////////////////////////////////////////////////////////////////////////////

bool init_handler(int timer, int tick, unsigned int timeout)
{
	int fd;
	char buf[100];

	struct sigaction sig;
	sig.sa_sigaction=irq_handler;
	sig.sa_flags = SA_SIGINFO | SA_NODEFER;
	sigaction(SIG_TIMER_IRQ, &sig, NULL);

	fd=open("/sys/kernel/debug/timer-irq", O_WRONLY);
	if(fd < 0)
	{
		perror("open");
		return false;
	}

	sprintf(buf, "+ %d %u\n+ %d %u %u", timer, tick, timer, timeout, getpid());

	if(write(fd, buf, strlen(buf) + 1) < 0)
	{
		perror("write");
		return false;
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////////

bool remove_handler(int timer)
{
	int fd;
	char buf[100];

	fd=open("/sys/kernel/debug/timer-irq", O_WRONLY);
	if(fd < 0)
	{
		perror("open");
		return false;
	}

	sprintf(buf, "- %d", timer);

	if(write(fd, buf, strlen(buf) + 1) < 0)
	{
		perror("write");
		return false;
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////////

int main(int argc, char* argv[])
{
	if(argc > 2)
	 {
		int timer=atoi(argv[1]);
		int timeout=atoi(argv[2]);

		int wait=0;
		if(argc > 3)
		 {
			wait=atoi(argv[3]);
		 }

		_counter=0;

		struct timespec before;
		struct timespec after;

		printf("Counting timer %d signals (press Ctrl+C to stop)...\n",timer);

		if(!init_handler(timer, 20, timeout))
		{
			printf("Can't handle timer %d!\n",timer);
			return -1;
		}

		sigset_t sigset;
	    siginfo_t siginfo;

	    sigemptyset(&sigset);
	    sigaddset(&sigset, SIGINT);	//	Ctrl+C
//		sigaddset(&sigset, SIGQUIT);
//		sigaddset(&sigset, SIGSTOP);
//	    sigaddset(&sigset, SIGTERM);

	    sigprocmask(SIG_BLOCK, &sigset, NULL);

		unsigned int was=_counter;
		unsigned int total=0;
		unsigned long usecs=0;

		clock_gettime(CLOCK_REALTIME,&before);

		while(1)
		{
			sigwaitinfo(&sigset, &siginfo);

//printf("Signal: %d\n", siginfo.si_signo);

			total=_counter-was;

			clock_gettime(CLOCK_REALTIME,&after);

			usecs=diff_usecs(before,after);

			if(wait && (usecs >= wait*1000000))
			{
				break;
			}

			if(siginfo.si_signo == SIGINT)
			{
				break;
			}
		}

		remove_handler(timer);

		printf("Received %u signals in %d us (%d s %d ms %d us).\n",total,usecs,usecs/1000000L,(usecs%1000000L)/1000L,usecs%1000L);
		printf("Interval is about %u us.\n",usecs/total);
		return 0;
	}

	printf("Usage:\ntimer-irq-test <timer 0...3> <timeout in us> [seconds to wait]\n");
	return -1;
}

////////////////////////////////////////////////////////////////////////////////
