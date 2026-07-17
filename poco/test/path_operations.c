#include <stdio.h>
#include <string.h>

#include "path_operations.h"

static int failures;

#define CHECK(condition, message) \
	do { \
		if (!(condition)) { \
			fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, (message)); \
			++failures; \
		} \
	} while (0)

static void check_split(const char* input,
	const char* expected_device,
	const char* expected_dir,
	const char* expected_file,
	const char* expected_suffix)
{
	char device[8];
	char dir[PATH_SIZE];
	char file[PATH_SIZE];
	char suffix[PATH_SIZE];

	CHECK(poco_path_split(input,
		device, sizeof(device),
		dir, sizeof(dir),
		file, sizeof(file),
		suffix, sizeof(suffix)) == Success,
		"split should succeed");
	CHECK(strcmp(device, expected_device) == 0, "unexpected device");
	CHECK(strcmp(dir, expected_dir) == 0, "unexpected directory");
	CHECK(strcmp(file, expected_file) == 0, "unexpected file");
	CHECK(strcmp(suffix, expected_suffix) == 0, "unexpected suffix");
}

static void test_portable_split(void)
{
	check_split("/usr/local/bin/foo.txt", "", "/usr/local/bin/", "foo", ".txt");
	check_split("C:\\Animator\\scripts\\run.poc", "C:", "\\Animator\\scripts\\", "run", ".poc");
	check_split("D:/Animator/scripts/run.poc", "D:", "/Animator/scripts/", "run", ".poc");
	check_split("report.tar.gz", "", "", "report", ".tar.gz");
	check_split("/tmp/", "", "/tmp/", "", "");
}

static void test_split_errors_are_bounded(void)
{
	char device[8] = "keep";
	char dir[8] = "keep";
	char file[8] = "keep";
	char suffix[8] = "keep";
	char path[PATH_SIZE + 1];

	CHECK(poco_path_split(NULL,
		device, sizeof(device), dir, sizeof(dir), file, sizeof(file), suffix, sizeof(suffix)) == Err_null_ref,
		"NULL input must fail");
	CHECK(poco_path_split("",
		device, sizeof(device), dir, sizeof(dir), file, sizeof(file), suffix, sizeof(suffix)) == Err_null_ref,
		"empty input must fail");
	CHECK(poco_path_split("9:bad.poc",
		device, sizeof(device), dir, sizeof(dir), file, sizeof(file), suffix, sizeof(suffix)) == Err_no_device,
		"invalid drive must fail");
	CHECK(poco_path_split("/path/name.poc",
		device, sizeof(device), dir, sizeof(dir), file, 4, suffix, sizeof(suffix)) == Err_buf_too_small,
		"small file buffer must fail");
	CHECK(strcmp(device, "keep") == 0 && strcmp(dir, "keep") == 0
		&& strcmp(file, "keep") == 0 && strcmp(suffix, "keep") == 0,
		"failed split must not partially write components");

	memset(path, 'a', PATH_SIZE);
	path[PATH_SIZE] = '\0';
	CHECK(poco_path_split(path,
		device, sizeof(device), dir, sizeof(dir), file, sizeof(file), suffix, sizeof(suffix)) == Err_dir_too_long,
		"PATH_SIZE input must fail");
}

static void test_merge_capacity_contract(void)
{
	char exact[15];
	char too_small[14] = "unchanged";

	CHECK(poco_path_merge(exact, sizeof(exact), "C:", "\\tmp\\", "run", ".poc") == Success,
		"exactly-sized destination must succeed");
	CHECK(strcmp(exact, "C:\\tmp\\run.poc") == 0, "merge output mismatch");
	CHECK(poco_path_merge(too_small, sizeof(too_small), "C:", "\\tmp\\", "run", ".poc") == Err_buf_too_small,
		"small destination must fail");
	CHECK(strcmp(too_small, "unchanged") == 0, "failed merge must not write destination");
	CHECK(poco_path_merge(NULL, 0, "", "", "", "") == Err_null_ref,
		"NULL destination must fail");
}

int main(void)
{
	test_portable_split();
	test_split_errors_are_bounded();
	test_merge_capacity_contract();

	if (failures != 0) {
		fprintf(stderr, "%d path operation assertion(s) failed\n", failures);
	}
	return failures != 0;
}
