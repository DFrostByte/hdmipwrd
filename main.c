#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/input.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* cat /proc/bus/input/devices */

#define M_INPUT_ "/dev/input/mouse0"
#define K_INPUT_ "/dev/input/event0"

/* fbset and xrefresh need to be executed to update the display */

#define CMD_HDMI_ON_     "/opt/vc/bin/tvservice --preferred; " \
                         "fbset -depth 8; fbset -depth 16; xrefresh"
#define CMD_HDMI_OFF_    "/opt/vc/bin/tvservice --off"
#define CMD_HDMI_STATUS_ "/opt/vc/bin/tvservice --status"
#define STATUS_MSG_OFF_  "state 0x120002 [TV is off]"
#define TIMEOUT_ (60 * 2)
#define READ_ERR_ -1
#define OPEN_ERR_ -1
#define SLEEP_TIME_ 2
#define SLEEPS_ (TIMEOUT_ / SLEEP_TIME_)

struct dev_ {
	const char *path;
	int         fd;
};

enum state_ { HDMI_STATE_ERR = -1,
              HDMI_STATE_OFF = 0,
              HDMI_STATE_ON  = 1 };

static enum state_
_get_hdmi_state (void)
/*
 * determine the on/off state of the HDMI device by reading the output of the
 * status command and comparing it to the output given when the status is off
 */
{
	static const char   status_off_str[] = STATUS_MSG_OFF_;
	static const size_t status_off_len   = sizeof (STATUS_MSG_OFF_) - 1;

	char buf[status_off_len];
	enum state_ state = HDMI_STATE_ERR;

	FILE *pipe = popen (CMD_HDMI_STATUS_, "r");
	if (pipe)
	{
		fread (buf, sizeof (char), status_off_len, pipe);
		if (! ferror (pipe))
		{
			if (! strncmp (status_off_str, buf, status_off_len))
				state = HDMI_STATE_OFF;
			else
				state = HDMI_STATE_ON;
		}
		pclose (pipe);
	}

	return state;
}

static enum state_
_set_hdmi_state (enum state_ new_state)
/*
 * turn HDMI on/off. test current state so state change commands
 * are only executed if necessary
 */
{
	enum state_ state = _get_hdmi_state ();
	const char * cmd  = NULL;

	if (new_state != state)
	{
		switch (new_state)
		{
			case HDMI_STATE_OFF:
				cmd = CMD_HDMI_OFF_;
				break;
			case HDMI_STATE_ON:
				cmd = CMD_HDMI_ON_;
				break;
		}
		if (cmd)
		{
			if (! system (cmd))
				state = new_state;
		}
	}

	return state;
}

static int
_wake_ps_running (void)
/*
 * pass a list of pipe-separated exe names to pgrep to see
 * if any of them are running
 */
{
	static const char *pgrep_cmd = "pgrep 'omxplayer'";

	if (system (pgrep_cmd))
		return 0;
	else
		return 1;
}

static void
_daemonize (void)
{
	pid_t pid,
          sid;
	int   fd;

	/* test if already a daemon */

	if (getppid () == 1)
		return;

	pid = fork ();
	if (pid < 0)
		exit (EXIT_FAILURE);

	/* exit the parent process */

	if (pid > 0)
		exit (EXIT_SUCCESS);

	/* daemon starts here */

	sid = setsid ();
	if (sid < 0)
		exit (EXIT_FAILURE);

	chdir ("/");

	/* redirect input and output to /dev/null */

	fd = open ("/dev/null", O_RDWR, 0);
	if (fd != -1)
	{
		dup2 (fd, STDIN_FILENO);
		dup2 (fd, STDOUT_FILENO);
		dup2 (fd, STDERR_FILENO);

		if (fd > 2)
			close (fd);
	}

	umask (027);
}

int
main (void)
{
	struct dev_ devs[] = { K_INPUT_, 0,
	                       M_INPUT_, 0,
	                              0, 0 };

    struct dev_ *dev_ptr;
    struct input_event ev;

	/* open mouse and keyboard devices for reading */

	for (dev_ptr = devs; dev_ptr->path; ++dev_ptr)
	{
    	dev_ptr->fd = open (dev_ptr->path, O_RDONLY | O_NONBLOCK);
    	if (dev_ptr->fd == OPEN_ERR_)
		{
        	fprintf (stderr, "Cannot open %s: %s.\n", dev_ptr->path, strerror (errno));
        	return EXIT_FAILURE;
    	}
    }

	_daemonize ();

	int timeout = SLEEPS_;
	int wakeup = 0;
    for ( ; ; --timeout)
    {
		/* try to read events from input devices and test for 'stay awake
         * processes'. if anything is found, reset timeout and set
		 * HDMI_STATE_ON
		 */

		for (dev_ptr = devs; dev_ptr->path; ++dev_ptr)
		{
     		while (READ_ERR_ != read (dev_ptr->fd, &ev, sizeof (ev)))
				wakeup = 1;
		}

		if (wakeup || _wake_ps_running ())
		{
			_set_hdmi_state (HDMI_STATE_ON);
			timeout = SLEEPS_;
			wakeup = 0;
		}
		else
		{
			if (timeout == 0)
				_set_hdmi_state (HDMI_STATE_OFF);
		}

		sleep (SLEEP_TIME_);
	}

	return EXIT_SUCCESS;
}
