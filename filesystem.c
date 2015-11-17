#include<stdio.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<string.h>
#include<unistd.h>
#include<time.h>

#define name_limit      15    //limit of the file name
#define BLOCK           4096      //block size in bytes
#define NBLOCK          64
#define SFS_CLEAN       0
#define LIMIT           9   //super block cache limit
#define IS_FREE         0
#define IS_DIR          1
#define IS_FILE         2
#define BLOCK_POINTERS 13
#define IOS_BEG         0
#define IOS_CURR        1
#define IOS_END         2

struct inode{
    int file_size;                  //file sizee in bytes
    int i_owner;                    //owner of file
    int i_atime;
    int i_ctime;
    int i_mtime;
    int i_mode;                     //mode of file:directory or normal file
    int i_uid;                      //user id
    int i_gid;                      //group id
    unsigned short i_blocks[BLOCK_POINTERS];   //12 direct + 1 indirect
};

#define NINODE 5
#define INODETABSIZE ((NINODE*BLOCK)/sizeof(struct inode))
#define DATABLOCK    (NBLOCK-NINODE-1)
#define PAGECOMP BLOCK*NINODE-(INODETABSIZE*sizeof(inodetable))

struct superblock{
    int sb_fs_size;
    char sb_name[name_limit];
    int sb_free_block_count;               //no of free blocks on the system
    unsigned int sb_free_block_list[10];      //free block list
    int sb_next_freeblock_index;                //next free index;
    int sb_inode_size;                     //size of the inode list
    int sb_freeinode_index;                     //no of free inode list
    unsigned int sb_free_inode_list[10];     //free inode list offset
    int sb_flags;                          //flag to indicate modification of super block
};

struct OnDiskDirEntry{
    char dirname[name_limit];
    int i_node;
};
struct InCoreDirEntry{
    int offset;
    struct inode INODE;
};
struct indirect_pointer{
int ptr_datablk;
};

#define INDIRECT_NO BLOCK/sizeof(struct indirectpinter)
struct InCoreINode{
    struct inode * INODE;
    int i_node;
    int offset;
    int mode;
    int ref_count;
    struct InCoreINode * left;
    struct  InCoreINode * right;
};
struct data_block_init{
    int cur,next;
};
char * devfiles[] = {"TEST",NULL};
int devfd[] = {-1, -1};

// Open the device
int OpenDevice(int dev)
{
	// Open the device related file for both reading and writing.
	//
	if ((devfd[dev] = open(devfiles[dev], O_RDWR)) < 0)
	{
		printf("Opening device file failure:");
		exit(0);
	}
	return devfd[dev];
}

// Shutdown the device
int ShutdownDevice(int dev)
{
	// if (dev < ... && dev > ...)
	if (devfd[dev] >= 0)
		close(devfd[dev]);

	return 0;
}

struct data_block_init db;  //used for initialisation of free data blocks
struct inode inodetable;    //used for initialisation of free inode blocks
struct inode * root_inode;
struct superblock sb;
struct indirect_pointer ptr;
struct InCoreINode * head,*tail;
struct OnDiskDirEntry dentry;
char token[name_limit];
struct InCoreDirEntry dirnode;

void init_superblock(struct superblock * sb)
{
    sb->sb_fs_size=BLOCK*NBLOCK;
    sb->sb_flags=SFS_CLEAN;
    int i;
    sb->sb_free_block_count=DATABLOCK;
    sb->sb_freeinode_index=0;
    sb->sb_inode_size=INODETABSIZE;
    sb->sb_next_freeblock_index=0;
    strcpy(sb->sb_name,"NIMIHS");
    for(i=0;i<=LIMIT;i++)
    {
        sb->sb_free_block_list[i]=i;
        sb->sb_free_inode_list[i]=i;
    }
}
int get_addressofblock(int blockno,int offset=0)
{
    return (6+blockno)*BLOCK+offset;
}

int get_inode_address(int inodeno)
{
    return (BLOCK+inodeno*sizeof(struct inode))*BLOCK;
}

void init_inodetable(struct inode * inodetable)
{
    inodetable->file_size=0;
    int i;
    inodetable->i_atime=inodetable->i_ctime=inodetable->i_mtime=-1;
    inodetable->i_uid=inodetable->i_gid=inodetable->i_owner=-1;
    inodetable->i_mode=IS_FREE;
    for(i=0;i<BLOCK_POINTERS;i++)
    {
        inodetable->i_blocks[i]=1;
    }
}
struct InCoreINode * init_inode(int i_no,int uid,int gid,int filesize,int mode,int perm)
{
    struct InCoreINode * temp=(struct InCoreINode *)malloc(sizeof(struct InCoreINode));
    temp->right=temp->left=NULL;
    temp->offset=0;
    temp->ref_count=1;
    temp->i_node=i_no;
    temp->mode=perm;
    temp->INODE=(struct inode *)malloc(sizeof(struct inode));
    temp->INODE->i_uid=uid;
    temp->INODE->i_gid=gid;
    temp->INODE->file_size=filesize;
    temp->INODE->i_mode=mode;
    temp->INODE->i_ctime=temp->INODE->i_mtime=temp->INODE->i_atime=time(0);
    return temp;
}
void fill_inode_cache(int dev)
{
    int i;
    lseek(devfd[dev],get_inode_address(0),SEEK_SET);
    for(i=0;i<INODETABSIZE;i++)
    {
        read(devfd[dev],&inodetable,sizeof(inodetable));
        if(inodetable.i_mode==IS_FREE)
        {
            sb.sb_free_block_list[++sb.sb_freeinode_index]=i;
        }
        if(sb.sb_freeinode_index==LIMIT+1)
            return;
    }
}
void fill_datablock_cache(int dbno,int dev){
    int i;
    if(dbno==-1)
        return ;
    lseek(devfd[dev],get_addressofblock(dbno),SEEK_SET);
    read(devfd[dev],&db,sizeof(db));
    lseek(devfd[dev],get_addressofblock(db.next),SEEK_SET);
    for(i=0;i<=LIMIT;i++)
    {
        read(devfd[dev],&db,sizeof(db));
        if(db.next==-1)
        {
            return ;
        }
        else
            sb.sb_free_block_list[sb.sb_next_freeblock_index++]=db.cur;
        lseek(devfd[dev],get_addressofblock(db.next),SEEK_SET);
    }
}
int getinode(int dev)
{
    int j;
    j=sb.sb_free_inode_list[sb.sb_freeinode_index];
    sb.sb_freeinode_index--;
        if(sb.sb_freeinode_index==-1)
        {
            fill_inode_cache(dev);
        }
    return j;
}
int alloc_data(int dev){
int     j=sb.sb_free_block_list[sb.sb_next_freeblock_index];
    sb.sb_next_freeblock_index--;
    if(sb.sb_next_freeblock_index==-1)
    {
        fill_datablock_cache(j,dev);
    }
    return j;
}
void initSFS(int dev)
{ //Assuming only predefined sizes are used
    char buf[4096];
    int i;
    init_superblock(&sb);
    write(devfd[dev],(&sb),BLOCK);
    init_inodetable(&inodetable);
    //initialising root directory inode
    head=init_inode(getinode(dev),1,1,BLOCK,IS_DIR,6);  //4+2=>read+write
      tail=head;
      head->INODE->i_blocks[0]=alloc_data(dev);
      strcpy(dentry.dirname,".");
      dentry.i_node=head->i_node;
      head->INODE->file_size=sizeof(dentry);
      write(devfd[dev],head->INODE,sizeof(head->INODE));
    for(i=1;i<INODETABSIZE;i++)
        write(devfd[dev],&inodetable,sizeof(inodetable));
      //   printf("%d\n",p);
    bzero(buf,PAGECOMP);
    write(devfd[dev],buf,PAGECOMP);
    write(devfd[dev],&dentry,BLOCK);
    head->offset=sizeof(dentry);
    for(i=1;i<DATABLOCK;i++)
    {
        if(i==DATABLOCK-1)
            db.next=-1;
        else
            db.next=i+1;
            db.cur=i;
        write(devfd[dev],&db,BLOCK);
    }
}
int write_inode(int dev,int i_node,struct inode * data,int size)
{
    lseek(devfd[dev],get_inode_address(i_node),SEEK_SET);
    write(devfd[dev],data,size);
}
int write_data(int dev,int i_node , void * data,int size)
{
    int flag=0;
    lseek(devfd[dev],get_inode_address(i_node),SEEK_SET);
    read(devfd[dev],&inodetable,sizeof(inodetable));
    int datablock_no=(inodetable.file_size)/BLOCK;
    int rem;
    if(datablock_no>BLOCK*12)
    {
        int indirect_ptr=(inodetable.file_size-12*BLOCK)/BLOCK;
        lseek(devfd[dev],get_addressofblock(inodetable->i_blocks[12],indirect_ptr),SEEK_SET);
        read(devfd[dev],&ptr,sizeof(ptr));
        rem=(inodetable.file_size%BLOCK);
        if((BLOCK-rem)<size)
        {
            lseek(devfd[dev],get_addressofblock(ptr.ptr_datablk,rem),SEEK_SET);
            write(devfd[dev],data,BLOCK-rem);
            if(datablock_no==INDIRECT_NO+11)
            {
                flag=1;  //size limit of file exceeded
                printf("THe size limit of a file has been exceeded\n");
            }
            else{
                    int newptr=alloc_data(dev);
                    if(newptr==-1)
                    {
                        flag=1;
                    }
                    else{
                    ptr.ptr_datablk=newptr;
                    lseek(devfd[dev],get_addressofblock(inodetable->i_blocks[12],(inodetable.file_size/BLOCK)-13),SEEK_SET);
                    write(devfd[dev],&ptr,sizeof(ptr));
                    lseek(devfd[dev],get_addressofblock(newptr),SEEK_SET);
                    write(devfd[dev],data+BLOCK-rem,size-(BLOCK-rem));
                    }
            }
        }
        else{
              lseek(devfd[dev],get_addressofblock(ptr.ptr_datablk,rem),SEEK_SET);
            write(devfd[dev],data,size);
        }
    }
    else{
        rem_bytes=(inodetable.file_size%BLOCK);
        if((BLOCK-rem)<size)
        {
            lseek(devfd[dev],get_addressofblock(inodetable->i_blocks[datablock_no],rem),SEEK_SET);
            write(devfd[dev],data,BLOCK-rem);
            if(datablock_no==11)
            {
                int newPtr=alloc_data(dev);
                if(newPtr==-1)
                {
                    flag=1;
                }
                else{
                    inodetable.i_blocks[12]=newPtr;
                    newPtr=alloc_data(dev);
                    if(newPtr==-1)
                    {
                        flag=1;
                    }
                    else{
                        ptr.ptr_datablk=newPtr;
                        lseek(devfd[dev],get_addressofblock(inodetable.i_blocks[12]),SEEK_SET);
                        write(devfd[dev],&ptr,sizeof(ptr));
                        lseek(devfd[dev],get_addressofblock(newPtr),SEEK_SET);
                        write(devfd[dev],data+BLOCK-rem,size-(BLOCK-rem));
                    }
                }
            }
            else{
                    newPtr=alloc_data(dev);
                    if(newPtr==-1)
                    {
                        flag=1;
                    }
                    else{
                        inodetable.i_blocks[datablock_no+1]=newPtr;
                        lseek(devfd[dev],get_addressofblock(inodetable.i_blocks[datablock_no+1]),SEEK_SET);
                        write(devfd[dev],data+BLOCK-rem,size-(BLOCK-rem));
                    }
            }
        }
        else{
            lseek(devfd[dev],get_addressofblock(inodetable->i_blocks[datablock_no],rem),SEEK_SET);
            write(devfd[dev],data,size);
            return 1;
        }
    }
    if(flag==1)
    {
        return -1;
    }
    else
        return 0;
}
int read_datablock(int dev,int offset,struct inode * INODE,void * data,int size)
{
    int blockno=(offset)/BLOCK;
    int ptr1;
    if(blockno>12)
    {
        blockno=(offset-12*BLOCK)/BLOCK;
        ptr1=offset%BLOCK;
        if(BLOCK-ptr1<size)
        {
            lseek(devfd[dev],get_addressofblock(INODE->i_blocks[12],blockno),SEEK_SET);
            read(devfd[dev],&ptr,sizeof(ptr));
            lseek(devfd[dev],get_addressofblock(ptr.ptr_datablk),SEEK_SET);
            read(devfd[dev],data,BLOCK-ptr1);
            lseek(devfd[dev],get_addressofblock(INODE->i_blocks[12],blockno+1),SEEK_SET);
            read(devfd[dev],&ptr,sizeof(ptr));
            lseek(devfd[dev],get_addressofblock(ptr.ptr_datablk),SEEK_SET);
            read(devfd[dev],data+BLOCK-ptr1,size-(BLOCK-ptr1));
        }
        else
        {
            lseek(devfd[dev],get_addressofblock(INODE->i_blocks[12],blockno),SEEK_SET);
            read(devfd[dev],ptr,sizeof(ptr));
            lseek(devfd[dev],get_address(ptr.ptr_datablk),SEEK_SET);
            read(devfd[dev],data,SEEK_SET);
        }
    }
    else{
        ptr1=offset%BLOCK;
        if(BLOCK-ptr1>size)
        {
            lseek(devfd[dev],get_addressofblock(INODE->i_blocks[blockno],ptr1),SEEK_SET);
            read(devfd[dev],data,size);
        }
        else{
                lseek(devfd[dev],get_addressofblock(INODE->i_blocks[blockno],offset),SEEK_SET);
                read(devfd[dev],data,BLOCK-ptr1);
            if(blockno==11)
            {
                lssek(devfd[dev],get_addressofblock(INODE->i_blocks[12]),SEEK_SET);
                read(devfd[dev],&ptr,sizeof(ptr));
                lssek(devfd[dev],get_addressofblock(ptr.ptr_datablk),SEEK_SET);
                read(devfd[dev],data+BLOCK-ptr1,size-(BLOCK-ptr));
            }
            else{
                lseek(devfd[dev],get_addressofblock(INODE->i_blocks[blockno+1]),SEEK_SET);
                read(devfd[dev],data+BLOCK-ptr,size-(BLOCK-ptr));
            }
        }
    }
}
int readDir(struct InCoreDirEntry * dir,struct OnDiskDirEntry * Dentry)
{
    if(dir->offset>=dir->INODE->file_size)
        return -1;
    read_datablock(dev,dir->offset,dir->INODE,Dentry,sizeof(*Dentry));
    dir->offset+=sizeof(*Dentry);
    return 0;
}
int IS_exist(int i_node,char * file)
{
    dirnode.offset=0;
    lseek(devfd[dev],get_inode_address(i_node),SEEK_SET);
    read(devfd[dev],dirnode.INODE,sizeof(dirnode.INODE));
    while(readDir(&dirnode,&dentry)!=-1)
    {
            if(strcmp(dentry.dirname,file)==0)
            {
                return dentry.d_ino;
            }
    }
    return -1;
}
int getparent_inode(char * ch)
{
    token= strtok (str,"/");
    int i_node=head->i_node;
    while (token != NULL)
    {
        a=IS_exist(i_node,token);
        if(a==-1)
        {
            return -1;
        }
      token= strtok (NULL, "/");
    }
    return a;
}
int CloseFile(int filehandle)
{
    struct InCoreINode * temp=head,temp2;
    while(temp!=NULL)
    {
        if(temp->i_node==filehandle)
        {
            temp2=temp->left;
            temp2->right=temp->right;
            temp->right->left=temp2;
            free(temp);
            break;
        }
    }
}
int free_block(int dev,int i_node)
{
    int i,a;
	lseek(dvfd[dev],get_inode_address(i_node),SEEK_SET);
	read(devfd[dev],inodetable,sizeof(inode));
	int block_no=(inodetable.file_size)/BLOCK;
	if((LIMIT-sb.sb_next_freeblock_index)>=block_no)
    {
            for(i=0;i<=11&&i<=block_no;i++)
            {
                a=inodetable.i_blocks[i];
                b=sb.sb_free_block_list[sb.sb_next_freeblock_index];
                db.cur=a;
                db.next=b;
                lseek(devfd[dev],get_addressofblock(a),SEEK_SET);
                write(devfd[dev],&db,BLOCK);
                sb.sb_free_block_list[++sb.sb_next_freeblock_index]=a;
            }
    }
    else{
        i=0;
        int offset=0;
        while(block_count>0)
        {
            for(;i<12&&block_no>0;i++)
            {
                a=inodetable.i_blocks[i];
                b=sb.sb_free_block_list[sb.sb_next_freeblock_index];
                db.cur=a;
                db.next=b;
                lseek(devfd[dev],get_addressofblock(a),SEEK_SET);
                write(devfd[dev],&db,BLOCK);
                sb.sb_free_block_list[++sb.sb_next_freeblock_index]=a;
                if(sb.sb_next_freeblock_index==LIMIT)
                {
                    sb.sb_free_block_list[0]=sb.sb_free_block_list[sb_next_freeblock_index];
                    sb.sb_next_freeblock_index=0;
                }
                block_no--;
            }
            if(block_no>0)
            {
                lseek(devfd[dev],get_inode_address(inodetable.i_blocks[12],offset*sizeof(ptr)),SEEK_SET);
                read(devfd[dev],&ptr,,sizeof(ptr));
                offset++;
                a=ptr.ptr_datablk;
                b=sb.sb_free_block_list[sb.sb_next_freeblock_index];
                db.cur=a;
                db.next=b;
                lseek(devfd[dev],get_addressofblock(a),SEEK_SET);
                write(devfd[dev],&db,BLOCK);
                sb.sb_free_inode_list[++sb.sb_next_freeblock_index]=a;
                block_no--;
                if(sb.sb_next_freeblock_index==LIMIT+1)
                {
                    sb.sb_free_block_list[0]=sb.sb_free_block_list[sb_next_freeblock_index];
                    sb.sb_next_freeblock_index=-1;
                }
            }
        }

    }
}
int change_parent(int par,struct inode  INODE,int i_node)
{
    int offset;
    lseek(devfd[dev],get_inode_address(par,SEEK_SET));
    inode parent;
    read(devfd[dev],&parent,sizeof(parent));
    dirnode.offset=0;
    lseek(devfd[dev],get_inode_address(par),SEEK_SET);
    read(devfd[dev],&dirnode.INODE,sizeof(dirnode.INODE));
    while(readDir(&dirnode,&dentry)!=-1)
    {
        if(dentry.i_node==i_node)
            {
                offset=dirnode.offset;
            }
    }
    write_data()
}
int RmDir(char * path,char * dirname)
{
    int par=getparent_inode(path);
    if(par==-1)
    {
        printf("NO such path exist\n");
        return -1;
    }
    else{
        int i_node=IS_exist(par,dirname);
        if(i_node==-1)
        {
            printf("NO such directory or file exist\n");
            return -1;
        }
        else
        {
            lseek(devfd[dev],get_inode_address(i_node),SEEK_SET);
            read(devfd[dev],&dirnode.INODE,sizeof(dirnode.INODE));
            dirnode.offset=0;
            int flag=0;
            if(dirnode.INODE.i_mode==IS_DIR){
                count=0;
                while(readDir(&dirnode,&dentry)!=-1)
                {
                    count++;
                    if(count>2)
                        break;
                }
                if(count>1)
                {
                    printf("Directory not empty\n");
                    return -1;
                }
                else{
                    flag=1;
                }
            }
                FreeBlk(dev,i_node);
                FreeInode(dev,i_node);
                change_parent(par,dirnode.INODE,i_node);
        }
    }
}
int Open(int dev,char * path,char * filename,int mode,int attribute,int uid,int gid)
{
    token=strtok(path,"/");
    int i_node=head->i_node,a;
    while(token !=NULL)
    {
        a=IS_exist(i_node,token);
        if(a==-1)
        {
            printf("PATH does nto exist");
            return -1;
        }
        token=strtok(NULL,"/");
    }
    int dirpar=a;
     struct InCoreINode * ptr=head;
    a=IS_exist(dirpar,filename);
    if(a!=-1)    //file already exist
    {
        while(ptr!=NULL)
        {
            if(ptr->i_node==a)
            {
                ptr->ref_count++;
                printf("File already opened\n");
                return ptr->i_node;
            }
        }
         struct InCoreINode * temp=(struct InCoreINode *)malloc(sizeof(struct InCoreINode));
        temp->INODE=(struct inode *)malloc(sizeof(inodetable));
        lseek(devfd[dev],get_inode_address(a),SEEK_SET);
        read(devfd[dev],temp->INODE,sizeof(inodetable))
        temp->right=NULL;
        temp->left=tail;
        tail=temp;
        temp->offset=0;
        temp->i_node=a;
        temp->mode=mode;
        temp->ref_count=1;
        return temp->i_node;
    }
    else{
        ptr=init_inode(getinode(dev),uid,gid,0,IS_FILE,7);
        ptr->right=NULL;
        ptr->left=tail;
        tail=ptr;
        lseek(devfd[dev],get_inode_address(ptr->i_node),SEEK_SET);
        write(devfd[dev],ptr->INODE,sizeof(struct inode));
        strcpy(dentry,filename);
        dentry.i_node=ptr->i_node;
        write_data(dev,&dentry,sizeof(dentry));
    }
}
int OpenDir(char * path,char * dirname,struct InCoreDirEntry * Dentry)
{
    int par_inode=getparent_inode(path);
    int i_node=IS_exist(i_node,dirname);
    if(i_node==-1)
    {
        printf("Error while opening directory\n");
        return -1;
    }
    else{
            lseek(devfd[dev],get_inode_address(i_node),SEEK_CUR);
        read(devfd[dev],&Dentry->INODE,sizeof(Dentry->INODE));
    Dentry->offset=0;
    }
}
int seekFile(int handle,int pos,int whence)
{
    struct InCoreINode * ptr=head;
    while(ptr!=NULL)
    {
        if(ptr->i_node==handle)
        {
            break;
        }
    }
    if(ptr==NULL)
    {
        printf("Wrong handle!!!!\n");
        return -1;
    }
    switch(whence)
    {
    case IOS_BEG:
        {
            if(pos>=0&&pos<=ptr->INODE->file_size)
            {
                ptr->offset=pos;
                return 1;
            }
            else
            {
                printf("Out of boundary\n");
                return -1;
            }
        }
    case IOS_CURR:
        {
            if(pos+ptr->offset<=ptr->INODE->file_size&&(pos+ptr->offset)>=0)
            {
                ptr->offset+=pos;
                return 1;
            }
            else
            {
                return -1;
            }
        }
    case IOS_END:
        {
            if(ptr->INODE->file_size-pos>=0&&ptr->INODE->file_size-pos<=ptr->INODE->file_size)
            {
                ptr->offset=ptr->INODE->file_size-pos;
                return 1;
            }
            else
                return -1;
        }
    }
}
void readFS(int dev)
{
    char buf[4096];
    struct superblock sb;
    struct data_block_init db;
    read(devfd[dev],&sb,sizeof(sb));
    printf("%d %s %d",sb.sb_fs_size,sb.sb_name,sb.sb_free_block_count);
    lseek(devfd[dev],6*BLOCK,SEEK_SET);
    read(devfd[dev],&db,sizeof(db));
}
int MKdir(int dev,char * ch,char * dirname){
    struct InCoreINode * par=getparent_inode(ch);
    if(par==-1)
    {
        printf("%s path doesnt exist\n",ch);

    }
    else{
            struct InCoreINode * entry=init_inode(getinode(),1,1,0,IS_DIR,6);
            entry->right=NULL;
            tail->right=entry;
            entry->left=tail;
            tail=entry;
            entry->INODE->file_size=sizeof(dentry);
            entry->INODE->i_blocks[0]=alloc_data(dev);
            write_inode(dev,entry->i_node,entry->INODE,sizeof(entry->INODE));
            //INode entry updated
            strcpy(dentry.dirname,dirname);
            dentry.i_node=entry->i_node;
            write_data(dev,par,&dentry,sizeof(dentry));
            par->INODE->file_size+=sizeof(dentry);
            par->INODE->i_mtime=time(0);
            write_inode(dev,par->i_node,par->INODE,sizeof(par->INODE));
            //parent inode updated
            strcpy(dentry.dirname,".");
            dentry.i_node=entry->i_node;
            write_data(dev,entry->i_node,&dentry,sizeof(dentry));
            //inode number of current diretory written
    }
}
int main()
{
    int a;
    char ch[400],text[400];
    OpenDevice(0);  //device number 0
    initSFS(0);
    while(1)
    {
        printf("Hello!! Please select one of the following options \n 1.make directory \n2.Open file \n3.Read File \n.4 Write to a file \n 5.shutdown device\n");
       scanf("%d",&a);
       switch(a)
       {

           case 1:{
               printf("Enter the path and name of the directory\n");
               scanf("%s ",&ch,&text);
             MKdir(0,ch,text);
               break;
           }
           case 2:{
               printf("Enter the path and name of the file\n");
               scanf("%s %s",&text,&ch);
           openfile(text,ch);
               break;
           }
           case 3:{
                printf("Enter the name of the file to be read\n");
                scanf("%s",&ch);
       //         readfile(ch);
                break;
           }
           case 4:
            {
                printf("Enter the name of the file and text to be written in the file\n");
                scanf("%s %s",&ch,&text);
         //       writefile(ch,text);
                break;
            }
           case 5:
            {
                ShutdownDevice(0);
                OpenDevice(0);
                readFS(0);
                ShutdownDevice(0);
                return 0;
            }
           default:
            {
                printf("You Have entered a wrong Choice...Please enter again!!!!\n");
                break;
            }
       }
    }
}
