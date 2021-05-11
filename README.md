# filesystem-consistency-checker
This project is to gain experience with xv6's file system. The program fcheck.c takes in a file system image from xv6 as an argument and conducts some checks on the image to make sure the file system is in a consistent state.

## Checks Conducted
These are the checks that are being conducted by this program.
1. Each inode is either unallocated or one of the valid types (T_FILE, T_DIR, T_DEV). These are constant defined in xv6.
2. For in-use inodes, each block address that is used by the inode is valid (points to a valid data block address within the image).
3. Root directory exists, its inode number is 1, and the parent of the root directory is itself.
4. Each directory contains . and .. entries, and the . entry points to the directory itself.
5. For in-use inodes, each block address in use is also marked in use in the bitmap.
6. For blocks marked in-use in bitmap, the block should actually be in-use in an inode or indirect block somewhere.
7. For in-use inodes, each direct address in use is only used once.
8. For in-use inodes, each indirect address in use is only used once.
9. For all inodes marked in use, each must be referred to in at least one directory.
10. For each inode number that is referred to in a valid directory, it is actually marked in use.
11. Reference counts (number of links) for regular files match the number of times file is referred to in directories (i.e., hard links work correctly).
12. No extra links allowed for directories (each directory only appears in one other directory).

## Compilation
This program must be compiled in a Linux environment. Furthermore, some header files from xv6 are necessary to run this program, specifically fs.h, types.h, and stat.h.

```bash
gcc fcheck.c -o fcheck -Wall -Werror -O
```
## Usage
This file requires an image file to be passed in as an argument. This program is written to test against xv6 file system images specifically. The program was tested with image files that had inconsistencies built into it. The specific checks performed are given above.

```bash
./fcheck <file_system_image>
```
