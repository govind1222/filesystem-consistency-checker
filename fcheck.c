#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "../E2/xv6/include/types.h"
#include "../E2/xv6/include/fs.h"

#define stat xv6_stat
#include "../E2/xv6/include/stat.h"
#undef stat

// methods that xv6 used to read indirect block
uint xint(uint x);
void rsect(int fsfd, uint sec, void *buf);

int main(int argc, char *argv[]) {

    struct superblock *sb;
    struct dinode *dip;
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
    // checks each inode to make sure it isn't bad
    for (i = 0; i < numInodes; i++) {
        // unallocated
        if(dip[i].size == 0) {
            continue;
        }
        // invalid size or not of the right type
        // bad inode error
        if(dip[i].size < 0 || (dip[i].type != T_FILE && dip[i].type != T_DIR && dip[i].type != T_DEV)) {
            fprintf(stderr, "ERROR: bad inode.\n");
            exit(1);
        }

        // check each address entry in the addrs array
        for (j = 0; j < NDIRECT; j++) {
            // data block address not in use
            if(!dip[i].addrs[j]) {
                continue;
            }

            // checks to see if direct block is pointing to a valid data
            // block address
            if(dip[i].addrs[j] < 0 || dip[i].addrs[j] > sb->nblocks) {
                fprintf(stderr, "ERROR: bad direct address in inode.\n");
                exit(1);
            }

            // make sure it's marked as in use by the bit map
            uint bn = dip[i].addrs[j];
            char *bitmapBlock = mapResult + (BBLOCK(bn, sb->ninodes)) * BSIZE;
            if(!(bitmapBlock[bn / 8] & (0x1 << (bn % 8)))) {
                fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.\n");
                exit(1);
            }
        }



        // if execution reaches here, direct blocks are all valid
        // now need to check indirect blocks
        if(!dip[i].addrs[NDIRECT]) {
            continue;
        }

        uint temp = dip[i].addrs[NDIRECT];
        uint indirect[NINDIRECT];

        // reads the indirect block address
        uint y = xint(temp);
        rsect(fd, y, (char *)indirect);

        // loops through all NINDIRECT addresses and checks to see if they are
        // a valid block number
        for (j = 0; j < NINDIRECT; j++) {
            int indirectBn = indirect[j];
            // indirect block address not in use
            if(!indirectBn) {
                continue;
            }

            // checks to see if indirect block is pointing to a valid data
            // block address
            if(indirectBn < 0 || indirectBn > sb->nblocks) {
                fprintf(stderr, "ERROR: bad indirect address in inode.\n");
                exit(1);
            }

            // makes sure the inode is marked as in use by the
            // bitmap
            char *bitmapBlock = mapResult + (BBLOCK(indirectBn, sb->ninodes)) * BSIZE;
            if(!(bitmapBlock[indirectBn / 8] & (0x1 << (indirectBn % 8)))) {
                fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.\n");
                exit(1);
            }
        }
    }

    de = (struct dirent *)(mapResult + (dip[ROOTINO].addrs[0]) * BSIZE);
    int size = dip[ROOTINO].size / sizeof(struct dirent *);
    for(i = 0; i < size; i++, de++) {
        // checks to see if . entry has the same inode as itself
        if(strcmp(de->name, ".") == 0) {
            if(de->inum != ROOTINO) {
                fprintf(stderr, "ERROR: root directory does not exist.\n");
                exit(1);
            }
            break;
        }
    }

    // check that the root directory exists
    // TODO: need to check if the parent is the root itself
    if(ROOTINO != 1 || dip[ROOTINO].size <= 0) {
        fprintf(stderr, "ERROR: root directory does not exist.\n");
        exit(1);
    }

    return 0;
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