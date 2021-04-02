struct stat;
struct rtcdate;

// system calls
// If not otherwise stated,
// return 0 for no error,
//       -1 if there’s an error.
int fork(void);
int exit(int) __attribute__((noreturn));
int wait(int*);
int pipe(int*);
int write(int, const void*, int);
// Read n bytes into buf; returns number read; or 0 if end of file.
int read(int, void*, int);
int close(int);
int kill(int);
int exec(char*, char**);
/**
 * fcntl.h
 * #define O_RDONLY  0x000
 * #define O_WRONLY  0x001
 * #define O_RDWR    0x002
 * #define O_CREATE  0x200
 * #define O_TRUNC   0x400
 */
int open(const char*, int);
int mknod(const char*, short, short);
int unlink(const char*);
/**
 * #define T_DIR     1   // Directory
 * #define T_FILE    2   // File
 * #define T_DEVICE  3   // Device
 * struct stat {
 *   int dev;     // File system's disk device
 *   uint ino;    // Inode number
 *   short type;  // Type of file
 *   short nlink; // Number of links to file
 *   uint64 size; // Size of file in bytes
 };
 */
int fstat(int fd, struct stat*);
int link(const char*, const char*);
int mkdir(const char*);
int chdir(const char*);
int dup(int);
int getpid(void);
char* sbrk(int);
int sleep(int);
int uptime(void);

// ulib.c
int stat(const char*, struct stat*);
char* strcpy(char*, const char*);
void* memmove(void*, const void*, int);
char* strchr(const char*, char c);
int strcmp(const char*, const char*);
void fprintf(int, const char*, ...);
void printf(const char*, ...);
char* gets(char*, int max);
uint strlen(const char*);
void* memset(void*, int, uint);
void* malloc(uint);
void free(void*);
int atoi(const char*);
int memcmp(const void*, const void*, uint);
void* memcpy(void*, const void*, uint);
