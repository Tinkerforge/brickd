/*
 * brickd
 * Copyright (C) 2014 Matthias Bolte <matthias@tinkerforge.com>
 *
 * conf_file_test.c: Tests for the ConfFile type
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <daemonlib/conf_file.h>

int read_file(const char *filename, char **buffer, int *length) {
	bool success = false;
	FILE *fp = NULL;

	*buffer = NULL;
	fp = fopen(filename, "rb");

	if (fp == NULL) {
		printf("read_file: fopen for '%s' failed\n", filename);

		goto cleanup;
	}

	if (fseek(fp, 0, SEEK_END) < 0) {
		printf("read_file: fseek for '%s' failed\n", filename);

		goto cleanup;
	}

	*length = ftell(fp);

	if (*length < 0) {
		printf("read_file: ftell for '%s' failed\n", filename);

		goto cleanup;
	}

	if (fseek(fp, 0, SEEK_SET) < 0) {
		printf("read_file: fseek for '%s' failed\n", filename);

		goto cleanup;
	}

	*buffer = malloc(*length);

	if (*buffer == NULL) {
		printf("read_file: malloc for '%s' failed\n", filename);

		goto cleanup;
	}

	if ((int)fread(*buffer, 1, *length, fp) != *length) {
		printf("read_file: fread for '%s' failed\n", filename);

		goto cleanup;
	}

	success = true;

cleanup:
	if (!success) {
		free(*buffer);
	}

	fclose(fp);

	return success ? 0 : -1;
}

int compare_files(const char *reference_filename, const char *output_filename) {
	bool success = false;
	char *reference = NULL;
	int reference_length;
	char *output = NULL;
	int output_length;

	if (read_file(reference_filename, &reference, &reference_length) < 0) {
		goto cleanup;
	}

	if (read_file(output_filename, &output, &output_length) < 0) {
		goto cleanup;
	}

	if (reference_length != output_length) {
		printf("compare_files: '%s' and '%s' differ in length\n",
		       reference_filename, output_filename);

		goto cleanup;
	}

	if (memcmp(reference, output, reference_length) != 0) {
		printf("compare_files: '%s' and '%s' differ in content\n",
		       reference_filename, output_filename);

		goto cleanup;
	}

	success = true;

cleanup:
	free(reference);
	free(output);

	return success ? 0 : -1;
}

int test1(void) {
	ConfFile conf_file;

	if (conf_file_create(&conf_file) < 0) {
		printf("test1: conf_file_create failed\n");

		return -1;
	}

	if (conf_file_set_option_value(&conf_file, "#foobar=", "blubb") < 0) {
		printf("test1: conf_file_set_option_value failed\n");

		return -1;
	}

	if (conf_file_set_option_value(&conf_file, "\t fo#ob=ar \r", "  blubb \n dummy  ") < 0) {
		printf("test1: conf_file_set_option_value failed\n");

		return -1;
	}

	if (conf_file_write(&conf_file, "conf_file_test1_output.conf") < 0) {
		printf("test1: conf_file_write failed\n");

		return -1;
	}

	if (compare_files("conf_file_test1_reference.conf", "conf_file_test1_output.conf") < 0) {
		printf("test1: compare_files failed\n");

		return -1;
	}

	conf_file_destroy(&conf_file);

	return 0;
}

int test2(void) {
	ConfFile conf_file;
	const char *reference = "\x20 blubb \n \xF3\x01?\x02 foobar \t --!";
	const char *value;

	if (conf_file_create(&conf_file) < 0) {
		printf("test2: conf_file_create failed\n");

		return -1;
	}

	if (conf_file_read(&conf_file, "conf_file_test2_reference.conf", NULL, NULL) < 0) {
		printf("test2: conf_file_read failed\n");

		return -1;
	}

	value = conf_file_get_option_value(&conf_file, "foo#bar.blu=bb");

	if (value == NULL) {
		printf("test2: conf_file_get_option_value failed\n");

		return -1;
	}

	if (strlen(reference) != strlen(value)) {
		printf("test2: reference and value differ in length\n");

		return -1;
	}

	if (memcmp(reference, value, strlen(reference)) != 0) {
		printf("test2: reference and value differ in content\n");

		return -1;
	}

	conf_file_destroy(&conf_file);

	return 0;
}

int test3(void) {
	ConfFile conf_file;

	if (conf_file_create(&conf_file) < 0) {
		printf("test3: conf_file_create failed\n");

		return -1;
	}

	if (conf_file_read(&conf_file, "conf_file_test3_input.conf", NULL, NULL) < 0) {
		printf("test3: conf_file_read failed\n");

		return -1;
	}

	if (conf_file_write(&conf_file, "conf_file_test3_output.conf") < 0) {
		printf("test3: conf_file_write failed\n");

		return -1;
	}

	if (compare_files("conf_file_test3_reference.conf", "conf_file_test3_output.conf") < 0) {
		printf("test3: compare_files failed\n");

		return -1;
	}

	conf_file_destroy(&conf_file);

	return 0;
}

int test4(void) {
	ConfFile conf_file;

	if (conf_file_create(&conf_file) < 0) {
		printf("test4: conf_file_create failed\n");

		return -1;
	}

	if (conf_file_read(&conf_file, "conf_file_test4_input.conf", NULL, NULL) < 0) {
		printf("test4: conf_file_read failed\n");

		return -1;
	}

	if (conf_file_write(&conf_file, "conf_file_test4_output.conf") < 0) {
		printf("test4: conf_file_write failed\n");

		return -1;
	}

	if (compare_files("conf_file_test4_reference.conf", "conf_file_test4_output.conf") < 0) {
		printf("test4: compare_files failed\n");

		return -1;
	}

	conf_file_destroy(&conf_file);

	return 0;
}

int main(void) {
#ifdef _WIN32
	fixes_init();
#endif

	if (test1()) {
		return EXIT_FAILURE;
	}

	if (test2()) {
		return EXIT_FAILURE;
	}

	if (test3()) {
		return EXIT_FAILURE;
	}

	if (test4()) {
		return EXIT_FAILURE;
	}

	printf("success\n");

	return EXIT_SUCCESS;
}
