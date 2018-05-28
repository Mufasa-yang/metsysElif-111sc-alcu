/*
 NAME:Xiao Yang,Zhengyuan Liu
 EMAIL:avadayang@icloud.com,zhengyuanliu@ucla.edu
 ID:104946787,604945064
 */
#include <stdio.h>
#include <time.h>
#include <math.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <ulimit.h>
#include <poll.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include "ext2_fs.h"

#define BUF_SIZE 1024

#define OFFSET_SB 1024

int exit_badArgument = 1;
int exit_otherError = 2;
char *fileImage;
int FDImage;
struct ext2_super_block* superblock;
struct ext2_group_desc* groupDesc;

unsigned int blockSize=0;
unsigned int groupCount=0;

void interpreSuperblock() {
    if (pread(FDImage, superblock, sizeof(struct ext2_super_block), OFFSET_SB) < 0) {
        fprintf(stderr, "error: read image to superblock \n" );
        exit(exit_otherError);
    }

    if (superblock->s_magic != EXT2_SUPER_MAGIC) {//to verify is image is ext2 file system
        fprintf(stderr, "File system in image is not ext2 \n" );
        exit(exit_otherError);
    }

    blockSize=1024 << superblock->s_log_block_size;

    printf("SUPERBLOCK,%u,%u,%u,%u,%u,%u,%u\n",
           superblock->s_blocks_count,
           superblock->s_inodes_count,
           blockSize,
           superblock->s_inode_size,
           superblock->s_blocks_per_group,
           superblock->s_inodes_per_group,
           superblock->s_first_ino);

}

void interpreBlockBitmap(int groupIndex) {

    unsigned int i;
    int groupPrev=groupIndex-1;
    if(groupIndex==0)
        groupPrev=0;

    for(i = 0; i < blockSize; i++){
        uint8_t byte;

        if (pread(FDImage, &byte, 1,  groupPrev * superblock->s_blocks_per_group * blockSize  + (groupDesc+groupIndex)->bg_block_bitmap * blockSize +i) < 0) {
            fprintf(stderr, "error: read block bitmap to scanUnit \n" );
            exit(exit_otherError);
        }
        int temp=1;
        unsigned long j;
        for(j = 0; j < 8; j++){
            if((byte & temp) == 0){
                printf("BFREE,%lu\n", groupPrev * superblock->s_blocks_per_group  +  j+i*8 +1);
            }
            temp<<=1;
        }
    }
}

void interpreInodeBitmap(int groupIndex) {

    unsigned int i;
    int groupPrev=groupIndex-1;
    if(groupIndex==0)
        groupPrev=0;

    for(i = 0; i < blockSize; i++){
        uint8_t byte;

        if (pread(FDImage, &byte, 1,  groupPrev * superblock->s_blocks_per_group * blockSize  + (groupDesc+groupIndex)->bg_inode_bitmap * blockSize +i) < 0) {
            fprintf(stderr, "error: read block bitmap to scanUnit \n" );
            exit(exit_otherError);
        }
        int temp=1;
        unsigned long j;
        for(j = 0; j < 8; j++){
            if((byte & temp) == 0){
                //printf("BFREE,%lu\n", groupPrev * superblock->s_blocks_per_group  +  j+i*8 +1);
                printf("IFREE,%lu\n", groupPrev * superblock->s_blocks_per_group  +  j+i*8 +1);
            }
            temp<<=1;
        }
    }
}

// print an error message on stderr when system call fails and exit with a return code of 1
void system_call_fail(char *name) {
    fprintf(stderr, "Error: system call %s fails, info: %s\n", name, strerror(errno));
    exit(exit_otherError);
}

#define INDIRECT_BLOCK 1
#define D_INDIRECT_BLOCK 2
#define T_INDIRECT_BLOCK 3
#define N_BLOCK_POINTERS blockSize / sizeof(__u32)
#define INDIRECT_BLOCK_OFFSET EXT2_NDIR_BLOCKS
#define D_INDIRECT_BLOCK_OFFSET INDIRECT_BLOCK_OFFSET + N_BLOCK_POINTERS
#define T_INDIRECT_BLOCK_OFFSET D_INDIRECT_BLOCK_OFFSET + N_BLOCK_POINTERS * N_BLOCK_POINTERS

// summary for directory entries in indirect blocks of this directory
void dirEntriesIndirectBlockSummary(struct ext2_inode inode, int parent_inode_number, unsigned int offset, int i_indirect, int indirect_type) {
    unsigned int i = 0;
    int block_address = i_indirect * blockSize;

    for (i = 0; i < N_BLOCK_POINTERS; i++) {  // for each pointer in the indirect block
        if (offset >= inode.i_size)
            break;
        __u32 pblock;
        if (pread(FDImage, &pblock, sizeof(__u32), block_address + i * sizeof(__u32)) < 0)
            system_call_fail("pread at dirEntriesIndirectBlockSummary");
        if (pblock == 0)
            continue;

        if (indirect_type == INDIRECT_BLOCK) {
            unsigned int offset_in_block = 0;
            int data_block_addr = pblock * blockSize;
            while(offset_in_block < blockSize) {
                if (offset >= inode.i_size)
                    break;
                struct ext2_dir_entry dir_entry;
                if (pread(FDImage, &dir_entry, sizeof(struct ext2_dir_entry), data_block_addr + offset_in_block) < 0)
                    system_call_fail("pread at dirEntriesIndirectBlockSummary");
                if (dir_entry.inode != 0) {  // inode == 0 indicates that the entry is not used
                    printf("DIRENT,%d,%d,%d,%d,%d,'%s'\n",
                           parent_inode_number,
                           offset,
                           dir_entry.inode,
                           dir_entry.rec_len,
                           dir_entry.name_len,
                           dir_entry.name);
                }
                offset += dir_entry.rec_len;
                offset_in_block += dir_entry.rec_len;
            }
        }
        else if (indirect_type == D_INDIRECT_BLOCK) {
            dirEntriesIndirectBlockSummary(inode, parent_inode_number, offset, pblock, INDIRECT_BLOCK);
        }
        else if (indirect_type == T_INDIRECT_BLOCK) {
            dirEntriesIndirectBlockSummary(inode, parent_inode_number, offset, pblock, D_INDIRECT_BLOCK);
        }
    }
}

// summary for each svalid (non-zero I-node number) directory entry
void directoryEntriesSummary(struct ext2_inode inode, int parent_inode_number) {
    int i = 0;
    int entry_size = sizeof(struct ext2_dir_entry);
    unsigned int offset = 0;
    for (i = 0; i < EXT2_NDIR_BLOCKS; i++) {  // for each direct block
        if (inode.i_block[i] == 0)  // no further block
            break;
        unsigned int offset_in_block = 0;
        int block_address = inode.i_block[i] * blockSize;
        while(offset_in_block < blockSize) {
            if (offset >= inode.i_size)
                break;
            struct ext2_dir_entry dir_entry;
            if (pread(FDImage, &dir_entry, entry_size, block_address + offset_in_block) < 0)
                system_call_fail("pread at directoryEntriesSummary");
            if (dir_entry.inode != 0) {  // inode == 0 indicates that the entry is not used
                printf("DIRENT,%d,%d,%d,%d,%d,'%s'\n",
                       parent_inode_number,
                       offset,
                       dir_entry.inode,
                       dir_entry.rec_len,
                       dir_entry.name_len,
                       dir_entry.name);
            }
            offset += dir_entry.rec_len;
            offset_in_block += dir_entry.rec_len;
        }
    }
    if (inode.i_block[EXT2_IND_BLOCK] != 0) {  // has indirect block
        dirEntriesIndirectBlockSummary(inode, parent_inode_number, offset, inode.i_block[EXT2_IND_BLOCK], INDIRECT_BLOCK);
    }
    if (inode.i_block[EXT2_DIND_BLOCK] != 0) {  // has doubly-indirect block
        dirEntriesIndirectBlockSummary(inode, parent_inode_number, offset, inode.i_block[EXT2_DIND_BLOCK], D_INDIRECT_BLOCK);
    }
    if (inode.i_block[EXT2_TIND_BLOCK] != 0) {  // has triply-indirect block
        dirEntriesIndirectBlockSummary(inode, parent_inode_number, offset, inode.i_block[EXT2_TIND_BLOCK], T_INDIRECT_BLOCK);
    }
}

void indirectBlockSummary(int inode_number, int block_offset, int i_indirect, int indirect_type) {
    unsigned int i = 0;
    int block_address = i_indirect * blockSize;

    for (i = 0; i < N_BLOCK_POINTERS; i++) {  // for each pointer in the indirect block
        __u32 pblock;
        if (pread(FDImage, &pblock, sizeof(__u32), block_address + i * sizeof(__u32)) < 0)
            system_call_fail("pread at dirEntriesIndirectBlockSummary");
        if (pblock == 0)
            continue;

        if (indirect_type == INDIRECT_BLOCK) {
            printf("INDIRECT,%d,%d,%d,%d,%d\n",
                   inode_number,
                   INDIRECT_BLOCK,
                   block_offset + i,
                   i_indirect,
                   pblock);
        }
        else if (indirect_type == D_INDIRECT_BLOCK) {
            printf("INDIRECT,%d,%d,%lu,%d,%d\n",
                   inode_number,
                   D_INDIRECT_BLOCK,
                   block_offset + i * N_BLOCK_POINTERS,
                   i_indirect,
                   pblock);
            indirectBlockSummary(inode_number, block_offset + i * N_BLOCK_POINTERS, pblock, INDIRECT_BLOCK);
        }
        else if (indirect_type == T_INDIRECT_BLOCK) {
            printf("INDIRECT,%d,%d,%lu,%d,%d\n",
                   inode_number,
                   T_INDIRECT_BLOCK,
                   block_offset + i * N_BLOCK_POINTERS * N_BLOCK_POINTERS,
                   i_indirect,
                   pblock);
            indirectBlockSummary(inode_number, block_offset + i * N_BLOCK_POINTERS * N_BLOCK_POINTERS, pblock, D_INDIRECT_BLOCK);
        }
    }
}

void inodeSummary(int groupIndex)
{
    int groupPrev=groupIndex-1;
    if(groupIndex==0)
        groupPrev=0;

    struct ext2_inode * inodeTable;//each inode is stored in this table as one entry
    inodeTable = (struct ext2_inode *)malloc(superblock->s_inodes_per_group*sizeof(struct ext2_inode));
    if(inodeTable == NULL){
        fprintf(stderr, "cannot allocate space for inodeTable \n" );
        exit(exit_otherError);
    }
    if (pread(FDImage, inodeTable, superblock->s_inodes_per_group*sizeof(struct ext2_inode),  (groupPrev * superblock->s_blocks_per_group +(groupDesc+groupIndex)->bg_inode_table )* blockSize ) < 0) {
        fprintf(stderr, "error: read block bitmap to scanUnit \n" );
        exit(exit_otherError);
    }
    unsigned int i=0;

    for(i=0;i<superblock->s_inodes_per_group;i++)//every group has the same number of entries in this table
    {
        //only consider non-zero mode and non-zero link count
        uint16_t mode=inodeTable[i].i_mode;
        if(mode != 0 && inodeTable[i].i_links_count != 0 )
        {
            char type = '?';
            uint16_t highestBit = mode & 0xF000;
            switch (highestBit) {
                case 0x8000:
                    type='f';
                    break;
                case 0x4000:
                    type='d';
                    break;
                case 0xA000:
                    type='s';
                    break;
                default:
                    break;
            }

            char tmpBufLastChange[32];
            char tmpBufModification[32];
            char tmpBufLastAccess[32];

            long int time=inodeTable[i].i_ctime;
            long int timeM=inodeTable[i].i_mtime;
            long int timeA=inodeTable[i].i_atime;

            strftime(tmpBufLastChange, 32, "%m/%d/%y %H:%M:%S", gmtime(&time));
            strftime(tmpBufModification, 32, "%m/%d/%y %H:%M:%S", gmtime(&timeM));
            strftime(tmpBufLastAccess, 32, "%m/%d/%y %H:%M:%S", gmtime(&timeA));

            printf("INODE,%u,%c,%o,%u,%u,%u,%s,%s,%s,%u,%u",
                   i+1,
                   type,
                   mode & 0x0FFF,
                   inodeTable[i].i_uid,
                   inodeTable[i].i_gid,
                   inodeTable[i].i_links_count,
                   tmpBufLastChange,
                   tmpBufModification,
                   tmpBufLastAccess,
                   inodeTable[i].i_size,
                   inodeTable[i].i_blocks
                   );
            //15 block address
            if (type == 's')//special case when symbolic link file length is less than the size of the block pointers (60 bytes)
            {
                if(inodeTable[i].i_size<60)
                {
                    //printf(",%s\n",(char *)inodeTable[i].i_block);//this just prints stored name
                    printf(",%u\n",inodeTable[i].i_block[0]);
                    continue;
                }
            }
            int j=0;
            for(j=0;j<15;j++)//15 address
            {
                printf(",%u",inodeTable[i].i_block[j]);
            }
            printf("\n");

            struct ext2_inode inode = inodeTable[i];
            if (type == 'd')
                directoryEntriesSummary(inode, i + 1);  // summary for each svalid (non-zero I-node number) directory entry
            // summary for indirect block references
            if (inode.i_block[EXT2_IND_BLOCK] != 0) {  // has indirect block
                indirectBlockSummary(i + 1, INDIRECT_BLOCK_OFFSET, inode.i_block[EXT2_IND_BLOCK], INDIRECT_BLOCK);
            }
            if (inode.i_block[EXT2_DIND_BLOCK] != 0) {  // has doubly-indirect block
                indirectBlockSummary(i + 1, D_INDIRECT_BLOCK_OFFSET, inode.i_block[EXT2_DIND_BLOCK], D_INDIRECT_BLOCK);
            }
            if (inode.i_block[EXT2_TIND_BLOCK] != 0) {  // has triply-indirect block
                indirectBlockSummary(i + 1, T_INDIRECT_BLOCK_OFFSET, inode.i_block[EXT2_TIND_BLOCK], T_INDIRECT_BLOCK);
            }
        }
    }
}

void interpreGroup()
{

    groupCount=(unsigned int)ceil((double)(superblock->s_blocks_count)/(double)(superblock->s_blocks_per_group));//ceil to include last one

    unsigned int groupDescSize = groupCount * sizeof(struct ext2_group_desc);

    groupDesc = (struct ext2_group_desc *)malloc(groupDescSize);
    if(groupDesc == NULL){
        fprintf(stderr, "cannot allocate space for groupDesc \n" );
        exit(exit_otherError);
    }

    if (pread(FDImage, groupDesc, groupDescSize, OFFSET_SB + blockSize) < 0) {
        fprintf(stderr, "error: read image to group descriptor \n" );
        exit(exit_otherError);
    }

    uint32_t numOfBlocksInGroup = superblock->s_blocks_per_group;//normal case, if is not the last group,
    uint32_t numOfInodesInGroup = superblock->s_inodes_per_group;

    unsigned int i=0;
    unsigned int groupPrev=0;
    for(i=0;i<groupCount;i++)
    {
        if(i+1==groupCount)//last one
        {
            if(i>0)
                groupPrev=i-1;//get how many groups in front of current one
            numOfBlocksInGroup=superblock->s_blocks_count-(groupPrev*superblock->s_blocks_per_group);
            numOfInodesInGroup=superblock->s_inodes_count -(groupPrev*superblock->s_inodes_per_group);
        }
        printf("GROUP,%u,%u,%u,%u,%u,%u,%u,%u\n",
               i,
               numOfBlocksInGroup,
               numOfInodesInGroup,
               (groupDesc+i)->bg_free_blocks_count,
               (groupDesc+i)->bg_free_inodes_count,
               (groupDesc+i)->bg_block_bitmap,
               (groupDesc+i)->bg_inode_bitmap,
               (groupDesc+i)->bg_inode_table);

        interpreBlockBitmap(i);//free block entries
        interpreInodeBitmap(i);//free I-node entries
        inodeSummary(i);//I-node summary
    }
}

int main(int argc, char* argv[])
{
    if (argc != 2) {
        fprintf(stderr, "bad argument \n" );
        exit(exit_badArgument);
    }

    fileImage = argv[1];
    FDImage = open(fileImage, O_RDONLY);
    if (FDImage < 0) {
        fprintf(stderr, "cannot open image file \n" );
        exit(exit_otherError);
    }
    //allocate space for superblock
    superblock = (struct ext2_super_block *)malloc(sizeof(struct ext2_super_block ));

    if(superblock==NULL)
    {
        fprintf(stderr, "malloc for superblock fail \n" );
        exit(exit_otherError);
    }

    interpreSuperblock();//superblock summary
    interpreGroup();//group summary

    free(superblock);
    free(groupDesc);
    return 0;

}
