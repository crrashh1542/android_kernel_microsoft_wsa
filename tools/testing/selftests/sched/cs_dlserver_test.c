// SPDX-License-Identifier: GPL-2.0
/*
 * Use the DL server infrastructure to give CFS tasks a fixed bandwidth
 * even when RT tasks are being "core scheduled" on a core. Verify that
 * they are getting the expected bandwidth (and thus not being starved).
 *
 * Copyright (c) 2024 Google.
 * Author: Joel Fernandes <joel@joelfernandes.org>
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of version 2.1 of the GNU Lesser General Public License as
 * published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses>.
 */

#define _GNU_SOURCE

#include <stdlib.h>
#include <unistd.h>
#include <sched.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/prctl.h>
#include <fcntl.h>
#include <string.h>

#include "common.h"

enum pid_type {PIDTYPE_PID = 0, PIDTYPE_TGID, PIDTYPE_PGID};

#define RUN_TIME 12 // Running time of the test in seconds
#define CORE_ID 0 // Assuming we're pinning processes to the first core
#define DL_SERVER_DEBUGFS "/sys/kernel/debug/sched/fair_server"

void write_server_debugfs(char *file, char *type, unsigned long value)
{
	char path[1024], buf[1024];
	int fd, n;

	snprintf(path, sizeof(path), "%s/%s/%s", DL_SERVER_DEBUGFS, file, type);
	fd = open(path,	O_WRONLY);
	if (fd == -1) {
		perror("Failed to open file for writing");
		return;
	}
	n = snprintf(buf, sizeof(buf), "%lu\n", value);
	n = write(fd, buf, n);
	if (n == -1)
		perror("Failed to write file");

	close(fd);
}

void write_dl_server_params(void)
{
	DIR *dir;
	struct dirent *entry;

	if (access(DL_SERVER_DEBUGFS, F_OK) == -1) {
		perror("DL server debugfs not found, cannot set DL parameters.");
		exit(EXIT_FAILURE);
	}

	dir = opendir(DL_SERVER_DEBUGFS);
	if (dir	== NULL) {
		perror("Failed to open directory");
		exit(EXIT_FAILURE);
	}

	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;

		write_server_debugfs(entry->d_name, "period", 100000000);
		write_server_debugfs(entry->d_name, "runtime", 50000000);
	}
	closedir(dir);
}

void process_func(void)
{
	unsigned long long count = 0;
	time_t end;

	// Busy loop for RUN_TIME seconds
	end = time(NULL) + RUN_TIME;
	while (time(NULL) < end) {
		count++; // Just a dummy operation
	}
}

void set_affinity(int cpu_id)
{
	cpu_set_t cpuset;

	CPU_ZERO(&cpuset);
	CPU_SET(cpu_id, &cpuset);
	CPU_SET(cpu_id + 1, &cpuset);

	if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) != 0) {
		perror("sched_setaffinity");
		exit(EXIT_FAILURE);
	}
}

void set_sched(int policy, int priority)
{
	struct sched_param param;

	param.sched_priority = priority;
	if (sched_setscheduler(0, policy, &param) != 0) {
		perror("sched_setscheduler");
		exit(EXIT_FAILURE);
	}
}

float get_process_runtime(int pid)
{
	char path[256];
	FILE *file;
	long utime, stime;
	int fields;

	snprintf(path, sizeof(path), "/proc/%d/stat", pid);
	file = fopen(path, "r");
	if (file == NULL) {
		perror("Failed to open stat file");
		return -1; // Indicate failure
	}

	// Skip the first 13 fields and read the 14th and 15th
	fields = fscanf(file,
					"%*d %*s %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu",
					&utime, &stime);
	fclose(file);

	if (fields != 2) {
		fprintf(stderr, "Failed to read stat file\n");
		return -1; // Indicate failure
	}

	// Calculate the total time spent in the process
	long total_time = utime + stime;
	long ticks_per_second = sysconf(_SC_CLK_TCK);
	float runtime_seconds = total_time * 1.0 / ticks_per_second;

	return runtime_seconds;
}

int main(void)
{
	float runtime1, runtime2, runtime3;
	int pid1, pid2, pid3;

	if (!hyperthreading_enabled())
		ksft_test_result_skip("This test requires hyperthreading to be enabled\n");

	write_dl_server_params();

	ksft_print_header();
	ksft_set_plan(1);

	// Create and set up a CFS task
	pid1 = fork();
	if (pid1 == 0) {
		set_affinity(CORE_ID);
		process_func();
		exit(0);
	} else if (pid1 < 0) {
		perror("fork for p1");
		ksft_exit_fail();
	}

	// Create a new unique cookie for the CFS task
	if (prctl(PR_SCHED_CORE, PR_SCHED_CORE_CREATE, pid1, PIDTYPE_TGID, 0) < 0) {
		perror("prctl for pid1");
		ksft_exit_fail();
	}

	// Create a new unique cookie for the current process. Future
	// forks will inherit this cookie.
	if (prctl(PR_SCHED_CORE, PR_SCHED_CORE_CREATE, 0, PIDTYPE_TGID, 0) < 0) {
		perror("prctl for current process");
		ksft_exit_fail();
	}

	// Create an RT task which inherits the parent's cookie
	pid2 = fork();
	if (pid2 == 0) {
		set_affinity(CORE_ID);
		set_sched(SCHED_FIFO, 50);
		process_func();
		exit(0);
	} else if (pid2 < 0) {
		perror("fork for p2");
		ksft_exit_fail();
	}

	// Create another RT task which inherits the parent's cookie
	pid3 = fork();
	if (pid3 == 0) {
		set_affinity(CORE_ID);
		set_sched(SCHED_FIFO, 50);
		process_func();
		exit(0);
	} else if (pid3 < 0) {
		perror("fork for p3");
		ksft_exit_fail();
	}

	sleep(RUN_TIME * 3 / 4);
	runtime1 = get_process_runtime(pid1);
	if (runtime1 != -1)
		ksft_print_msg("Runtime of PID %d is %f seconds\n", pid1, runtime1);
	else
		ksft_exit_fail_msg("Error getting runtime for PID %d\n", pid1);

	runtime2 = get_process_runtime(pid2);
	if (runtime2 != -1)
		ksft_print_msg("Runtime of PID %d is %f seconds\n", pid2, runtime2);
	else
		ksft_exit_fail_msg("Error getting runtime for PID %d\n", pid2);

	runtime3 = get_process_runtime(pid3);
	if (runtime3 != -1)
		ksft_print_msg("Runtime of PID %d is %f seconds\n", pid3, runtime3);
	else
		ksft_exit_fail_msg("Error getting runtime for PID %d\n", pid3);

	// Make sure runtime1 is within 30% of runtime2
	if (runtime1 < 0.7 * runtime2 || runtime1 > 1.3	* runtime2)
		ksft_exit_fail_msg("Runtime of PID %d is not within 30%% of runtime of PID %d\n",
						   pid1, pid2);

	// Make	sure runtime1 is within 30% of runtime3
	if (runtime1 < 0.7 * runtime3 || runtime1 > 1.3 * runtime3)
		ksft_exit_fail_msg("Runtime of PID %d is not within 30%% of runtime of PID %d\n",
						   pid1, pid3);

	waitpid(pid1, NULL, 0);
	waitpid(pid2, NULL, 0);
	waitpid(pid3, NULL, 0);

	ksft_test_result_pass("PASS\n");
	return 0;
}
