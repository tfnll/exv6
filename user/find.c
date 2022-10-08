#include "kernel/types.h"
#include "kernel/fs.h"
#include "kernel/stat.h"

#include "user/user.h"

#define PATH_MAX_LEN 1024

void dir_find(char *, char *);
void file_check(struct dirent, char *, char *);

int
main(int argc, char *argv[])
{
	if (argc != 3) {
		printf("USAGE: %s STARTING_DIRECTORY FILE_NAME\n", argv[0]);
	}

	dir_find(argv[1], argv[2]);

	exit(0);
}

void
dir_find(char *dirname, char *filename)
{
	int ret, fd;
	struct stat st;
	struct dirent de;

	fd = open(dirname, 0);
	if (fd < 0) {
		printf("ERROR: Unable to open file %s\n", dirname);
		return;
	}

	ret = fstat(fd, &st);
	if (ret < 0) {
		printf("ERROR: Unable to stat %s\n", dirname);
		close(fd);
		return;
	}

	if (st.type != T_DIR) {
		close(fd);
		return;
	}


	while (read(fd, &de, sizeof(de)) == sizeof(de))
		file_check(de, filename, dirname);

	close(fd);
}

void
file_check(struct dirent de, char *filename, char *path)
{
	int ret;
	char file_path[PATH_MAX_LEN];

	if (de.inum == 0)
		return;

	ret = strcmp(de.name, ".");
	if (ret == 0)
		return;

	ret = strcmp(de.name, "..");
	if (ret == 0)
		return;

	ret = strcmp(de.name, filename);

	strcpy(file_path, path);
	strcat(file_path, "/");
	strcat(file_path, de.name);

	if (ret == 0)
		printf("%s\n", file_path);

	dir_find(file_path, filename);
}
