/*
MIT License

Copyright (c) 2024 Mattia Cabrini

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/inotify.h>

typedef int bool;

const int true = 1;
const int false = 0;

char *file_path = NULL; // from argv
char *comm = NULL; // to be freed

const char* init_watch(int *fd, int *wd, char *path) {
	*fd = inotify_init();
	
	if (*fd == -1) {
		return "could not init inotify watch";
	}
	
	*wd = inotify_add_watch(*fd, path, IN_MODIFY | IN_IGNORED);

	if (*wd == -1) {
		close(*fd);
		return "could not add a watch";
	}

	return NULL;
}

void close_watch(int *fd, int *wd) {
	if (*wd >= 0) {
		inotify_rm_watch(*fd, *wd);
		*wd = -1;
	}

	if(*fd >= 0) {
		close(*fd);
		*fd = -1;
	}
}

bool reg_file_exists(char *path, ino_t *st_ino) {
	struct stat s;

	int sres = stat(path, &s);

	if (sres == -1) {
		return false;
	}

	bool isreg = S_ISREG(s.st_mode);

	if (st_ino != NULL) {
		*st_ino = s.st_ino;
	}

	return isreg;
}

bool read_inotify_e(int fd, struct inotify_event *e) {
	size_t sz_base_ev_struct = sizeof(struct inotify_event);
	size_t sz_buf = sizeof(struct inotify_event) + NAME_MAX + 1;
    
	size_t bytes_read = read(fd, e, sz_base_ev_struct);

	if (bytes_read != sz_base_ev_struct) {
		return false;
	}

	if (e->len > 0) {
		bytes_read = read(fd, e->name, e->len);

		if (bytes_read != e->len) {
			return false;
		}
	}

	return true;
}

size_t strsize(char *str) {
	return (strlen(str) + 1) * sizeof(char);
}

char* load_config(int argc, char **argv) {
	int i, what, comm_flag = -1, tot_len;
	
	for (i = 1; i < argc && comm_flag == -1; ++i) {
		char *arg = argv[i];

		if (strncmp(arg, "--path", strsize("--path")) == 0 || strncmp(arg, "-p", strsize("-p")) == 0) {
			if (i == argc - 1) {
				return "could not read path: no more args";
			}
			
			file_path = argv[++i];

			continue;
		}

		if (strncmp(arg, "--comm", strsize("--comm")) == 0 || strncmp(arg, "-c", strsize("-c")) == 0) {
			if (i == argc - 1) {
				return "could not read coomand: no more args";
			}
			
			comm_flag = i;
			continue;
		}
	}
	
	if (comm_flag == -1) {
		return "no command provided";
	}

	for (i = comm_flag + 1; i < argc; ++i) {
		tot_len += strlen(argv[i]) + 1;
	}
	tot_len += 1;
	
	comm = (char *) calloc(sizeof(char) * tot_len, '\0');
	for (i = comm_flag + 1; i < argc; ++i) {
		sprintf(comm, "%s%s%s", comm, comm[0] == 0 ? "" : " ", argv[i]);
	}

	return NULL;
}

void fatal(int status) {
	if(comm != NULL) {
		free(comm);
	}

	exit(status);
}

void check_config_fatal() {
	bool ok = true;

	if (file_path == NULL) {
		fprintf(stderr, "no path provided");
		ok = false;
	}
	
	if (comm == NULL) {
		fprintf(stderr, "no command provided");
		ok = false;
	}

	if (!ok) {
		fatal(1);
	}
}

// argv[1]: file path
// argv[2]: command
int main(int argc, char **argv) {
	const char *err;
	
	int fd;
	int wd;
	ino_t st_ino;

	// if (argc < 3) {
	//	printf("Usage: run_at_change file_path comm...\n");
	//	fatal(1);
	// }
	
	err = load_config(argc, argv);
	
	if (err != NULL) {
		fprintf(stderr, "%s\n", err);
		fatal(1);
	}
	
	check_config_fatal();

	printf("monitoring %s\n", file_path);

	err = init_watch(&fd, &wd, file_path);

	if (err != NULL) {
		fprintf(stderr, "%s\n", err);
		fatal(1);
	}

	if (!reg_file_exists(file_path, &st_ino)) {
		fprintf(stderr, "the file %s does not exist\n", file_path);
		fatal(1);
	}
	
	size_t sz_buf = sizeof(struct inotify_event) + NAME_MAX + 1;
	struct inotify_event *e = (struct inotify_event *) malloc(sz_buf);

	while(read_inotify_e(fd, e)) {
		if (e->wd != wd) {
			fprintf(stderr, "weird... e->wd != wd\n");
			continue;
		}

		if (e->mask & IN_MODIFY == 0) {
			fprintf(stderr, "weird... e->mask & IN_MODIFY == 0\n");
			continue;
		} 
  	
		if(e->mask & IN_MODIFY) {
			printf("changed\n");
			system(argv[2]);
		} 

		if(e->mask & IN_IGNORED) {
			printf("~~~ deleted\n");
			ino_t old_ino = st_ino;
			
			if (reg_file_exists(file_path, &st_ino)) {
				printf("~~~ file still exists\n");
				printf("~~~ ino %d :---> %d\n", old_ino, st_ino);
				
				close_watch(&fd, &wd);
		
				err = init_watch(&fd, &wd, file_path);
				
				if (err != NULL) {
					fprintf(stderr, "%s\n", err);
					fatal(1);
				}
			}
			
			system(comm);
		}
	}
	
	close_watch(&fd, &wd);

	fatal(0);
}
