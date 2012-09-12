/* @@@LICENSE
*
*      Copyright (c) 2008-2012 Hewlett-Packard Development Company, L.P.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
* LICENSE@@@ */


/**
 *********************************************************************
 * @file PmKLogDaemonUtil.c
 *
 * @brief This file contains generic utility functions.
 *
 *********************************************************************
 */


#include "PmKLogDaemon.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>


/**
 * @brief mystrcpy
 *
 * Easy to use wrapper for strcpy to make it safe against buffer
 * overflows and to report any truncations.
 */
void mystrcpy(char* dst, size_t dstSize, const char* src)
{
	size_t	srcLen;

	if (dst == NULL)
	{
		ErrPrint("mystrcpy null dst\n");
		return;
	}

	if (dstSize < 1)
	{
		ErrPrint("mystrcpy invalid dst size\n");
		return;
	}

	dst[ 0 ] = 0;

	if (src == NULL)
	{
		ErrPrint("mystrcpy null src\n");
		return;
	}

	srcLen = strlen(src);
	if (srcLen >= dstSize)
	{
		ErrPrint("mystrcpy buffer overflow on '%s'\n", src);
		srcLen = dstSize - 1;
	}

	memcpy(dst, src, srcLen);
	dst[ srcLen ] = 0;
}


/**
 * @brief mystrcat
 *
 * Easy to use wrapper for strcat to make it safe against buffer
 * overflows and to report any truncations.
 */
void mystrcat(char* dst, size_t dstSize, const char* src)
{
	size_t	dstLen;
	size_t	srcLen;
	size_t	maxLen;

	if (dst == NULL)
	{
		ErrPrint("mystrcat null dst\n");
		return;
	}

	if (dstSize < 1)
	{
		ErrPrint("mystrcat invalid dst size\n");
		return;
	}

	dstLen = strlen(dst);
	if (dstLen >= dstSize)
	{
		ErrPrint("mystrcat invalid dst len\n");
		return;
	}

	if (src == NULL)
	{
		ErrPrint("mystrcat null src\n");
		return;
	}

	srcLen = strlen(src);
	if (srcLen < 1)
	{
		/* empty string, do nothing */
		return;
	}

	maxLen = (dstSize - 1) - dstLen;

	if (srcLen > maxLen)
	{
		ErrPrint("mystrcat buffer overflow\n");
		srcLen = maxLen;
	}

	if (srcLen > 0)
	{
		memcpy(dst + dstLen, src, srcLen);
		dst[ dstLen + srcLen ] = 0;
	}
}


/**
 * @brief mysprintf
 *
 * Easy to use wrapper for sprintf to make it safe against buffer
 * overflows and to report any truncations.
 */
void mysprintf(char* dst, size_t dstSize, const char* fmt, ...)
{
	va_list 		args;
	int				n;

	if (dst == NULL)
	{
		ErrPrint("mysprintf null dst\n");
		return;
	}

	if (dstSize < 1)
	{
		ErrPrint("mysprintf invalid dst size\n");
		return;
	}

	dst[ 0 ] = 0;

	if (fmt == NULL)
	{
		ErrPrint("mysprintf null fmt\n");
		return;
	}

	va_start(args, fmt);

	n = vsnprintf(dst, dstSize, fmt, args);
	if (n < 0)
	{
		ErrPrint("mysprintf error\n");
		dst[ 0 ] = 0;
	}
	else if (((size_t) n) >= dstSize)
	{
		ErrPrint("mysprintf buffer overflow\n");
		dst[ dstSize - 1 ] = 0;
	}

	va_end(args);
}


typedef struct
{
	char	path[ PATH_MAX ];
	int		fd;
}
LockFile;


static LockFile	g_processLock;


/**
 * @brief LockProcess
 *
 * Acquire the process lock (by getting a file lock on our pid file).
 * @return true on success, false if failed.
 */
bool LockProcess(const char* component)
{
	const char* locksDirPath = "/tmp/run";

	LockFile*	lock;
	pid_t		pid;
	int			fd;
	int			result;
	char		pidStr[ 16 ];
	int			pidStrLen;
	int			err;

	lock = &g_processLock;
	pid = getpid();

	/* create the locks directory if necessary */
	(void) mkdir(locksDirPath, 0777);

	mysprintf(lock->path, sizeof(lock->path), "%s/%s.pid", locksDirPath,
		component);

	/* open or create the lock file */
	fd = open(lock->path, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	if (fd < 0)
	{
		err = errno;
		ErrPrint("Failed to open lock file (err %d, %s), exiting.\n",
			err, strerror(err));
		return false;
	}

	/* use a POSIX advisory file lock as a mutex */
	result = lockf(fd, F_TLOCK, 0);
	if (result < 0)
	{
		err = errno;
		if ((err == EDEADLK) || (err == EAGAIN))
		{
			ErrPrint("Failed to acquire lock, exiting.\n");
		}
		else
		{
			ErrPrint("Failed to acquire lock (err %d, %s), exiting.\n",
				err, strerror(err));
		}
		return false;
	}

	/* remove the old pid number data */
	result = ftruncate(fd, 0);
	if (result < 0)
	{
		err = errno;
		DbgPrint("Failed truncating lock file (err %d, %s).\n",
			err, strerror(err));
	}

	/* write the pid to the file to aid debugging */
	mysprintf(pidStr, sizeof(pidStr), "%d\n", pid);
	pidStrLen = (int) strlen(pidStr);
	result = write(fd, pidStr, pidStrLen);
	if (result < pidStrLen)
	{
		err = errno;
		DbgPrint("Failed writing lock file (err %d, %s).\n",
			err, strerror(err));
	}

	lock->fd = fd;
	return true;
}


/**
 * @brief UnlockProcess
 *
 * Release the lock on the pid file as previously acquired by
 * LockProcess.
 */
void UnlockProcess(void)
{
	LockFile*	lock;

	lock = &g_processLock;
	close(lock->fd);
	(void) unlink(lock->path);
}


/**
 * @brief ParseInt
 */
bool ParseInt(const char* valStr, int* nP)
{
	long int	n;
	char*		endptr;

	endptr = NULL;
	errno = 0;
	n = strtol(valStr, &endptr, 0);
	if ((endptr == valStr) || (*endptr != 0) || (errno != 0))
	{
		return false;
	}

	*nP = (int) n;
	return true;
}
