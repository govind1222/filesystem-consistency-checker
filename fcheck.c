#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "include/types.h"
#include "include/fs.h"

#define T_DIR  1   // Directory
#define T_FILE 2   // File
#define T_DEV  3   // Special device

#define DPB (BSIZE / sizeof(struct dirent))

struct superblock *sb;
struct dinode *dip;

// methods that xv6 used to read indirect block
uint xint(uint x);
void rsect(int fsfd, uint sec, void *buf);

uint findInodeInDir(int inum, char *addr, int fd);

int main(int argc, char *argv[]) {

    struct dirent *de;

    // making sure that there is 1 and only 1 argument
    // should be the path to the file system image
    if(argc != 2) {
        fprintf(stderr, "Usage: fcheck <file_system_image>\n");
        exit(1);
    }

    // opening the file and throwing an error if open fails
    int fd = open(argv[1], O_RDONLY);
    if(fd < 0) {
        fprintf(stderr, "image not found.\n");
        exit(1);
    }

    // gets the size of the file that the fd is pointing to
    struct stat statbuf;
    fstat(fd, &statbuf);

    // using mmap to read in the data from the image file
    char *mapResult = mmap(NULL, statbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

    // makes sure the file system image was properly read
    if(mapResult == MAP_FAILED) {
        perror("mmap failed");
        exit(1);
    }

    // reads in the superblock
    sb = (struct superblock *) (mapResult + 1 * BSIZE);
    // reads in the inodes
    dip = (struct dinode *) (mapResult + IBLOCK(0) * BSIZE);

    int numInodes = sb->ninodes;
    int i, j;

    // check that the root directory exists
    if(ROOTINO != 1 || dip[ROOTINO].size <= 0) {
        fprintf(stderr, "ERROR: root directory does not exist.\n");
        exit(1);
    }

    de = (struct dirent *)(mapResult + (dip[ROOTINO].addrs[0]) * BSIZE);
    int size = dip[ROOTINO].size / sizeof(struct dirent), c = 0;
    for(i = 0; i < size; i++, de++) {
        // checks to see if . entry has the same inode as itself
        if(c < 2 && (strcmp(de->name, ".") == 0 || strcmp(de->name, "..") == 0)) {
            if(de->inum != ROOTINO) {
                fprintf(stderr, "ERROR: root directory does not exist.\n");
                exit(1);
            }
            else
                c++;
        }
    }

    // array to keep track of which blocks are in use
    uint checkUsedBlocks[sb->size];

    // block number of the first data block
    uint startBlock = BBLOCK(sb->size, sb->ninodes) + 1;

    // everything else defaults to 0 for now
    for (i = 0; i < sb->size; i++) {
        checkUsedBlocks[i] = 0;
    }

    // the first block, superblock, blocks containing inodes, and
    // bitmap blocks are all marked as used in the bitmap
    for(i = 0; i < startBlock; i++) {
        //printf("%d ", i);
        checkUsedBlocks[i] = 1;
    }

    // checks each inode to make sure it isn't bad
    for (i = 0; i < numInodes; i++) {
        // unallocated
        if (dip[i].size == 0) {
            continue;
        }

        // invalid size or not of the right type
        // bad inode error
        if (dip[i].size < 0 || (dip[i].type != T_FILE && dip[i].type != T_DIR && dip[i].type != T_DEV)) {
            fprintf(stderr, "ERROR: bad inode.\n");
            exit(1);
        }

        c = 0;

        // check each address entry in the addrs array
        for (j = 0; j < NDIRECT; j++) {
            // data block address not in use
            if (!(dip[i].addrs[j])) {
                continue;
            }

            // checks to see if direct block is pointing to a valid data
            // block address
            if (dip[i].addrs[j] < 0 || dip[i].addrs[j] > sb->nblocks) {
                fprintf(stderr, "ERROR: bad direct address in inode.\n");
                exit(1);
            }

            // make sure it's marked as in use by the bit map
            uint bn = dip[i].addrs[j];
            char *bitmapBlock = mapResult + (BBLOCK(bn, sb->ninodes)) * BSIZE;
            if (!(bitmapBlock[bn / 8] & (0x1 << (bn % 8)))) {
                fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.\n");
                exit(1);
            }

            // checks if current data block has been used yet
            if (checkUsedBlocks[bn]) {
                fprintf(stderr, "ERROR: direct address used more than once.\n");
                exit(1);
            }
            // marks current data block as in use
            checkUsedBlocks[bn] = 1;

            //check entries of directory to make sure it has "."
            //and ".." entries and that "." points to itself
            if(dip[i].type == T_DIR) {
                de = (struct dirent *) (mapResult + bn * BSIZE);
                int size = dip[i].size / sizeof(struct dirent), d;
                for(d = 0; d < size; d++, de++) {
                    if(c < 2) {
                        if(strcmp(de->name, ".") == 0) {
                            if(de->inum == i) {
                                c++;
                            } else {
                                fprintf(stderr, "ERROR: directory not properly formatted.\n");
                                exit(1);
                            }
                        }
                        else if(strcmp(de->name, "..") == 0) {
                            c++;
                        }
                    }
                }
            }
        }

        // if the inode is a directory, c should be 2
        // c is incremented once for a "." directory
        if(dip[i].type == T_DIR && c != 2) {
            fprintf(stderr, "ERROR: directory not properly formatted.\n");
            exit(1);
        }

        // if execution reaches here, direct blocks are all valid
        // now need to check indirect blocks
        if (!(dip[i].addrs[NDIRECT])) {
            goto endOfLoop;
        }

        uint temp = dip[i].addrs[NDIRECT];
        // indirect block address is in use
        if(temp > 0 && temp < sb->size) {
            checkUsedBlocks[temp] = 1;
        }
        uint indirect[NINDIRECT];
        // reads the indirect block address
        uint y = xint(temp);
        rsect(fd, y, (char *)indirect);

        // loops through all NINDIRECT addresses and checks to see if they are
        // a valid block number
        for (j = 0; j < NINDIRECT; j++) {
            int indirectBn = indirect[j];

            // indirect block address not in use
            if (!indirectBn) {
                continue;
            }

            // checks to see if indirect block is pointing to a valid data
            // block address
            if (indirectBn < 0 || indirectBn > sb->nblocks) {
                fprintf(stderr, "ERROR: bad indirect address in inode.\n");
                exit(1);
            }

            // makes sure the inode is marked as in use by the
            // bitmap
            char *bitmapBlock = mapResult + (BBLOCK(indirectBn, sb->ninodes)) * BSIZE;
            if (!(bitmapBlock[indirectBn / 8] & (0x1 << (indirectBn % 8)))) {
                fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.\n");
                exit(1);
            }

            // marks current data block as in use
            if (checkUsedBlocks[indirectBn]) {
                fprintf(stderr, "ERROR: indirect address used more than once.\n");
                exit(1);
            }
            checkUsedBlocks[indirectBn] = 1;

        }

        endOfLoop:
        // checks to see if inode is found in directory
        if (i != ROOTINO && !findInodeInDir(i, mapResult, fd)) {
            fprintf(stderr, "ERROR: inode marked use but not found in a directory.\n");
            exit(1);
        }
    }

    // for every block marked as used in bitmap, make sure it is actually
    // in use somewhere
    for (i = 0; i < startBlock + sb->nblocks; i++) {
        char *bitmapBlock = mapResult + (BBLOCK(i, sb->ninodes)) * BSIZE;
        if ((bitmapBlock[i / 8] & (0x1 << (i % 8)))) {
            if(!checkUsedBlocks[i]) {
                fprintf(stderr, "ERROR: bitmap marks block in use but it is not in use.\n");
                exit(1);
            }
        }
    }

    // all checks completed, no errors found
    exit(0);
}

// included xint and rsect implementations from xv6 - from analyzing mkfs.c
// this is what allows us to read the block numbers from the indirect
// block address pointers
uint xint(uint x) {
    uint y;
    char *a = (char *)&y;
    a[0] = x;
    a[1] = x >> 8;
    a[2] = x >> 16;
    a[3] = x >> 24;
    return y;
}

void rsect(int fsfd, uint sec, void *buf) {
    if(lseek(fsfd, sec * 512L, 0) != sec * 512L){
        perror("lseek");
        exit(1);
    }
    if(read(fsfd, buf, 512) != 512){
        perror("read");
        exit(1);
    }
}

// this function checks to see if the given inode number
// is referred to in some directory. returns 1 if true, 0 otherwise
uint findInodeInDir(int inum, char* addr, int fd) {
    struct dirent *de;
    int i, j, k = 0;
    for (i = 0; i <= sb->ninodes; i++) {
        if (i != inum && dip[i].size && dip[i].type == T_DIR) {
            for (j = 0; j < NDIRECT; j++) {
                if(!dip[i].addrs[j]) {
                    continue;
                }
                de = (struct dirent *) (addr + (dip[i].addrs[j])*BSIZE);
                int size = dip[i].size / sizeof(struct dirent);
                for(k = 0; k < size && k < DPB; k++, de++) {
                    if(de->inum == inum) {
                        return 1;
                    }
                }
            }
            uint temp = dip[i].addrs[NDIRECT];
            if(!temp) continue;
            uint indirect[NINDIRECT];
            // reads the indirect block address
            uint y = xint(temp);
            rsect(fd, y, (char *)indirect);
            for(j = 0; j < NINDIRECT; j++) {
                int indirectBn = indirect[j];
                if(!indirectBn) {
                    continue;
                }
                de = (struct dirent *) (addr + (indirectBn) * BSIZE);
                int size = dip[i].size / sizeof(struct dirent);
                for(k = 0; k < size && k < DPB; k++, de++) {
                    if(de->inum == inum) {
                        return 1;
                    }
                }
            }
        }
    }
    return 0;
}

