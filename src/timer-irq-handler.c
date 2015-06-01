/*
 *  Timer IRQ handler for AR9331
 *
 *    Copyright (C) 2013-2015 Gerhard Bertelsmann <info@gerhard-bertelsmann.de>
 *    Copyright (C) 2015 Dmitriy Zherebkov <dzh@black-swift.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/clk.h>

#include <asm/mach-ath79/ar71xx_regs.h>
#include <asm/mach-ath79/ath79.h>
#include <asm/mach-ath79/irq.h>

#include <asm/siginfo.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>

//#define DEBUG_OUT

#ifdef	DEBUG_OUT
#define debug(fmt,args...)	printk (KERN_INFO fmt ,##args)
#else
#define debug(fmt,args...)
#endif	/* DEBUG_OUT */

//#define SIG_TIMER_IRQ	(SIGRTMIN+11)	// SIGRTMIN is different in Kernel and User modes
#define SIG_TIMER_IRQ	43				// So we have to hardcode this value

////////////////////////////////////////////////////////////////////////////////////////////

static unsigned int _timer_frequency=200000000;
static spinlock_t	_lock;

////////////////////////////////////////////////////////////////////////////////////////////

#define ATH79_TIMER0_IRQ		ATH79_MISC_IRQ(0)
#define AR71XX_TIMER0_RELOAD	0x04

#define ATH79_TIMER1_IRQ		ATH79_MISC_IRQ(8)
#define AR71XX_TIMER1_RELOAD	0x98

#define ATH79_TIMER2_IRQ		ATH79_MISC_IRQ(9)
#define AR71XX_TIMER2_RELOAD	0xA0

#define ATH79_TIMER3_IRQ		ATH79_MISC_IRQ(10)
#define AR71XX_TIMER3_RELOAD	0xA8

struct _timer_desc_struct
{
	unsigned int	irq;
	unsigned int	reload_reg;
} _timers[4]=
{
		{	ATH79_TIMER0_IRQ, AR71XX_TIMER0_RELOAD	},
		{	ATH79_TIMER1_IRQ, AR71XX_TIMER1_RELOAD	},
		{	ATH79_TIMER2_IRQ, AR71XX_TIMER2_RELOAD	},
		{	ATH79_TIMER3_IRQ, AR71XX_TIMER3_RELOAD	}
};

////////////////////////////////////////////////////////////////////////////////////////////

void __iomem *ath79_timer_base;

#define DRV_NAME	"timer-irq-handler"
#define FILE_NAME	"timer-irq"

////////////////////////////////////////////////////////////////////////////////////////////

#define MAX_PROCESSES	10

typedef struct
{
	pid_t			pid;
	unsigned int	timeout;			//	always microseconds
	int				only_once;			//	0 means "infinite" timer

	unsigned int	next_time;			//	next time we'll send signal
	unsigned int	next_interval;		//	next_time overflow counter
} _process_handler;

typedef struct
{
	int				timer;
	int				irq;
	unsigned int	timeout;			//	always microseconds

	unsigned int	current_tick;		//	ticks counter
	unsigned int	ticks_in_timeout;	//	total ticks we should get

	unsigned int	current_time;		//	microseconds since interval started
	unsigned int	current_interval;	//	current_time overflow counter

	_process_handler	processes[MAX_PROCESSES];
} _timer_handler;

#define TOTAL_TIMERS	4
static _timer_handler	all_handlers[TOTAL_TIMERS];

static struct dentry* in_file;

////////////////////////////////////////////////////////////////////////////////////////////

static int is_space(char symbol)
{
	return (symbol == ' ') || (symbol == '\t');
}

////////////////////////////////////////////////////////////////////////////////////////////

static int is_digit(char symbol)
{
	return (symbol >= '0') && (symbol <= '9');
}

////////////////////////////////////////////////////////////////////////////////////////////

static int is_eol(char symbol)
{
	return (symbol == '\n') || (symbol == '\r');
}

////////////////////////////////////////////////////////////////////////////////////////////

static int send_timer_signal(pid_t pid,int timer,unsigned int timeout)
{
	struct siginfo info;
	struct task_struct* ts=NULL;

	/* send the signal */
	memset(&info, 0, sizeof(struct siginfo));
	info.si_signo = SIG_TIMER_IRQ;
	info.si_code = SI_QUEUE;	// this is bit of a trickery: SI_QUEUE is normally used by sigqueue from user space,
					// and kernel space should use SI_KERNEL. But if SI_KERNEL is used the real_time data
					// is not delivered to the user space signal handler function.

	info.si_int=(timer << 24) | timeout;

	rcu_read_lock();
	ts=pid_task(find_vpid(pid), PIDTYPE_PID);
	rcu_read_unlock();

	if(ts)
	{
		send_sig_info(SIG_TIMER_IRQ, &info, ts);    //send the signal
		return 0;
	}

	return -1;
}

////////////////////////////////////////////////////////////////////////////////////////////

static void send_signals(_timer_handler* handler)
{
	int i=0;

	if(++(handler->current_tick) < handler->ticks_in_timeout)
	{
		return;
	}

	handler->current_tick=0;

	handler->current_time+=handler->timeout;
	if(handler->current_time < handler->timeout)
	{
		//	overflow!
		++(handler->current_interval);
	}

	for(i=0; i < MAX_PROCESSES; ++i)
	{
		if(handler->processes[i].pid)
		{
			if(	(handler->current_time >= handler->processes[i].next_time) &&
				(handler->current_interval >= handler->processes[i].next_interval))
			{
				if(	(send_timer_signal(handler->processes[i].pid,handler->timer,handler->processes[i].timeout) == 0) &&
					(!handler->processes[i].only_once))
				{
					handler->processes[i].next_time+=handler->processes[i].timeout;

					if(handler->processes[i].next_time < handler->processes[i].timeout)
					{
						//	overflow!
						++(handler->processes[i].next_interval);
					}
				}
				else
				{
					int cur_process=i;

					for(++i; i < MAX_PROCESSES; ++i)
					{
						handler->processes[i-1]=handler->processes[i];
					}
					handler->processes[i-1].pid=0;

					i=cur_process-1;
				}
			}
		}
		else
		{
			break;
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////

static irqreturn_t timer_interrupt(int irq, void* dev_id)
{
	_timer_handler* handler=(_timer_handler*)dev_id;

	if(handler && (handler->irq == irq))
	{
//		debug("Got handler for timer %d!\n",handler->timer);
		send_signals(handler);
	}
	else
	{
//		debug("IRQ %d event - no handlers found!\n",irq);
	}

	return(IRQ_HANDLED);
}

////////////////////////////////////////////////////////////////////////////////////////////

static int add_irq(int timer,void* data)
{
	int err=0;
	int irq_number=_timers[timer].irq;

	debug("Adding IRQ %d handler\n",irq_number);

	err=request_irq(irq_number, timer_interrupt, 0, DRV_NAME, data);

	if(!err)
	{
		debug("Got IRQ %d.\n", irq_number);
		return irq_number;
	}
	else
	{
		debug("Timer IRQ handler: trouble requesting IRQ %d error %d\n",irq_number, err);
	}

    return -1;
}

////////////////////////////////////////////////////////////////////////////////////////////

static void clear_handler(_timer_handler* handler)
{
	int i=0;

	handler->timer=-1;
	handler->irq=-1;
	handler->timeout=0;

	handler->current_tick=0;
	handler->ticks_in_timeout=1;

	handler->current_time=0;
	handler->current_interval=0;

	for(i=0; i < MAX_PROCESSES; ++i)
	{
		handler->processes[i].pid=0;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////

static void stop(int timer)
{
	unsigned long flags=0;

	_timer_handler* handler=&all_handlers[timer];

	spin_lock_irqsave(&_lock,flags);

	if(handler->irq >= 0)
	{
		free_irq(handler->irq, (void*)handler);
		clear_handler(handler);

		debug("Timer stopped.\n");
	}

	spin_unlock_irqrestore(&_lock,flags);
}

////////////////////////////////////////////////////////////////////////////////////////////

static int start(int timer,unsigned int timeout)
{
	int irq=-1;
	unsigned long flags=0;

	_timer_handler* handler=&all_handlers[timer];

	stop(timer);

	spin_lock_irqsave(&_lock,flags);
	// need some time (10 ms) before first IRQ - even after "lock"?!
	__raw_writel(_timer_frequency/100, ath79_timer_base+_timers[timer].reload_reg);

	irq=add_irq(timer,handler);

	if(irq >= 0)
	{
		unsigned int real_timeout=_timer_frequency/1000000*timeout;

/*		int	scale=0;

		scale=timeout/100;
		if(scale >= 2)
		{
			real_timeout/=100;
			handler->ticks_in_timeout=scale;
		}
*/
		handler->timer=timer;
		handler->irq=irq;
		handler->timeout=timeout;

		__raw_writel(real_timeout, ath79_timer_base+_timers[timer].reload_reg);

		debug("Timer #%d started with %u us interval.\n", timer, timeout);

		spin_unlock_irqrestore(&_lock,flags);
		return 0;
	}

	spin_unlock_irqrestore(&_lock,flags);

	stop(timer);
	return -1;
}

////////////////////////////////////////////////////////////////////////////////////////////

static void remove_process(int timer,pid_t pid)
{
	unsigned long flags=0;

	spin_lock_irqsave(&_lock,flags);

	if((timer >= 0) && (timer < TOTAL_TIMERS))
	{
		_timer_handler* handler=&all_handlers[timer];

		if(handler->timer == timer)
		{
			int i=0;

			for(; i < MAX_PROCESSES; ++i)
			{
				if(handler->processes[i].pid == pid)
				{
					for(++i; i < MAX_PROCESSES; ++i)
					{
						handler->processes[i-1]=handler->processes[i];
					}
					handler->processes[i-1].pid=0;

					spin_unlock_irqrestore(&_lock,flags);
					return;
				}
			}

			debug("Handler for timer %d to PID %u is not found.\n", timer, pid);
		}
	}

	spin_unlock_irqrestore(&_lock,flags);
}

////////////////////////////////////////////////////////////////////////////////////////////

static int add_process(int timer, pid_t pid, unsigned int timeout,int only_once)
{
	unsigned long flags=0;

	spin_lock_irqsave(&_lock,flags);

	if((timer >= 0) && (timer < TOTAL_TIMERS))
	{
		_timer_handler* handler=&all_handlers[timer];
		int p=0;

		if(handler->timer == timer)	//	timer is working
		{
			while((handler->processes[p].pid > 0) && (handler->processes[p].pid != pid)) ++p;

			if(p < MAX_PROCESSES)
			{
				handler->processes[p].pid=pid;
				handler->processes[p].timeout=timeout;
				handler->processes[p].only_once=only_once;

				handler->processes[p].next_time=handler->current_time;
				handler->processes[p].next_interval=handler->current_interval;

				handler->processes[p].next_time+=timeout;
				if(handler->processes[p].next_time < timeout)
				{
					//	overflow!
					++(handler->processes[p].next_interval);
				}

				spin_unlock_irqrestore(&_lock,flags);
				return 0;
			}
			else
			{
				debug("Can't add handler: %d processes already handle timer %d.\n", p, timer);
			}
		}
	}

	spin_unlock_irqrestore(&_lock,flags);
	return -1;
}

////////////////////////////////////////////////////////////////////////////////////////////

static ssize_t run_command(struct file *file, const char __user *buf,
                                size_t count, loff_t *ppos)
{
	char buffer[512];
	char line[20];
	char* in_pos=NULL;
	char* end=NULL;
	char* out_pos=NULL;

	int add=0;
	int timer=-1;
	unsigned int pid=0;
	unsigned int timeout=0;
	int only_once=0;

	if(count > 512)
		return -EINVAL;	//	file is too big

	copy_from_user(buffer, buf, count);
	buffer[count]=0;

	debug("Command is found (%u bytes length):\n%s\n",count,buffer);

	in_pos=buffer;
	end=in_pos+count-1;

	while(in_pos < end)
	{
		add=0;
		timer=-1;
		pid=0;
		timeout=0;

		while((in_pos < end) && is_space(*in_pos)) ++in_pos;	//	skip whitespace
		if(in_pos >= end) break;

		if(*in_pos == '+')
		{
			add=1;
		}
		else if(*in_pos == '-')
		{
			add=0;
		}
		else if(*in_pos == '?')
		{
			//	just print all handlers
			int i,j;

			for(i=0; i < TOTAL_TIMERS; ++i)
			{
				if(all_handlers[i].timer != -1)
				{
					printk(KERN_INFO "Timer %d (IRQ %d): ",all_handlers[i].timer,all_handlers[i].irq);

					for(j=0; j < MAX_PROCESSES; ++j)
					{
						if(all_handlers[i].processes[j].pid != 0)
						{
							printk("%u (%u us)",all_handlers[i].processes[j].pid, all_handlers[i].processes[j].timeout);
						}
						else
						{
							break;
						}
						if(j < (MAX_PROCESSES-1))
						{
							printk(", ");
						}
					}

					printk("\n");
				}
			}

			return count;
		}
		else
		{
			printk(KERN_INFO "Wrong command '%c'.\n", *in_pos);
			break;
		}
		++in_pos;
		while((in_pos < end) && is_space(*in_pos)) ++in_pos;	//	skip whitespace

		out_pos=line;
		while((in_pos < end) && is_digit(*in_pos)) *out_pos++=*in_pos++;
		*out_pos=0;

		if(is_digit(line[0]))
		{
			sscanf(line, "%d", &timer);
		}
		else
		{
			printk(KERN_INFO "Can't read timer number.\n");
			break;
		}

		if(add)
		{
			while((in_pos < end) && is_space(*in_pos)) ++in_pos;	//	skip whitespace

			out_pos=line;
			while((in_pos < end) && is_digit(*in_pos)) *out_pos++=*in_pos++;
			*out_pos=0;

			if(is_digit(line[0]))
			{
				sscanf(line, "%u", &timeout);
			}
			else
			{
				printk(KERN_INFO "Can't read timeout.\n");
				break;
			}
		}

		while((in_pos < end) && is_space(*in_pos)) ++in_pos;	//	skip whitespace

		out_pos=line;
		while((in_pos < end) && is_digit(*in_pos)) *out_pos++=*in_pos++;
		*out_pos=0;

		if(is_digit(line[0]))
		{
			sscanf(line, "%u", &pid);
		}

		while((in_pos < end) && is_space(*in_pos)) ++in_pos;	//	skip whitespace

		out_pos=line;
		while((in_pos < end) && is_digit(*in_pos)) *out_pos++=*in_pos++;
		*out_pos=0;

		if(line[0] == '1')
		{
			only_once=1;
		}

		if(add)
		{
			if(pid)
			{
				debug("Trying to add handler for timer %d to PID %u, timeout %u.\n",timer,pid,timeout);
				add_process(timer,pid,timeout,only_once);
			}
			else
			{
				debug("Trying to start timer %d , timeout %u.\n",timer,timeout);
				start(timer,timeout);
			}
		}
		else
		{
			if(pid)
			{
				debug("Trying to remove handler for timer %d to PID %u.\n",timer,pid);
				remove_process(timer,pid);
			}
			else
			{
				debug("Trying to stop timer %d.\n",timer);
				stop(timer);
			}
		}

		while((in_pos < end) && (is_space(*in_pos) || is_eol(*in_pos))) ++in_pos;	// next line
	}

	return count;
}

////////////////////////////////////////////////////////////////////////////////////////////

static const struct file_operations irq_fops=
{
//	.read = show_handlers,
	.write = run_command,
};

////////////////////////////////////////////////////////////////////////////////////////////

struct clk	//	defined in clock.c
{
	unsigned long rate;
};

////////////////////////////////////////////////////////////////////////////////////////////

static int __init mymodule_init(void)
{
    int i=0;

	struct clk* ahb_clk=clk_get(NULL,"ahb");
	if(ahb_clk)
	{
		_timer_frequency=ahb_clk->rate;
	}

	ath79_timer_base = ioremap_nocache(AR71XX_RESET_BASE, AR71XX_RESET_SIZE);

	for(i=0; i < TOTAL_TIMERS; ++i)
	{
		clear_handler(&all_handlers[i]);
	}

	in_file=debugfs_create_file(FILE_NAME, 0666, NULL, NULL, &irq_fops);

	printk(KERN_INFO DRV_NAME " is waiting for commands in file /sys/kernel/debug/" FILE_NAME ".\n");

    return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////

static void __exit mymodule_exit(void)
{
	int i=0;

	for(; i < TOTAL_TIMERS; ++i)
	{
		clear_handler(&all_handlers[i]);
	}

	debugfs_remove(in_file);

	return;
}

////////////////////////////////////////////////////////////////////////////////////////////

module_init(mymodule_init);
module_exit(mymodule_exit);

////////////////////////////////////////////////////////////////////////////////////////////

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dmitriy Zherebkov (Black Swift team)");

////////////////////////////////////////////////////////////////////////////////////////////
