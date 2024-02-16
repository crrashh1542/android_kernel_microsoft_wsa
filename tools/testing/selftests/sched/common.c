// SPDX-License-Identifier: GPL-2.0

#include "common.h"

bool hyperthreading_enabled(void)
{
	FILE *file = fopen("/sys/devices/system/cpu/smt/active", "r");
	char smt_active[2];

	if (file == NULL) {
		ksft_print_msg("Could not determine if hyperthreading is enabled\n");
		return false;
	}

	if (fgets(smt_active, sizeof(smt_active), file)	== NULL) {
		perror("Failed to read smt_active");
		return false;
	}
	fclose(file);

	if (smt_active[0] != '1')
		return false;
	return true;
}
