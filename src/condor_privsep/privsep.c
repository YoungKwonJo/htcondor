/***************************Copyright-DO-NOT-REMOVE-THIS-LINE**
  *
  * Condor Software Copyright Notice
  * Copyright (C) 1990-2006, Condor Team, Computer Sciences Department,
  * University of Wisconsin-Madison, WI.
  *
  * This source code is covered by the Condor Public License, which can
  * be found in the accompanying LICENSE.TXT file, or online at
  * www.condorproject.org.
  *
  * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  * AND THE UNIVERSITY OF WISCONSIN-MADISON "AS IS" AND ANY EXPRESS OR
  * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  * WARRANTIES OF MERCHANTABILITY, OF SATISFACTORY QUALITY, AND FITNESS
  * FOR A PARTICULAR PURPOSE OR USE ARE DISCLAIMED. THE COPYRIGHT
  * HOLDERS AND CONTRIBUTORS AND THE UNIVERSITY OF WISCONSIN-MADISON
  * MAKE NO MAKE NO REPRESENTATION THAT THE SOFTWARE, MODIFICATIONS,
  * ENHANCEMENTS OR DERIVATIVE WORKS THEREOF, WILL NOT INFRINGE ANY
  * PATENT, COPYRIGHT, TRADEMARK, TRADE SECRET OR OTHER PROPRIETARY
  * RIGHT.
  *
  ****************************Copyright-DO-NOT-REMOVE-THIS-LINE**/

#define _CONDOR_ALLOW_OPEN_AND_FOPEN

#include "condor_common.h"
#include <dirent.h>


/* unix values */
#define CONFIG_FILENAME "/etc/condor_privsep.cfg"
#define DIR_DELIMITER ':'
#define UID_DELIMITER ','



/* NOTES:

	function 0: NOOP

	function 1:  function_spawn_procd
		1

	function 2:  function_spawn_transferd
		2 uid gid

	function 3:  function_spawn_user_job
		3 uid gid executable [params....]

	function 4:  function_cleanup_user_dir
		4 uid gid dir_name

	function 5:  function_create_user_dir
		5 uid gid dir_name

	function 6:  function_recursive_chown
		6 uid gid dir_name

	function 7:  function_open
		7 uid gid path flags mode

	function 8:  function_rename
		8 uid gid old_path new_path

	function 9:  function_chmod
		9 uid gid path mode


however, how do we tell the difference between the magic box failing and the
user job failing?  afterall, we execv the user job.  so, we're really the same
process.  answer seems to be to user a pipe back to report errors, and close
the pipe then before execing the user job.



and, logging?


*/



// global variables to hold paths to trusted binaries
// and uids of calling entity.  they are read from a
// config file in ParseConfigFile.
char *condor_procd = NULL;
char *condor_transferd = NULL;
char *condor_user_dir = NULL;
char **condor_execute_prefix_whitelist = NULL;
int  *condor_execute_uid_whitelist = NULL;
int  condor_uid = 0;
int  condor_gid = 0;


// certain variables require a bit of extra processing.  these functions
// are here just to keep assign_global_variable cleaner

void assign_condor_execute_prefix_whitelist(char* val) {

	int num_dirs;
	char* delim_finder;
	int dir_index;
	char* dir_pointer;

	// clear old values, if they had already been set.
	if(condor_execute_prefix_whitelist) {
		dir_index = 0;
		while(condor_execute_prefix_whitelist[dir_index]) {
			free(condor_execute_prefix_whitelist[dir_index]);
			dir_index++;
		}
		free(condor_execute_prefix_whitelist);
		condor_execute_prefix_whitelist = NULL;
	}
	assert(condor_execute_prefix_whitelist == NULL);

	// if we are setting the value to NULL, we can just return here.
	if(val[0]==0) {
		return;
	}


	// make two passes.  the first counts the number of entries, so we can
	// malloc the outer array, and the second pass mallocs and dups the
	// strings.
	//
	// a NULL pointer terminates the list.

	// first, count the number of entries
	num_dirs = 1;
	delim_finder = val;
	while((delim_finder = strchr(delim_finder, DIR_DELIMITER))) {
		num_dirs++;
		delim_finder++;
	}

	// malloc an array of char* that is num_dirs + 1.  the extra 1
	// is for the null terminator of the list of strings.
	condor_execute_prefix_whitelist = (char**)malloc((num_dirs+1)*(sizeof(char*)));

	// now, rescan, split on the delimiter, and dup the strings
	dir_index = 0;
	dir_pointer = val;
	do {
		// find the delimiter and zero it
		delim_finder = strchr(dir_pointer, DIR_DELIMITER);
		if(delim_finder) {
			*delim_finder = '\0';
		}

		// dup the dir
		condor_execute_prefix_whitelist[dir_index] = strdup(dir_pointer);
		//printf("dir: %s\n", dir_pointer);
		dir_index++;

		// move the dir_pointer to the next entry (if there was one)
		if(delim_finder) {
			delim_finder++;
			dir_pointer = delim_finder;
		}
	} while (delim_finder);

	// terminate the list
	condor_execute_prefix_whitelist[num_dirs] = 0;
}


void assign_condor_execute_uid_whitelist(char* val) {

	int num_uids;
	char *delim_finder;
	int uid_index;
	char* uid_pointer;

	// clear old values, if they had already been set.
	if(condor_execute_uid_whitelist) {
		free(condor_execute_uid_whitelist);
		condor_execute_uid_whitelist = NULL;
	}
	assert(condor_execute_uid_whitelist == NULL);


	// if we are setting the value to NULL, we can just return here.
	if(val[0]==0) {
		return;
	}


	// make two passes.  the first counts the number of entries, so we can
	// malloc the outer array, and the second pass mallocs and dups the
	// strings.
	//
	// a -1 value terminates the list.

	// first, count the number of entries
	num_uids = 1;
	delim_finder = val;
	while((delim_finder = strchr(delim_finder, UID_DELIMITER))) {
		num_uids++;
		delim_finder++;
	}

	// malloc an array of char* that is num_uids + 1.  the extra 1
	// is for the null terminator of the list of strings.
	condor_execute_uid_whitelist = (int*)malloc((num_uids+1)*(sizeof(int)));

	// now split on the delimiter
	uid_index = 0;
	uid_pointer = val;
	do {
		// find the delimiter and zero it
		delim_finder = strchr(uid_pointer, UID_DELIMITER);
		if(delim_finder) {
			*delim_finder = '\0';
		}

		// convert the uid
		condor_execute_uid_whitelist[uid_index] = atoi(uid_pointer);
		//printf("uid: %i\n", atoi(uid_pointer));
		uid_index++;

		// move the dir_pointer to the next entry (if there was one)
		if(delim_finder) {
			delim_finder++;
			uid_pointer = delim_finder;
		}
	} while (delim_finder);

	// terminate the list
	condor_execute_uid_whitelist[num_uids] = -1;
}



// 1 == success
// 0 == failure
int assign_global_variable(char* var, char* val) {

	if(!var || !val) {
		return 0;
	}

	if(strcasecmp(var, "condor_procd") == 0) {
		if(condor_procd) {
			free(condor_procd);
		}
		condor_procd = strdup(val);
		return 1;
	}

	if(strcasecmp(var, "condor_transferd") == 0) {
		if(condor_transferd) {
			free(condor_transferd);
		}
		condor_transferd = strdup(val);
		return 1;
	}

	if(strcasecmp(var, "condor_user_dir") == 0) {
		if(condor_user_dir) {
			free(condor_user_dir);
		}
		condor_user_dir = strdup(val);
		return 1;
	}

	if(strcasecmp(var, "condor_execute_prefix_whitelist") == 0) {
		assign_condor_execute_prefix_whitelist(val);
		return 1;
	}

	if(strcasecmp(var, "condor_execute_uid_whitelist") == 0) {
		assign_condor_execute_uid_whitelist(val);
		return 1;
	}

	if(strcasecmp(var, "condor_uid") == 0) {
		condor_uid = atoi(val);
		return 1;
	}

	if(strcasecmp(var, "condor_gid") == 0) {
		condor_gid = atoi(val);
		return 1;
	}

	// variable not valid!
	return 0;
}


int chomp(char* buf) {
	// erase the newline
	char *newline = strchr(buf, '\n');
	if (!newline) {
		// error, no newline!
		return 0;
	}
	*newline = '\0';
	return 1; // success
}

// 1 == success
// 0 == failure
//
// this config file format is very simple and unforgiving. :|
// fortunately, it's generated automatically.
//
// # comments start with #
// PARAM_STRING=/path/to/something
// PARAM_INT=123
// # no whitespace on side of equals or at end of line.
// PARAM_STRING=/reassignment/overwrites/previous/value
//
// # blank lines allowed.

int ParseConfigFile(void) {

	char linebuf[1024];
	char *equals;

	FILE* config = fopen( CONFIG_FILENAME, "rt");
	if (config == NULL) {
		return 0;
	}

	linebuf[1023] = '\0';

	while(fgets(linebuf, 1024, config)) {
		// allow for blank lines and comments
		if((linebuf[0] == '\0') || (linebuf[0] == '#')) {
			continue;
		}

		if(!chomp(linebuf)) {
			// line must have been too long.  fail, since it's
			// better than silently continuing with a truncated
			// value.
			return 0;
		}

		// find the equals sign delimiter
		equals = strchr(linebuf, '=');
		if(!equals) {
			// no equals sign, fail
			return 0;
		}

		// zero the equals sign and move the pointer ahead to the value
		*(equals++) = '\0';

		//printf("assigning %s to %s\n", linebuf, equals);
		// linebuf is the variable name, equals is the value.
		if(!assign_global_variable(linebuf, equals)) {
			// bad variable name, fail
			return 0;
		}
	}

	// all done!
	fclose(config);

	// success
	return 1;
}



FILE *logfile = NULL;
int logprint(char* fmt, ...) {
	if(logfile == NULL) {
		logfile = fopen("/tmp/magic.log", "w+");
	}
	fprintf(logfile, fmt);
	fflush(logfile);
	return 0;
}


// this is a helper function.  the return codes are not exit codes.
// this function returns zero on success, and non-zero upon failure.
// 
// needs root!
//
// does NOT chown the directory you pass in.
//
int helper_function_chown_dir(char *current_path, int from_uid, int to_uid) {

	DIR *dir;
	struct dirent *dp;
	struct stat st;
	int errcode;

	int result = 0;

	//printf ("current_path: %s\n", current_path);
	if ((dir = opendir(current_path)) == NULL) {
		// cannot open.
		return errno;
	}

	// the directory was succesfully opened, so iterate through all the entries
	// and deal with them appropriately
	while ((dp = readdir(dir)) != NULL) {
		// if this is a directory, we should recurse into it unless the
		// directory name is . or ..
		// 
		// otherwise, chown it.  don't chown soft links!

		// in both cases, we need to construct a buffer with the full pathname.
		// this is the two string lengths plus 2: a slash between them and a
		// NULL terminator.
		int full_path_len = strlen(current_path) + strlen(dp->d_name) + 2;

		// keep this on the stack, so it gets cleaned up nicely.
		//
		// JIM: would it be better to use the heap and malloc/free?
		//
		char full_path[full_path_len];
		strcpy(full_path, current_path);
		strcat(full_path, "/");
		strcat(full_path, dp->d_name);

		// we must lstat the file to see if it's a directory
		result = lstat(full_path, &st);
		if (result) {
			return errno;
		}

		if (S_ISDIR(st.st_mode)) {
			// directory.  check for . and ..
			if (strcmp(dp->d_name, ".") && strcmp(dp->d_name, "..")) {
				// this is a directory.  we need to recurse into it.
				result = helper_function_chown_dir(full_path, from_uid, to_uid);
				if (result) {
					// something failed, bail (and pass code through)
					return result;
				}
			}
		} else {
			// lchown the entry.
			// (if we don't have lchown, use chown - but only if the
			//  path doesn't refer to a symlink)

			//printf("lchown(%s, %i, -1)\n", full_path, to_uid);

			errcode = 0;
#if defined(HAVE_LCHOWN)
			errcode = lchown(full_path, to_uid, -1);
#else
			if (!S_ISLNK(st.st_mode)) {
				errcode = chown(full_path, to_uid, -1);
			}
#endif

			if (errcode) {
				// couldn't unlink, so return failure
				return errno;
			}
		}
	}

	closedir(dir);

	// chown the passed in dir
	// (we don't need to lchown here since we shouldn't be called
	//  with a symlink as our argument)

	//printf("chown(%s, %i, -1)\n", current_path, to_uid);

	errcode = chown(current_path, to_uid, -1);

	if (errcode) {
		// couldn't lchown, so return failure
		return errno;
	}

	// return success
	return 0;
}


// this function DOES remove the directory passed in.
int helper_function_rmrf_dir(char *current_path) {

	// not yet implemented
	DIR *dir;
	struct dirent *dp;
	struct stat st;

	int result = 0;

	//printf ("current_path: %s\n", current_path);
	if ((dir = opendir(current_path)) == NULL) {
		// cannot open.
		return errno;
	}

	// the directory was succesfully opened, so iterate through all the entries
	// and deal with them appropriately
	while ((dp = readdir(dir)) != NULL) {
		// if this is a directory, we should recurse into it unless the
		// directory name is . or ..
		// 
		// otherwise, unlink it whatever it is.

		// in both cases, we need to construct a buffer with the full pathname.
		// this is the two string lengths plus 2: a slash between them and a
		// NULL terminator.
		int full_path_len = strlen(current_path) + strlen(dp->d_name) + 2;

		// keep this on the stack, so it gets cleaned up nicely.
		//
		// JIM: would it be better to use the heap and malloc/free?
		//
		char full_path[full_path_len];
		strcpy(full_path, current_path);
		strcat(full_path, "/");
		strcat(full_path, dp->d_name);

		// we must lstat the file to see if it's a directory
		result = lstat(full_path, &st);
		if (result) {
			return errno;
		}

		if (S_ISDIR(st.st_mode)) {
			// directory.  check for . and ..
			if (strcmp(dp->d_name, ".") && strcmp(dp->d_name, "..")) {
				// this is a directory.  we need to recurse into it.
				result = helper_function_rmrf_dir(full_path);
				if (result) {
					// something failed, bail (and pass code through)
					return result;
				}
			}
		} else {
			// this is not a dir entry -- unlink it.

			int errcode = 0;
			//errcode = unlink(full_path);

			//printf("unlink(%s) -- type %i\n", full_path, dp->d_type);

			if (errcode) {
				// couldn't unlink, so return failure
				return errno;
			}
		}
	}

	closedir(dir);

	// all the contents are now succesfully removed, so remove the
	// directory itself.
	//printf("rmdir(%s)\n", current_path);

	result = 0;
	//result = rmdir(current_path);

	if (!result) {
		// rmdir failed -- return the errno.
		return errno;
	}

	// return success
	return 0;
}


// ZERO is success
// NONZERO is an errno
int helper_function_set_identity(int target_uid, int target_gid) {

	// become the correct group
/* need to do this in a cross-platform manner.  for now, linux only.
	if (setegid(target_gid)) {
		// failed!
		return errno;
	}
*/
	if (setgid(target_gid)) {
		// failed!
		return errno;
	}

	// become the correct user
/* need to do this in a cross-platform manner.  for now, linux only.
	if (seteuid(target_uid)) {
		// failed!
		return errno;
	}
*/
	if (setuid(target_uid)) {
		// failed!
		return errno;
	}

	// success!
	return 0;
}


// NONZERO means a match was made
// ZERO means the item was not found
//
// list must be terminated with a -1
int helper_function_check_int_whitelist(int test, int* whitelist) {

	int current_index = 0;

	if (!whitelist) {
		// NULL list means everything is allowed?

		// found
		return 1;
	}

	current_index = 0;
	while(whitelist[current_index] != -1) {
		if (test == whitelist[current_index]) {
			// match!
			return 1;
		}

		// didn't match, move to next entry
		current_index++;
	}

	// not found
	return 0;
}

// NONZERO means a match was made
// ZERO means the item was not found
//
// list must be terminated with a NULL
int helper_function_check_string_whitelist(char* file, char** whitelist) {

	int current_index;

	if (!whitelist) {
		// NULL list means everything is allowed?

		// found
		return 1;
	}

	current_index = 0;
	while(whitelist[current_index]) {
		int current_len = strlen(whitelist[current_index]);
		if (strncmp(whitelist[current_index], file, current_len) == 0) {
			// match!
			return 1;
		}

		// didn't match, move to next entry
		current_index++;
	}

	// not found
	return 0;
}


int function_spawn_procd(char** args) {

	int master_pid;
	char pidbuf[16];
	char *exec_argv[3];

	// stay root in this case!

	// there should be no args for this function
	if (args[0]) {
		// fail!
		return 1;
	}

	// it's the master that spawned us, so get the ppid and pass that on the
	// command line
	master_pid = (int)getppid();

	// now convert to a string
	sprintf(pidbuf, "%i", master_pid);

	// construct the argv array.
	exec_argv[0] = condor_procd;
	exec_argv[1] = pidbuf;
	exec_argv[2] = 0;
	execv(condor_procd, exec_argv);

	// if we are here, execv failed, so return failure!
	return 39;
}


int function_spawn_transferd(char **args) {

	int result;
	char *exec_argv[2];

	// first, validate that args[0] is NULL
	if (!args[0]) {
		// fail!
		return 41;
	}

	result = 0;
	
	// construct the argv array.
	exec_argv[0] = condor_transferd;
	exec_argv[1] = 0;
	result = execv(condor_transferd, exec_argv);

	// if we are here, execv failed, so return failure!
	return 79;
}


int function_spawn_user_job(char **args) {

	int result;
	char* executable;
	char** arguments;
	struct stat st;

	// args[0] is the executable
	// args[1...] are passed through

	// make sure args are sane
	if (!args[0]) {
		return 81;
	}

	executable = args[0];
	arguments = &args[0];

	result = 0;

	// check the executable prefix whitelist -- nonzero means found
	result = helper_function_check_string_whitelist(executable,
	                                                condor_execute_prefix_whitelist);
	if (!result) {
		return 83;
	}

	// stat the file to check the owner (follow symlinks in this case)
	result = stat(executable, &st);
	if (result) {
		return 84;
	}

	// check the executable uid whitelist -- nonzero means found
	result = helper_function_check_int_whitelist(st.st_uid,
	                                             condor_execute_uid_whitelist);
	if (!result) {
		return 85;
	}

	// if we got here, all checks have passed and we are already running as the
	// target user.  so, spawn the job!
	result = execv(executable, arguments);

	// if we got here, execv failed!
	return 119;

}

int old_function_spawn_user_job(char **args) {

	int result;
	struct stat st;

	// args[0] is executable
	// args[1...] are passed through

	// make sure args are sane
	if (!args[0]) {
		return 81;
	}

	result = 0;

	// check the executable prefix whitelist -- nonzero means found
	result = helper_function_check_string_whitelist(args[0], condor_execute_prefix_whitelist);
	if (!result) {
		return 83;
	}

	// stat the file to check the owner (follow symlinks in this case)
	result = stat(args[0], &st);
	if (result) {
		return 84;
	}

	// check the executable uid whitelist -- nonzero means found
	result = helper_function_check_int_whitelist(st.st_uid, condor_execute_uid_whitelist);
	if (!result) {
		return 85;
	}

	// if we got here, all checks have passed and we are already running as the
	// target user.  so, spawn the job!
	result = execv(args[0], &args[0]);

	// if we got here, execv failed!
	return 119;

}


// this function will rm -rf a subdirectory
int function_cleanup_user_dir(char **args) {

	int result = 0;
	char *path;
	char *dir_location;

	// first, validate that args[0], args[1], args[2] exist and args[3] is NULL
	if (!(args[0] && args[1] == 0)) {
		// fail!
		return 121;
	}


/*
	// switch uid and gid to the expected user
	result = helper_function_set_identity(args[0], args[1]);
	if (result) {
		return 122;
	}
*/

	// the directory must have a dirname equal to condor_user_dir.
	// dirname may modify its argument, so make a temp copy.
	path = strdup(args[2]);
	dir_location = dirname(path);
	result = strcmp(dir_location, condor_user_dir);
	free(path);
	if (result) {
		// doesn't match, bail!
		return 123;
	}

	// use helper_function_rmrf_dir()
	result = helper_function_rmrf_dir(args[2]);
	if (result) {
		return 124;
	}

	// success
	return 0;
}


/*
// this function will rm -rf a subdirectory of spool
int function_cleanup_execute_dir(char **args) {

	int result = 0;
	char *path;
	char *dir_location;

	// first, validate that args[0], args[1], args[2] exist and args[3] is NULL
	if (!(args[0] && args[1] && args[2] && args[3] == 0)) {
		// fail!
		return 161;
	}

	// switch uid and gid to the expected user
	result = helper_function_set_identity(args[0], args[1]);
	if (result) {
		return 162;
	}

	// the directory must have a dirname equal to condor_spool_dir.
	// dirname may modify its argument, so make a temp copy.
	path = strdup(args[2]);
	dir_location = dirname(path);
	result = strcmp(dir_location, condor_execute_dir);
	free(path);
	if (result) {
		// doesn't match, bail!
		return 163;
	}

	// use helper_function_rmrf_dir()
	result = helper_function_rmrf_dir(args[2]);
	if (result) {
		return 164;
	}

	// success
	return 0;
}
*/


int function_create_user_dir(char **args) {

	uid_t target_uid;
	gid_t target_gid;
	char* pathname;

	if (!args[0] || !args[1] || !args[2] || args[3]) {
		return 181;
	}
	
	target_uid = atoi(args[0]);
	if (target_uid == 0) {
		return 182;
	}
	
	target_gid = atoi(args[1]);
	if (target_gid == 0) {
		return 183;
	}

	pathname = args[2];

	// first make the directory (as root)
	//
	if (mkdir(pathname, 0755) == -1) {
		return 184;
	}

	// now chown it to the correct user and group
	//
	if (chown(pathname, target_uid, target_gid) == -1) {
		return 185;
	}

	// success
	return 0;
}


int function_recursive_chown( char **args ) {

	int target_uid;
	int retval;

	target_uid = atoi(args[0]);
	if (!target_uid) {
		// target_uid cannot be zero!  fail!
		return -1;
	}

	retval = helper_function_chown_dir( args[1], -1, target_uid );

	logprint("recursive_chown %s to uid %i returns %i\n", args[1],
			target_uid, retval);

	// success
	return 0;
}

int privsep_open_server(char*, int, mode_t);

int function_open( char **args ) {

	char* path;
	int flags;
	mode_t mode;
	int result;

	// verify the argument list is the right size
	//
	if (!args[0] || !args[1] || !args[2] || args[3]) {
		return 190;
	}

	path = args[0];
	flags = atoi(args[1]);
	mode = (mode_t)atoi(args[2]);

	result = privsep_open_server(path, flags, mode);
	if (result != 0) {
		return result;
	}

	// success
	//
	return 0;
}

int function_rename( char **args ) {

	char* old_path;
	char* new_path;

	// verify the argument list is the right size
	//
	if (!args[0] || !args[1] || args[2]) {
		return 201;
	}

	old_path = args[0];
	new_path = args[1];
	if (rename(old_path, new_path) == -1) {
		return 202;
	}

	return 0;
}

int function_chmod( char **args ) {

	char* path;
	mode_t mode;

	// verify the argument list is the right size
	//
	if (!args[0] || !args[1] || args[2]) {
		return 211;
	}

	path = args[0];
	mode = (mode_t)atoi(args[1]);
	if (chmod(path, mode) == -1) {
		return 212;
	}

	return 0;
}

int main(int argc, char** argv) {

	int result;
	int calling_uid;
	int calling_gid;
	int target_uid;
	int target_gid;
	int function;

	//printf ("uid %i, euid %i, gid %i, egid %i\n",
	//		getuid(), geteuid(), getgid(), getegid());

	// needs to be done as root
	if (!ParseConfigFile()) {
		exit(200);
	}

	// verify the caller is the condor uid/gid
	calling_uid = getuid();
	calling_gid = getgid();
/*
	if (calling_uid != condor_uid) {
		exit(201);
	}
	if (calling_gid != condor_gid) {
		exit(202);
	}
*/

	// verify that we got a command int
	if (!argv[1]) {
		exit(203);
	}

	// get the enumerated function
	function = atoi(argv[1]);

	// function 1, 5, and 6 remain root
	// all others have a uid and gid passed on the command line

	target_uid = 0;
	target_gid = 0;

	// consume and validate the uid and gid parameters
	if ((function != 1) && (function != 5) && (function != 6)) {
		// make sure the uid and gid strings are not null
		if (!argv[2] || !argv[3]) {
			return -1;
		}

		// next, validate that we have a positive integer uid
		target_uid = atoi(argv[2]);
		if (!target_uid) {
			// target_uid cannot be zero!  fail!
			return -1;
		}

		// next, validate that we have a positive integer gid
		target_gid = atoi(argv[3]);
		if (!target_gid) {
			// target_gid cannot be zero!  fail!
			return 43;
		}
	}


	// drop all supplementary groups in all cases
	result = 0;
/*
	result = setgroups(1, &target_gid);
	if (result) {
		// fail
		return -1;
	}
*/


	// become the final user for all functions except 1, 5, and 6.
	if ((function != 1) && (function != 5) && (function != 6)) {
		result = helper_function_set_identity(target_uid, target_gid);
		if (result) {
			// fail
			return -1;
		}
	}

	//printf("invoking function %i\n", function);
	fflush(0);

	// now invoke the correct function
	switch (function) {
		case 0: // no int on the command line?
			exit(204);
		case 1:
			exit(function_spawn_procd(&argv[2]));
		case 2:
			exit(function_spawn_transferd(&argv[4]));
		case 3:
			exit(function_spawn_user_job(&argv[4]));
		case 4:
			exit(function_cleanup_user_dir(&argv[4]));
		case 5:
			exit(function_create_user_dir(&argv[2]));
		case 6:
			exit(function_recursive_chown(&argv[2]));
		case 7:
			exit(function_open(&argv[4]));		
		case 8:
			exit(function_rename(&argv[4]));
		case 9:
			exit(function_chmod(&argv[4]));
		default:
			exit(205);
	}

	// we should never get here.
	exit(206);
}

