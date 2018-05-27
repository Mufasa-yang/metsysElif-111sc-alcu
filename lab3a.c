/*
 NAME:Xiao Yang
 EMAIL:avadayang@icloud.com
 ID:104946787
 */
#include <stdio.h>
#include<time.h>
#include <math.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/time.h>
#include<fcntl.h>
#include <stdlib.h>
#include<errno.h>
#include <ulimit.h>
#include <poll.h>
#include <string.h>
#include <ctype.h>
#include "ext2_fs.h"
#include <stdint.h>
#define BUF_SIZE 1024

#define OFFSET_SB    1024

int exit_badArgument =1;
int exit_otherError =2;
char *fileImage;
int FDImage;
struct ext2_super_block* superblock;
struct ext2_group_desc* groupDesc;

unsigned int blockSize=0;
unsigned int groupCount=0;


ssize_t
doRead(int fd,  void *buff, size_t count)
{
    ssize_t res;
    res = read (fd, buff, count);
    if(res <0)
    {
        int tempErr=errno;
        char * argument="read error:";
        fprintf(stderr, "%s %s\n", argument,  strerror(tempErr));
        exit(1);
    }
    else
        return res;
}



void interpreSuperblock()
{
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
                printf("BFREE,%lu\n", groupPrev * superblock->s_blocks_per_group  +  j+i*8 +1);
            }
            temp<<=1;
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
            
            strftime(tmpBufLastChange, 32, "%m/%d/%y %H:%M:%S", localtime(&time));
            strftime(tmpBufModification, 32, "%m/%d/%y %H:%M:%S", localtime(&timeM));
            strftime(tmpBufLastAccess, 32, "%m/%d/%y %H:%M:%S", localtime(&timeA));
            
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

