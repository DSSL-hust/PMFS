#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/unistd.h>
//#include <asm/uaccess.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/vfs.h>

#include "pmfs.h"
#include "pmfs_def.h"
#include "xip.h"
//#include "lru.h"


#define SYS_CALL_TABLE_ADDRESS 0xffffffff81a00200 //server: cat /proc/kallsyms | grep sys_call_table

#define UD_syscall 335
#define syscall_num 10

#define sys_call_allocssd 335  //yes
#define sys_call_operateinode 336  //yes
#define sys_call_syncinodelog 337  //yes
#define sys_call_fetchdentry 338  //yes
#define sys_call_fetchdirblock 339

#define PAGE_SHIFT_KTOM 9
struct tmpinode{
    umode_t         i_mode;
    unsigned short      i_opflags;
    kuid_t          i_uid;
    kgid_t          i_gid;
    unsigned int        i_flags;

    unsigned long i_ino;
     
    union {
        unsigned int i_nlink;
        unsigned int __i_nlink;
    }; 
    
    u8      height;         /* height of data b-tree; max 3 for now */
    u8      i_blk_type;     /* data block size this inode uses */
    __le64  root;               /* btree root. must be below qw w/ height */

    loff_t          i_size;
    struct timeval      i_atime;
    struct timeval      i_mtime;
    struct timeval      i_ctime;
    unsigned short          i_bytes;
    unsigned int        i_blkbits;
    blkcnt_t        i_blocks;
    unsigned long       i_state;
    uint32_t nvmblock;
    unsigned long blkoff;
};

int orig_cr0;
unsigned long *UD_syscall_table=0;
static int(*anything_saved[10])(void);  

static int clear_cr0(void) 
{
    unsigned int cr0=0;
    unsigned int ret;
    asm volatile( "movq %%cr0,%%rax":"=a"(cr0));
    ret=cr0;
    cr0&=0xfffffffffffeffff;
    asm volatile("movq %%rax,%%cr0"::"a"(cr0));
    return ret;
}

static void setback_cr0(int val) 
{
    asm volatile("movq %%rax,%%cr0"::"a"(val));
}


char rootname[256]="/mnt/pmem_emul/";

struct super_block *sb;

static void * superblock_init(void){
    struct file *filp = filp_open(rootname, O_RDONLY, 0);
    struct inode *inode = file_inode(filp);
    sb = inode->i_sb;
    return sb;
}

//=========================================user-defined functions=================================================================================================
//================================================================================================================================

//--------------------------------------------------------------326---------------------------------------------------------------

struct kernelfs_addr_info {
    __le64 nvm_block_addr;
    __le64 ssd1_block_addr;
    __le64 ssd2_block_addr;
    __le64 ssd3_block_addr;
    __le64 u_free_inode_base;
};

//===================================================335 alloc_ssdblock============================================================
asmlinkage static unsigned long alloc_addr_info(struct kernelfs_addr_info *addr_info, int num_inodes)
{
    unsigned long blocknr;

    struct kernelfs_addr_info fs_addr_info;
    if(!sb)
        sb=superblock_init();
    struct pmfs_sb_info *sbi = PMFS_SB(sb);

    struct pmfs_super_block *ps = pmfs_get_super(sb);
    printk("ps->free_ssd1_blockaddr %d\n",ps->free_ssd1_blockaddr);
    printk("ps->free_ssd2_blockaddr %d\n",ps->free_ssd2_blockaddr);
    printk("ps->free_ssd3_blockaddr %d\n",ps->free_ssd3_blockaddr);
    if(ps->free_nvm_blockaddr == 0){
        ps->free_nvm_blockaddr=1024*1024; //from 4GB
    }
    if(ps->u_free_inode_base == 0){
        while(sbi->s_free_inodes_count <num_inodes){
            pmfs_increase_inode_table_size(sb);
        }
        ps->u_free_inode_base = sbi->s_free_inode_hint<<PMFS_INODE_BITS;
        sbi->s_free_inode_hint = (num_inodes + 1);
    }
    fs_addr_info.nvm_block_addr = ps->free_nvm_blockaddr;
    ps->free_nvm_blockaddr +=524288;
    fs_addr_info.ssd1_block_addr = ps->free_ssd1_blockaddr;
    ps->free_ssd1_blockaddr +=1048576;
    fs_addr_info.ssd2_block_addr = ps->free_ssd2_blockaddr;
    ps->free_ssd2_blockaddr +=1048576;
    fs_addr_info.ssd3_block_addr = ps->free_ssd3_blockaddr;
    ps->free_ssd3_blockaddr +=1048576;
    fs_addr_info.u_free_inode_base = ps->u_free_inode_base;
    ps->u_free_inode_base +=100;
    __copy_to_user(addr_info,&fs_addr_info,sizeof(fs_addr_info));
 
    return ps->u_free_inode_base;
}

//--------------------------------------------------------------end alloc_ssdblock---------------------------------------------
//------------------------------------------------------------------327----------------------------------------------------------


//=====================================================328 syscall_operate_inode===============================================
asmlinkage static unsigned long fetch_inode_ino(unsigned long ino, void * finode)
{
   
    if(!sb)
        sb=superblock_init();
    struct pmfs_inode *pinode = pmfs_fetch_inode(sb,ino);
    struct tmpinode tinode;
    tinode.height = pinode->height;
    tinode.root = pinode->root;
    tinode.i_size = pinode->i_size;
    tinode.i_mode= pinode->i_mode;
    tinode.i_flags= pinode->i_flags;
    tinode.i_nlink= pinode->i_links_count;
    
    tinode.i_atime.tv_sec = pinode->i_atime;
    tinode.i_mtime.tv_sec = pinode->i_mtime;
    tinode.i_ctime.tv_sec = pinode->i_ctime;
    
    tinode.i_blocks=pinode->i_blocks;
    tinode.i_blk_type =pinode->i_blk_type;
    tinode.nvmblock=pinode->nvmblock;
    tinode.blkoff = pinode->blkoff;
    __copy_to_user(finode,&tinode,sizeof(tinode));
    return ino;

}



//======================================================329 syscall_sync_inodelog===================================================
struct setattr_entry {
    __le32  ino;
    u8  entry_type;
    u8  attr;
    __le16  mode;
    __le32  mtime;  //64th bit is mtime
    __le16  isdel;
    __le16  padding;
    __le32  ctime;
    __le32  uid;
    __le32  gid;
    __le32  size;
} __attribute((__packed__));

struct kernelfs_extent {
    __le32  ino;
    u8  entry_type;
    u8 device_id;
    u8  blkoff_hi; 
    u8 isappend;
    __le32  mtime;
    uint32_t ext_block;    /* first logical block extent covers */
    uint16_t ext_len;      /* number of blocks covered by extent */
    uint16_t ext_start_hi; /* high 16 bits of physical block */
    uint32_t ext_start_lo; /* low 32 bits of physical block */
    __le32  size;
    __le32  blkoff_lo;
} __attribute((__packed__));


#define Addden 1
#define Delden 2
#define Ondisk 0
#define NAME_LEN 31
struct kernelfs_dentry {
    __le32  ino;
    u8  entry_type;
    u8  name_len;               /* length of the dentry name */
    u8  file_type;              /* file type */
    u8  addordel;
    __le32  mtime;          /* For both mtime and ctime */
    u8  invalid;        /* Invalid now? */
    u8 padding;
    //__le16    de_len;                 /* length of this dentry */
    __le16  links_count;
    __le64  fino;                    /* inode no pointed to by this entry */
    __le64  size;
    char    name[NAME_LEN + 1]; /* File name */
} __attribute((__packed__));



/* Do we need this to be 32 bytes? */
struct link_change_entry {
    __le32  ino;
    u8  entry_type;
    u8  padding;
    __le16  links;
    __le32  mtime;
    __le32  ctime;
    __le32  flags;
    __le32  generation;
    __le32  paddings[2];
} __attribute((__packed__));


enum nova_entry_type {
    PADDING =0,
    FILE_WRITE,
    DIR_LOG,
    SET_ATTR,
    LINK_CHANGE,
};


unsigned long logblock =1024;  //log file <--->1024 blocks


static int recursive_index_blocks(struct super_block *sb, struct pmfs_inode *pi, __le64 block, u32 height,
    unsigned long first_blocknr, unsigned long last_blocknr, unsigned long blocknr, unsigned long ext_block, unsigned long ext_len, bool new_node)
{
    int i, errval;
    uint8_t devid,nodeid;
    unsigned int meta_bits = META_BLK_SHIFT, node_bits;
    __le64 *node;
    unsigned long blockn, first_blk, last_blk;
    unsigned int first_index, last_index;
    unsigned int flush_bytes;

    node = pmfs_get_block(sb, le64_to_cpu(block));

    node_bits = (height - 1) * meta_bits;
    first_index = first_blocknr >> node_bits;
    last_index = last_blocknr >> node_bits;
    devid = (uint8_t)(blocknr>>62);
    for (i = first_index; i <= last_index; i++) {
        if (height == 1) {
           
            nodeid = (uint8_t)(node[i]>>62);
            if(devid==0||(node[i]!=0&&nodeid==0)){
                if(node[i]==0){
                    errval = pmfs_new_block(sb, &blockn, PMFS_BLOCK_TYPE_4K, true);
                    if (errval) {
                        pmfs_dbg_verbose("[%s:%d] failed: alloc data"
                        " block\n", __func__, __LINE__);
                        goto fail;
                    }
                    pmfs_memunlock_block(sb, node);
                    node[i] = cpu_to_le64(pmfs_get_block_off(sb,
                            blockn, PMFS_BLOCK_TYPE_4K));
                    pmfs_memlock_block(sb, node);
                    new_node = 1;
                }
                __le64 *leafnode;
                int j;
                leafnode = pmfs_get_block(sb, le64_to_cpu(node[i]));
                unsigned long blocka;
                unsigned int findex, lindex;

                findex = ext_block&(((unsigned long)0x1<<9)-1);
                lindex = findex+ext_len;
            
                
                for(j=findex; j<lindex;j++){
                    
                    blocka=blocknr+(j<<PAGE_SHIFT);
                    if(leafnode[j]!=blocka){
                        pmfs_memunlock_block(sb, leafnode);
                        leafnode[j]=cpu_to_le64(blocka);
                        pmfs_memlock_block(sb, leafnode);
                       
                    }
                }
                
                if(devid==0)
                    pi->nvmblock +=(lindex-findex +1);
            }else{
                if (node[i] == 0) {
                    pmfs_memunlock_block(sb, node);
                    node[i] = cpu_to_le64(blocknr);
                    pmfs_memlock_block(sb, node);
                }else if(devid>0){
                    if(node[i]==(blocknr& ~((1<<PAGE_SHIFT_2M)-1))){
                        printk("noop\n");
                    }else{
                        printk("index error\n");
                        goto fail;
                    }
                }else{
                    printk("index error cannot handle\n");
                    goto fail;
                }
            }
        } else { 
            if (node[i] == 0) {
                errval = pmfs_new_block(sb, &blockn, PMFS_BLOCK_TYPE_4K, 1);
                if (errval) {
                    pmfs_dbg_verbose("alloc meta blk failed\n");
                    goto fail;
                }
                pmfs_memunlock_block(sb, node);
                node[i] = cpu_to_le64(pmfs_get_block_off(sb,
                        blockn, PMFS_BLOCK_TYPE_4K));
                pmfs_memlock_block(sb, node);
                new_node = 1;
            }
            first_blk = (i == first_index) ? (first_blocknr &
                ((1 << node_bits) - 1)) : 0;

            last_blk = (i == last_index) ? (last_blocknr &
                ((1 << node_bits) - 1)) : (1 << node_bits) - 1;
            errval = recursive_index_blocks(sb, pi, node[i],
            height - 1, first_blk, last_blk, blocknr, ext_block, ext_len,new_node);
            if (errval < 0)
                goto fail;
        
        }
    }
    

    if (new_node) {
        /* if the changes were not logged, flush the cachelines we may
        * have modified */
        flush_bytes = (last_index - first_index + 1) * sizeof(node[0]);
        pmfs_flush_buffer(&node[first_index], flush_bytes, false);
    }
    errval = 0;
fail:
    return errval;
}

static int add_inode_index(struct super_block *sb, struct pmfs_inode *pi, struct kernelfs_extent* logextent){
    int errval;
    uint8_t devid;
    unsigned long blocknr,blockn, blkoff;
    unsigned int height = pi->height,len;
    unsigned int blk_shift, meta_bits = META_BLK_SHIFT, node_bits;
    unsigned long max_blocks, total_blocks;
    blocknr=le32_to_cpu(logextent->ext_start_lo);  
    blocknr |= ((unsigned long)le16_to_cpu(logextent->ext_start_hi) << 31) << 1;
    blocknr = blocknr >>PAGE_SHIFT <<PAGE_SHIFT;
    
    devid = logextent->device_id;
    blocknr|=cpu_to_le64(devid)<<62;
   
    unsigned long offset,endoff;
    offset = logextent->ext_block >>PAGE_SHIFT_KTOM;
    if(logextent->ext_len >0){
        endoff = ((logextent->ext_block+logextent->ext_len-1)|((1<<PAGE_SHIFT_KTOM) -1))>>PAGE_SHIFT_KTOM;
    }else{
        endoff = ((logextent->ext_block+logextent->ext_len)|((1<<PAGE_SHIFT_KTOM) -1))>>PAGE_SHIFT_KTOM;
    }
    len = endoff-offset+1;
    blk_shift = height * meta_bits;  

    max_blocks = 0x1UL << blk_shift;
    
    if (endoff > max_blocks - 1) {
        /* B-tree height increases as a result of this allocation */
        total_blocks = endoff >> blk_shift;;
        while (total_blocks > 0) {
            total_blocks = total_blocks >> meta_bits;
            height++;
        }
        if (height > 3) {
            pmfs_dbg("[%s:%d] Max file size. Cant grow the file\n",
                __func__, __LINE__);
            errval = -ENOSPC;
            return errval;
        }
    }
   
    if (!pi->root) {
        if ((height == 0) && (len<=1)) {
            if(devid==0){
                __le64 root;
                errval = pmfs_new_block(sb, &blockn, PMFS_BLOCK_TYPE_4K, true);
                if (errval) {
                    pmfs_dbg_verbose("[%s:%d] failed: alloc data"
                        " block\n", __func__, __LINE__);
                    return errval;
                }
                root = cpu_to_le64(pmfs_get_block_off(sb, blockn,
                           pi->i_blk_type));
                pmfs_memunlock_inode(sb, pi);
                pi->root = root;
                pi->height = height;
                pmfs_memlock_inode(sb,pi);
                __le64 *node;
                int i;
                node = pmfs_get_block(sb, le64_to_cpu(pi->root));
                unsigned long blocka;
                unsigned int first_index, last_index;

                first_index = logextent->ext_block;
                last_index = first_index + logextent->ext_len -1;
                for(i=first_index; i<=last_index;i++){
                    blocka=blocknr+((i-first_index)<<PAGE_SHIFT);   //byte address
                    if(node[i]!=blocka){
                        pmfs_memunlock_block(sb, node);
                        node[i]=cpu_to_le64(blocka);
                        pmfs_memlock_block(sb, node);
                    }
                    
                }
                
                if(devid==0)
                    pi->nvmblock+=(last_index-first_index+1);
            }else{
                blocknr|=cpu_to_le64(devid)<<62;
                pmfs_memunlock_inode(sb,pi);
                pi->root = blocknr;
                pi->height=height;
                pmfs_memlock_inode(sb,pi);
            }
        } else {
            errval = pmfs_increase_btree_height(sb, pi, height);
            if (errval) {
                pmfs_dbg_verbose("[%s:%d] failed: inc btree"
                    " height\n", __func__, __LINE__);
                return errval;
            }

            errval = recursive_index_blocks(sb, pi, pi->root, pi->height, offset, endoff, blocknr, logextent->ext_block, logextent->ext_len,1);
            if (errval < 0)
                return errval;
            
        }
    } else {
        /* Go forward only if the height of the tree is non-zero. */
        if (height == 0 && len<=1){
            uint8_t nodeid = (uint8_t)(pi->root>>62);
            if(nodeid==0){
                __le64 *node;
                int i;
                node = pmfs_get_block(sb, le64_to_cpu(pi->root));
                unsigned long blocka;
                unsigned int first_index, last_index;
                first_index = logextent->ext_block;
                last_index = first_index + logextent->ext_len -1;  
                for(i=first_index; i<=last_index;i++){
                    blocka=blocknr+cpu_to_le64((i-first_index)<<PAGE_SHIFT);
                    if(node[i]!=blocka){
                        pmfs_memunlock_block(sb, node);
                        node[i]=cpu_to_le64(blocka);
                        pmfs_memlock_block(sb, node);
                        if(devid==0)
                            pi->nvmblock+=1;
                    }
                }

            }else{
                if(pi->root==(blocknr& ~((1<<PAGE_SHIFT_2M)-1))){
                    printk("noop\n");
                }else{
                    printk("index error\n");
                    return -1;
                }
            }
            
        }else{
            if (height > pi->height) {
                errval = pmfs_increase_btree_height(sb, pi, height);
                if (errval) {
                    pmfs_dbg_verbose("Err: inc height %x:%x tot %lx"
                        "\n", pi->height, height, total_blocks);
                    return errval;
                }
            }
            errval = recursive_index_blocks(sb, pi, pi->root, height, offset, endoff, blocknr, logextent->ext_block, logextent->ext_len,0);
            if (errval < 0)
                return errval;
        }
    
    }
    unsigned long fileoff = (logextent->ext_block<<PAGE_SHIFT) +  (logextent->ext_start_lo & (((unsigned long)0x1<<PAGE_SHIFT) -1));
    if(fileoff+logextent->size >pi->i_size)
        pi->i_size=fileoff+logextent->size;

    pi->i_mtime=logextent->mtime;
    
    blkoff=le32_to_cpu(logextent->blkoff_lo);
    blkoff |= ((unsigned long)(logextent->blkoff_hi) << 31) << 1;
    if(blkoff!=0){
        blkoff|=cpu_to_le64(devid)<<62;
        pi->blkoff=blkoff;
    }
    return errval;

}

unsigned long pmfs_sparse_last_blockn(unsigned int height,
        unsigned long last_blocknr)
{
    if (last_blocknr >= (1UL << (height * META_BLK_SHIFT)))
        last_blocknr = (1UL << (height * META_BLK_SHIFT)) - 1;
    return last_blocknr;
}

/*
 * Free data blocks from inode in the range start <=> end
 */
static void __pmfs_truncate_block(struct super_block *sb, struct pmfs_inode *pi, loff_t start,
                    loff_t end)
{
    
    unsigned long first_blocknr, last_blocknr;
    __le64 root;
    unsigned int freed = 0;
    unsigned int data_bits = blk_type_to_shift[pi->i_blk_type];
    unsigned int meta_bits = META_BLK_SHIFT;
    bool mpty;

    //inode->i_mtime = inode->i_ctime = CURRENT_TIME_SEC;

    if (!pi->root)
        goto end_truncate_blocks;

    pmfs_dbg_verbose("truncate: pi %p iblocks %llx %llx %llx %x %llx\n", pi,
             pi->i_blocks, start, end, pi->height, pi->i_size);

    first_blocknr = (start + (1UL << data_bits) - 1) >> data_bits;

    if (pi->i_flags & cpu_to_le32(PMFS_EOFBLOCKS_FL)) {
        last_blocknr = (1UL << (pi->height * meta_bits)) - 1;
    } else {
        if (end == 0)
            goto end_truncate_blocks;
        last_blocknr = (end - 1) >> data_bits;
        last_blocknr = pmfs_sparse_last_blockn(pi->height,
            last_blocknr);
    }

    if (first_blocknr > last_blocknr)
        goto end_truncate_blocks;
    root = pi->root;

    if (pi->height == 0) {
        first_blocknr = pmfs_get_blocknr(sb, le64_to_cpu(root),
            pi->i_blk_type);
        pmfs_free_block(sb, first_blocknr, pi->i_blk_type);
        root = 0;
        freed = 1;
    } else {
        freed = recursive_truncate_blocks(sb, root, pi->height,
            pi->i_blk_type, first_blocknr, last_blocknr, &mpty);
        if (mpty) {
            first_blocknr = pmfs_get_blocknr(sb, le64_to_cpu(root),
                PMFS_BLOCK_TYPE_4K);
            pmfs_free_block(sb, first_blocknr, PMFS_BLOCK_TYPE_4K);
            root = 0;
        }
    }
    /* if we are called during mount, a power/system failure had happened.
     * Don't trust inode->i_blocks; recalculate it by rescanning the inode
     */
/*    
    if (pmfs_is_mounting(sb))
        inode->i_blocks = pmfs_inode_count_iblocks(sb, pi, root);
    else
        inode->i_blocks -= (freed * (1 << (data_bits -
                sb->s_blocksize_bits)));
*/
    pmfs_memunlock_inode(sb, pi);
    pmfs_decrease_btree_height(sb, pi, start, root);
    /* Check for the flag EOFBLOCKS is still valid after the set size */
    //check_eof_blocks(sb, pi, inode->i_size);
    pmfs_memlock_inode(sb, pi);
    /* now flush the inode's first cacheline which was modified */
    pmfs_flush_buffer(pi, 1, false);
    return;
end_truncate_blocks:
    /* we still need to update ctime and mtime */
    pmfs_memunlock_inode(sb, pi);
    pmfs_memlock_inode(sb, pi);
    pmfs_flush_buffer(pi, 1, false);
}

/*
 * Free data blocks from inode in the range start <=> end
 */
void pmfs_truncate_block(struct super_block *sb, struct pmfs_inode *pi, loff_t start, loff_t end)
{
    __pmfs_truncate_block(sb, pi, start, end);
}

static int truncate_inode_index(struct super_block *sb, struct pmfs_inode *pi, struct kernelfs_extent* logextent){ //参数有问题
    loff_t start, end;
    if(pi->i_size<=logextent->size){
        return 0;
    }else{
        start = logextent->size;
        end = pi->i_size;
        //pmfs_truncate_block(sb, pi, start, end);
        //struct pmfs_inode *pi = pmfs_get_inode(sb, inode->i_ino);
        pi->i_mtime = logextent->mtime;
        pi->i_ctime = logextent->mtime;
        return 1;
    }

}

static int kernelfs_add_dirent(struct kernelfs_dentry* logdentry, u8 *blk_base, struct pmfs_direntry *de, struct super_block *sb,  struct pmfs_inode *pidir,int newblock)
{
    unsigned short reclen;
    int nlen, rlen;
    char *top;
    unsigned long blocknr;
 
    reclen = PMFS_DIR_REC_LEN(logdentry->name_len);
    if (!de) {
        de = (struct pmfs_direntry *)blk_base;
        top = blk_base + 4096 - reclen;
        while ((char *)de <= top) {
#if 0
            if (!pmfs_check_dir_entry("pmfs_add_dirent_to_buf",
                dir, de, blk_base, offset))
                return -EIO;
            if (pmfs_match(namelen, name, de))
                return -EEXIST;
#endif
           
            rlen = le16_to_cpu(de->de_len);
            if (de->ino) {
                nlen = PMFS_DIR_REC_LEN(de->name_len);
                if ((rlen - nlen) >= reclen)
                    break;
            }else if (rlen >= reclen)
                break;
            de = (struct pmfs_direntry *)((char *)de + rlen);
        }
        if ((char *)de > top)
            return -ENOSPC;
    }
    rlen = le16_to_cpu(de->de_len);
    if (de->ino) {
        struct pmfs_direntry *de1;
        nlen = PMFS_DIR_REC_LEN(de->name_len);
        de1 = (struct pmfs_direntry *)((char *)de + nlen);
        pmfs_memunlock_block(sb, blk_base);
        de1->de_len = cpu_to_le16(rlen - nlen);
        de->de_len = cpu_to_le16(nlen);
        pmfs_memlock_block(sb, blk_base);
        pmfs_flush_buffer(de, nlen, false);
        de = de1;
    }
    pmfs_memunlock_block(sb, blk_base);
    de->ino = cpu_to_le64(logdentry->fino);
    //de->de_len = logdentry-> de_len;                 /* length of this directory entry */
    de->name_len = logdentry->name_len;               /* length of the directory entry name */
    de->file_type = logdentry->file_type;
    memcpy(de->name,logdentry->name, de->name_len);
    
    pmfs_memlock_block(sb, blk_base);
    pmfs_flush_buffer(de, reclen, false);
    __le64 *root;
    if(de->file_type == T_DIR){
        int errval = pmfs_new_block(sb, &blocknr, PMFS_BLOCK_TYPE_4K, 1);
        if (errval) {
            pmfs_dbg_verbose("alloc meta blk"" failed\n");
            return -1;
        }
        struct pmfs_inode *pin = pmfs_get_inode(sb, de->ino);
        blocknr=pmfs_get_block_off(sb,blocknr, PMFS_BLOCK_TYPE_4K);
        
       
        pmfs_memunlock_inode(sb, pin);
        pin->i_blk_type = PMFS_DEFAULT_BLOCK_TYPE;
        pin->i_flags = pmfs_mask_flags(16877, pidir->i_flags);
        pin->i_mode =16877;
        pin->root=cpu_to_le64(blocknr);

        pin->i_size += cpu_to_le64(PMFS_DEF_BLOCK_SIZE_4K);
        pmfs_memlock_inode(sb, pin);
        root=pmfs_get_block(sb, blocknr);
        
        
        struct pmfs_direntry *den = (struct pmfs_direntry *)root;
    
        den->ino = cpu_to_le64(de->ino);
        den->name_len = 1;
        den->de_len = cpu_to_le16(PMFS_DIR_REC_LEN(den->name_len));
        strcpy(den->name, ".");
        den->file_type = T_DIR; 
        den=(struct pmfs_direntry *)((char *)den + le16_to_cpu(den->de_len));
        
        den->ino = cpu_to_le64(logdentry->ino);
        den->de_len = cpu_to_le16(sb->s_blocksize - PMFS_DIR_REC_LEN(1));
        den->name_len = 2;
        strcpy(den->name, "..");
        den->file_type =  T_DIR; 
        //pmfs_memlock_range(sb, blk_base, sb->s_blocksize);

        /* No need to journal the dir entries but we need to persist them */
        pmfs_flush_buffer(root, PMFS_DIR_REC_LEN(1) +
                PMFS_DIR_REC_LEN(2), true);
    }
    
    pmfs_memunlock_inode(sb, pidir);
    pidir->i_mtime = logdentry->mtime;
    //pidir->i_ctime = cpu_to_le32(dir->i_ctime.tv_sec);
    pmfs_memlock_inode(sb, pidir);
    return 0;
}

u64 pmfs_find_block(struct super_block *sb, u64 ino, unsigned long file_blocknr)
{
    struct pmfs_inode *pi = pmfs_get_inode(sb, ino);
    u32 blk_shift;
    unsigned long blk_offset, blocknr = file_blocknr;
    unsigned int data_bits = blk_type_to_shift[pi->i_blk_type]; //12
    unsigned int meta_bits = META_BLK_SHIFT;
    u64 bp;

    /* convert the 4K blocks into the actual blocks the inode is using */
    blk_shift = data_bits - sb->s_blocksize_bits;
    blk_offset = file_blocknr & ((1 << blk_shift) - 1);
    blocknr = file_blocknr >> blk_shift;
    if (blocknr >= (1UL << (pi->height * meta_bits)))
        return 0;

    bp = __pmfs_find_data_block(sb, pi, blocknr);
    pmfs_dbg1("find_data_block %lx, %x %llx blk_p %p blk_shift %x"
        " blk_offset %lx\n", file_blocknr, pi->height, bp,
        pmfs_get_block(sb, bp), blk_shift, blk_offset);

    if (bp == 0)
        return 0;
    return bp + (blk_offset << sb->s_blocksize_bits);
}

int pmfs_alloc_block(struct super_block *sb, struct pmfs_inode *pi, 
    unsigned long file_blocknr, unsigned int num, bool zero)
{
    int errval;
    unsigned long max_blocks;
    unsigned int height;
    unsigned int data_bits = blk_type_to_shift[pi->i_blk_type];
    unsigned int blk_shift, meta_bits = META_BLK_SHIFT;
    unsigned long blocknr, first_blocknr, last_blocknr, total_blocks;
    timing_t alloc_time;

    /* convert the 4K blocks into the actual blocks the inode is using */
    blk_shift = data_bits - sb->s_blocksize_bits;

    PMFS_START_TIMING(alloc_blocks_t, alloc_time);
    first_blocknr = file_blocknr >> blk_shift;
    last_blocknr = (file_blocknr + num - 1) >> blk_shift;

    pmfs_dbg_verbose("alloc_blocks height %d file_blocknr %lx num %x, "
           "first blocknr 0x%lx, last_blocknr 0x%lx\n",
           pi->height, file_blocknr, num, first_blocknr, last_blocknr);
  
    height = pi->height;

    blk_shift = height * meta_bits;

    max_blocks = 0x1UL << blk_shift;

    if (last_blocknr > max_blocks - 1) {
        /* B-tree height increases as a result of this allocation */
        total_blocks = last_blocknr >> blk_shift;
        while (total_blocks > 0) {
            total_blocks = total_blocks >> meta_bits;
            height++;
        }
        if (height > 3) {
            pmfs_dbg("[%s:%d] Max file size. Cant grow the file\n",
                __func__, __LINE__);
            errval = -ENOSPC;
            goto fail;
        }
    }

    if (!pi->root) {
        if (height == 0) {
            __le64 root;
            
            errval = pmfs_new_block(sb, &blocknr, PMFS_BLOCK_TYPE_4K,zero);
            if (errval) {
                pmfs_dbg_verbose("[%s:%d] failed: alloc data"
                    " block\n", __func__, __LINE__);
                goto fail;
            }
            root = cpu_to_le64(pmfs_get_block_off(sb, blocknr,
                       pi->i_blk_type));
            pmfs_memunlock_inode(sb, pi);
            pi->root = root;
            pi->height = height;
            pmfs_memlock_inode(sb, pi);
        } else {
            errval = pmfs_increase_btree_height(sb, pi, height);
            if (errval) {
                pmfs_dbg_verbose("[%s:%d] failed: inc btree"
                    " height\n", __func__, __LINE__);
                goto fail;
            }
            errval = recursive_alloc_blocks(NULL, sb, pi, pi->root,
            pi->height, first_blocknr, last_blocknr, 1, zero);
            if (errval < 0)
                goto fail;
        }
    } else {
        /* Go forward only if the height of the tree is non-zero. */
        if (height == 0)
            return 0;

        if (height > pi->height) {
            errval = pmfs_increase_btree_height(sb, pi, height);
            if (errval) {
                pmfs_dbg_verbose("Err: inc height %x:%x tot %lx"
                    "\n", pi->height, height, total_blocks);
                goto fail;
            }
        }
        errval = recursive_alloc_blocks(NULL, sb, pi, pi->root, height,
                first_blocknr, last_blocknr, 0, zero);
        if (errval < 0)
            goto fail;
    }
    PMFS_END_TIMING(alloc_blocks_t, alloc_time);
    return 0;
fail:
    PMFS_END_TIMING(alloc_blocks_t, alloc_time);
    return errval;
}

static int add_direntry(struct super_block *sb, struct pmfs_inode *pidir,  struct kernelfs_dentry* logdentry){
    int retval = -EINVAL;
    unsigned long block, blocks;
    struct pmfs_direntry *de;
    char *blk_base;
    struct pmfs_inode *pi = pmfs_get_inode(sb, logdentry->fino);

    umode_t mode = 33188;
    
    pmfs_memunlock_inode(sb, pi);
    pi->i_blk_type = PMFS_DEFAULT_BLOCK_TYPE;
    pi->i_flags = pmfs_mask_flags(33188, pidir->i_flags);
    pi->i_mode = mode;
    pi->height = 0;
    pi->i_dtime = 0;
    //pi->i_size = 0;
    pi->i_blocks=0;
    pi->nvmblock = 0;
    pi->blkoff = 0;
    pmfs_memlock_inode(sb, pi);
    
    blocks = pidir->i_size >> sb->s_blocksize_bits;
   
    for (block = 0; block < blocks; block++) {
        
        blk_base =
            pmfs_get_block(sb, pmfs_find_block(sb,logdentry->ino, block));
       
        if (!blk_base) {
            retval = -EIO;
            return retval;
        }
       
        retval = kernelfs_add_dirent(logdentry, blk_base, NULL, sb, pidir,0);  

//------------------------------------*/
        if (retval != -ENOSPC)
           goto update_inode;
    }
    retval = pmfs_alloc_block(sb, pidir, blocks, 1, false);   
    if (retval)
        return retval;

    pidir->i_size += cpu_to_le64(sb->s_blocksize);


    blk_base = pmfs_get_block(sb, pmfs_find_block(sb, logdentry->ino,blocks)); 
   
    if (!blk_base) {  
        retval = -ENOSPC;
    }

    de = (struct pmfs_direntry *)blk_base;
    pmfs_memunlock_block(sb, blk_base);
    de->ino = 0;
    de->de_len = cpu_to_le16(sb->s_blocksize);
    pmfs_memlock_block(sb, blk_base);
    retval = kernelfs_add_dirent(logdentry,blk_base, de, sb, pidir,1);

update_inode:
    pi->i_atime = le32_to_cpu(logdentry->mtime);
    pi->i_ctime = le32_to_cpu(logdentry->mtime);
    pi->i_mtime = le32_to_cpu(logdentry->mtime);
    pi->i_links_count = 1;

    return retval;
}

int pmfs_search_dir_block(u8 *blk_base, struct super_block *sb, struct qstr *child,
              struct pmfs_direntry **res_dir, struct pmfs_direntry **prev_dir)
{
    struct pmfs_direntry *de;
    struct pmfs_direntry *pde = NULL;
    char *dlimit;
    int de_len;
    const char *name = child->name;
    int namelen = child->len;

    de = (struct pmfs_direntry *)blk_base;
    dlimit = blk_base + sb->s_blocksize;
    while ((char *)de < dlimit) {
        /* this code is executed quadratically often */
        /* do minimal checking `by hand' */
        //printk("1de->name is %s, %d, %d\n",de->name,de->ino,de->de_len);
        if ((char *)de + namelen <= dlimit &&
            pmfs_match(namelen, name, de)) {
            /* found a match - just to be sure, do a full check */
            //if (!pmfs_check_dir_entry("pmfs_inode_by_name",
            //               dir, de, blk_base, offset))
            //    return -1;
            *res_dir = de;
            if (prev_dir)
                *prev_dir = pde;
            return 1;
        }
        /* prevent looping on a bad block */
        de_len = le16_to_cpu(de->de_len);
        if (de_len <= 0)
            return -1;
        //offset += de_len;
        pde = de;
        de = (struct pmfs_direntry *)((char *)de + de_len);
    }
    return 0;
}


static int remove_direntry(struct super_block *sb, struct pmfs_inode *pidir, struct kernelfs_dentry* logdentry){
    int retval = -EINVAL;
    unsigned long block, blocks;
    char *blk_base = NULL;

    struct qstr entry= QSTR_INIT(logdentry->name, logdentry->name_len);;

    struct pmfs_direntry *res_entry, *prev_entry;
  
    blocks = pidir->i_size >> sb->s_blocksize_bits;

    for (block = 0; block < blocks; block++) {
        blk_base =
            pmfs_get_block(sb, pmfs_find_block(sb, logdentry->ino, block));
        if (!blk_base)
            goto out;
        if (pmfs_search_dir_block(blk_base, sb, &entry, &res_entry, &prev_entry) == 1)
            break;
    }

   if (block == blocks)
        goto out;
    if (prev_entry) {
        pmfs_memunlock_block(sb, blk_base);
        prev_entry->de_len =
            cpu_to_le16(le16_to_cpu(prev_entry->de_len) +
                    le16_to_cpu(res_entry->de_len));
        pmfs_memlock_block(sb, blk_base);
    } else {
        pmfs_memunlock_block(sb, blk_base);
        res_entry->ino = 0;
        pmfs_memlock_block(sb, blk_base);
    }


    pmfs_memunlock_inode(sb, pidir);
    pidir->i_mtime = logdentry->mtime;
    pmfs_memlock_inode(sb, pidir);
    retval = 0;
out:
    return retval;
}



void sort_inodelog(struct file *filp,unsigned long sindex, unsigned long eindex,unsigned long soffset,unsigned long eoffset){
    struct loglist *loglist,*lentry, *preventry;
    unsigned long index,offset;
    void *xip_mem;
    size_t error = 0,inoflag,typeflag;
    unsigned long xip_pfn;
    loglist=kzalloc(sizeof(struct loglist), GFP_KERNEL);
    INIT_LIST_HEAD(&(loglist->list));
    struct list_head *head = &(loglist->list);
    index=sindex;
    offset=soffset;
    while(index <= eindex)
    {
        error = pmfs_get_xip_mem(filp->f_mapping,(index % logblock),0,&xip_mem, &xip_pfn);
        if (unlikely(error)) {
            error=-1;
            return error;
        }
        if(likely(index < eindex))
        {
            while(offset < PMFS_DEF_BLOCK_SIZE_4K){
                struct kernelfs_extent* logentry=(struct kernelfs_extent*)(xip_mem+offset);
                inoflag=0;
                list_for_each_entry(lentry,head,list){
                    if (lentry->list.next == head) {
                        break;
                    }
                    if(lentry->ino==logentry->ino){
                        inoflag=1;
                        if((logentry->entry_type==SET_ATTR)&&(((struct setattr_entry *)logentry)->isdel==1)){
                            list_for_each_entry(preventry,&(lentry->list),list){
                                if(preventry->ino==logentry->ino){
                                    list_del(&(preventry->list));
            
                                }
                                list_del(&(lentry->list));
                            }
                            break;
                        }
                            
                    

                        if(lentry->type==logentry->entry_type){
                            if((lentry->type==SET_ATTR)&&((struct setattr_entry *)logentry)->isdel==0){
                                struct setattr_entry * attrentry=lentry->addr;
                                attrentry->mode |= ((struct setattr_entry *)logentry)->mode;
                                attrentry->uid |= ((struct setattr_entry *)logentry)->uid;
                                attrentry->gid |= ((struct setattr_entry *)logentry)->gid;
            
                                attrentry->mtime=((struct setattr_entry *)logentry)->mtime;
                                attrentry->size = ((struct setattr_entry *)logentry)->size;
                                goto contin;
                            
                            }
                            if(lentry->type==LINK_CHANGE){
                                struct link_change_entry * lcentry=lentry->addr;

                                lcentry->links = ((struct link_change_entry *)logentry)->links;
                                lcentry->flags = ((struct link_change_entry *)logentry)->flags;
                                lcentry->mtime=((struct link_change_entry *)logentry)->mtime;
                                goto contin;
                            }

                            if(lentry->time>logentry->mtime){
                                break;
                            }
                        }
                        preventry=lentry;
                    }else{
                        if(inoflag==1){ 
                            lentry=preventry;
                            break;
                        }
                    }
                }
                struct loglist *entry = kzalloc(sizeof(struct loglist), GFP_KERNEL);
                entry->ino=logentry->ino;
                entry->time = logentry->mtime;
                entry->type=logentry->entry_type;
                entry->entryoffset = (index-sindex)*PMFS_DEF_BLOCK_SIZE_4K+offset;
                //if(entry->type==SET_ATTR){
                entry->addr=logentry;
                
                list_add(&(entry->list),&(lentry->list));

conti:
                switch(logentry->entry_type){
                    case(FILE_WRITE):
                        offset+=sizeof(struct kernelfs_extent);
                        break;
                    case(DIR_LOG):
                        offset+=sizeof(struct kernelfs_dentry);
                        break;
                    case(SET_ATTR):
                        offset+=sizeof(struct setattr_entry);
                        break;
                    case(LINK_CHANGE):
                        offset+=sizeof(struct link_change_entry);
                        break;
                    case(PADDING):
                        offset+=sizeof(struct kernelfs_extent);
                        break;
                    default:
                        error =-1;
                        return error;

                }
            }
        }else{
            while(offset < eoffset){
                struct kernelfs_extent* logentry=(struct kernelfs_extent*)(xip_mem+offset);
                inoflag=0;
                list_for_each_entry(lentry,head,list){
                    if (lentry->list.next == head) {
                        break;
                    }
                    if(lentry->ino==logentry->ino){
                        inoflag=1;
                        if((logentry->entry_type==SET_ATTR)&&(((struct setattr_entry *)logentry)->isdel==1)){
                            list_for_each_entry(preventry,&(lentry->list),list){
                                if(preventry->ino==logentry->ino){
                                    list_del(&(preventry->list));
            
                                }
                                list_del(&(lentry->list));
                            }
                            break;
                        }
                            
                    

                        if(lentry->type==logentry->entry_type){
                            if((lentry->type==SET_ATTR)&&((struct setattr_entry *)logentry)->isdel==0){
                                struct setattr_entry * attrentry=lentry->addr;
                                attrentry->mode |= ((struct setattr_entry *)logentry)->mode;
                                attrentry->uid |= ((struct setattr_entry *)logentry)->uid;
                                attrentry->gid |= ((struct setattr_entry *)logentry)->gid;
            
                                attrentry->mtime=((struct setattr_entry *)logentry)->mtime;
                                attrentry->size = ((struct setattr_entry *)logentry)->size;
                                goto contin;
                            
                            }

                            if(lentry->type==LINK_CHANGE){
                                struct link_change_entry * lcentry=lentry->addr;

                                lcentry->links = ((struct link_change_entry *)logentry)->links;
                                lcentry->flags = ((struct link_change_entry *)logentry)->flags;
                                lcentry->mtime=((struct link_change_entry *)logentry)->mtime;
                                goto contin;
                            }

                            if(lentry->time>logentry->mtime){
                                break;
                            }
                        }
                        preventry=lentry;
                    }else{
                        if(inoflag==1){ 
                            lentry=preventry;
                            break;
                        }
                    }
                }
                struct loglist *entry = kzalloc(sizeof(struct loglist), GFP_KERNEL);
                entry->ino=logentry->ino;
                entry->time = logentry->mtime;
                entry->type=logentry->entry_type;
                entry->entryoffset = (index-sindex)*PMFS_DEF_BLOCK_SIZE_4K+offset;
                entry->addr=logentry;
                list_add(&(entry->list),&(lentry->list));
contin:
                switch(logentry->entry_type){
                    case(FILE_WRITE):
                        offset+=sizeof(struct kernelfs_extent);
                        break;
                    case(DIR_LOG):
                        offset+=sizeof(struct kernelfs_dentry);
                        break;
                    case(SET_ATTR):
                        offset+=sizeof(struct setattr_entry);
                        break;
                    case(LINK_CHANGE):
                        offset+=sizeof(struct link_change_entry);
                        break;
                    case(PADDING):
                        offset+=sizeof(struct kernelfs_extent);
                        break;
                    default:
                        error =-1;
                        return error;
                }
                list_for_each_entry(lentry,head,list){

                }
            }
        }
        offset=0;
        index ++;
    }
    list_for_each_entry(lentry,head,list){
    }
}

asmlinkage static int syscall_sync_inodelog(char *filename, unsigned long startaddr, unsigned long endaddr){
    char fname[256];
    void *xip_mem;
    size_t error = 0;
    size_t entrynum=0;
    size_t addr_num=0;
    pgoff_t index, end_index;
    unsigned long offset, eoffset;
    unsigned long xip_pfn;

    copy_from_user(fname,filename,sizeof(fname));
   
    struct file *filp = filp_open(fname, O_RDONLY, 0);
    struct inode *inode = file_inode(filp);
    
    if(!sb)
        sb=superblock_init();

    struct pmfs_super_block *ps = pmfs_get_super(sb);


    struct pmfs_inode *pi;


    index = startaddr >> PAGE_SHIFT;
    offset = startaddr & ~PAGE_MASK;
    
    end_index = endaddr >> PAGE_SHIFT;
    eoffset = endaddr & ~PAGE_MASK;
    
    if(end_index< index)
        end_index = end_index+logblock;

    //sort_inodelog(filp,index, end_index,offset,eoffset);
    while(index <= end_index)
    {
        error = pmfs_get_xip_mem(filp->f_mapping,(index % logblock),0,&xip_mem, &xip_pfn);
        if (unlikely(error)) {
            error=-1;
            return error;
        }
        if(likely( index < end_index))
        {
            while(offset < PMFS_DEF_BLOCK_SIZE_4K){ 
                struct kernelfs_extent* logentry=(struct kernelfs_extent*)(xip_mem+offset);
                pi = pmfs_get_inode(sb, logentry->ino); 
               
                if(logentry->entry_type>0&&logentry->entry_type<5){
                    entrynum=entrynum+1;
                }
                switch(logentry->entry_type){
                    case(FILE_WRITE):
                        if(logentry->isappend==1){
                            error= add_inode_index(sb, pi, logentry);
                        }else{
                            //error= truncate_inode_index(sb, pi, logentry);
                        }
                        
                        offset+=sizeof(struct kernelfs_extent);
                        break;
                    case(DIR_LOG): 
                        if(((struct kernelfs_dentry *)logentry)->addordel==Addden){
                            error= add_direntry(sb, pi,(struct kernelfs_dentry *)logentry);
                        }else{
                            error = remove_direntry(sb, pi,(struct kernelfs_dentry *)logentry);
                        }
                        
                        offset+=sizeof(struct kernelfs_dentry);
                        break;
                    case(SET_ATTR):
                        if(((struct setattr_entry *)logentry)->isdel==0){
                            pi->i_mode = ((struct setattr_entry *)logentry)->mode;
                            pi->i_uid = ((struct setattr_entry *)logentry)->uid;
                            pi->i_gid = ((struct setattr_entry *)logentry)->gid;
                            pi->i_atime = le32_to_cpu(((struct setattr_entry *)logentry)->mtime);
                            pi->i_mtime = le32_to_cpu(((struct setattr_entry *)logentry)->mtime);
                            pi->i_ctime = le32_to_cpu(((struct setattr_entry *)logentry)->ctime);
                            pi->i_size = cpu_to_le64(((struct setattr_entry *)logentry)->size);
                            pmfs_flush_buffer(pi, sizeof(struct pmfs_inode), false);
                        }else{
                            pi->root = 0;
                            pi->i_links_count = 0;
                            pi->i_size = 0;
                            pi->i_dtime = cpu_to_le32(get_seconds());
                            struct pmfs_sb_info *sbi = PMFS_SB(sb);
                            unsigned long inode_nr = logentry->ino >> PMFS_INODE_BITS;
                            if (inode_nr < (sbi->s_free_inode_hint))
                                sbi->s_free_inode_hint = (inode_nr);

                            sbi->s_free_inodes_count += 1;

                            if ((sbi->s_free_inodes_count) ==
                                (sbi->s_inodes_count) - PMFS_FREE_INODE_HINT_START) {
                                // filesystem is empty 
                                pmfs_dbg_verbose("fs is empty!\n");
                                sbi->s_free_inode_hint = (PMFS_FREE_INODE_HINT_START);
                            }

                        }
                        
                        offset+=sizeof(struct setattr_entry);
                        break;
                    case(LINK_CHANGE):
                        pi->i_links_count = cpu_to_le16(((struct link_change_entry *)logentry)->links);
                        pi->i_flags = ((struct link_change_entry *)logentry)->flags;
                        pi->i_generation = ((struct link_change_entry *)logentry)->generation;
                        pmfs_flush_buffer(pi, sizeof(struct pmfs_inode), false);
                        
                        offset+=sizeof(struct link_change_entry);
                        break;
                    case(PADDING):
                        offset+=sizeof(struct kernelfs_extent);
                        break;
                    default:
                        printk("read log1 Error!\n");
                        error =-1;
                        //offset+=sizeof(struct kernelfs_extent);
                        return error;
                }
            }
        }else{
            while(offset < eoffset){
                struct kernelfs_extent* logentry=(struct kernelfs_extent*)(xip_mem+offset);
                pi = pmfs_get_inode(sb, logentry->ino);
                if(logentry->entry_type>0&&logentry->entry_type<5){
                    entrynum=entrynum+1;
                }
                switch(logentry->entry_type){
                    case(FILE_WRITE):
                        if(logentry->isappend==1){
                            error= add_inode_index(sb, pi, logentry);
                        }else{
                            //error= truncate_inode_index(sb, pi, logentry);
                        }
                        offset+=sizeof(struct kernelfs_extent);
                        break;
                    case(DIR_LOG):
                        if(((struct kernelfs_dentry *)logentry)->invalid){
                            error= add_direntry(sb, pi,(struct kernelfs_dentry *)logentry);
                        }else{
                            error = remove_direntry(sb, pi,(struct kernelfs_dentry *)logentry);
                        }
                        
                        offset+=sizeof(struct kernelfs_dentry);
                        break;
                    case(SET_ATTR):
                        if(((struct setattr_entry *)logentry)->isdel==0){
                            pi->i_mode = ((struct setattr_entry *)logentry)->mode;
                            pi->i_uid = ((struct setattr_entry *)logentry)->uid;
                            pi->i_gid = ((struct setattr_entry *)logentry)->gid;
                            pi->i_atime = le32_to_cpu(((struct setattr_entry *)logentry)->mtime);
                            pi->i_mtime = le32_to_cpu(((struct setattr_entry *)logentry)->mtime);
                            pi->i_ctime = le32_to_cpu(((struct setattr_entry *)logentry)->ctime);
                            pi->i_size = cpu_to_le64(((struct setattr_entry *)logentry)->size);
                            pmfs_flush_buffer(pi, sizeof(struct pmfs_inode), false);
                        }else{
                            pi->root = 0;
                            pi->i_links_count = 0;
                            pi->i_size = 0;
                            pi->i_dtime = cpu_to_le32(get_seconds());
                            struct pmfs_sb_info *sbi = PMFS_SB(sb);
                            unsigned long inode_nr = logentry->ino >> PMFS_INODE_BITS;
                            if (inode_nr < (sbi->s_free_inode_hint))
                                sbi->s_free_inode_hint = (inode_nr);

                            sbi->s_free_inodes_count += 1;

                            if ((sbi->s_free_inodes_count) ==
                                (sbi->s_inodes_count) - PMFS_FREE_INODE_HINT_START) {
                                // filesystem is empty 
                                pmfs_dbg_verbose("fs is empty!\n");
                                sbi->s_free_inode_hint = (PMFS_FREE_INODE_HINT_START);
                            }

                        }
                        
                        offset+=sizeof(struct setattr_entry);
                        break;
                    case(LINK_CHANGE):
                        pi->i_links_count = cpu_to_le16(((struct link_change_entry *)logentry)->links);
                        pi->i_flags = ((struct link_change_entry *)logentry)->flags;
                        pi->i_generation = ((struct link_change_entry *)logentry)->generation;
                        pmfs_flush_buffer(pi, sizeof(struct pmfs_inode), false);
                        
                        offset+=sizeof(struct link_change_entry);
                        break;

                    case(PADDING):
                        //printk("padding entry\n");
                        offset+=sizeof(struct kernelfs_extent);
                        break;
                    default:
                        printk("read log2 Error!\n");
                        error =-1;
                        //offset+=sizeof(struct kernelfs_extent);
                        return error;
                }
            }
        }
        offset=0;
        index ++;
    }
    

    offset = eoffset;
    if(offset > (4096-sizeof(struct kernelfs_addr_info))){
        //index ++;
        error = pmfs_get_xip_mem(filp->f_mapping,(index % logblock),0,&xip_mem, &xip_pfn);
        //error = pmfs_get_xip_mem(filp->f_mapping,(index % logblock),0,&mem, &xip_pfn);

        if (unlikely(error)) {
            //printk("sync_inodelog read error\n");
            error=-1;
            return error;
        }
        offset=0;
    }

    struct kernelfs_addr_info* addr_info=(struct kernelfs_addr_info*)(xip_mem+offset);
    pmfs_memunlock_range(sb, ps, PMFS_SB_SIZE);
    
    ps->free_nvm_blockaddr = addr_info->nvm_block_addr;
    ps->free_ssd1_blockaddr = addr_info->ssd1_block_addr;
    ps->free_ssd2_blockaddr = addr_info->ssd2_block_addr;
    ps->free_ssd3_blockaddr = addr_info->ssd3_block_addr;

    ps->u_free_inode_base = addr_info->u_free_inode_base;

    pmfs_memlock_range(sb, ps, PMFS_SB_SIZE);
    pmfs_flush_buffer(ps, PMFS_SB_SIZE, false);

    return error;
}


asmlinkage static int syscall_fetchdentry(uint64_t ino, char* filename, int namelen){
    unsigned long block, blocks;
    char * blk_base;
    int nlen, rlen;
    char *top;
    struct pmfs_direntry *de;
    char name[PMFS_NAME_LEN];
    //int namelen;
    copy_from_user(name,filename,sizeof(name));
    
    int retval;
    
    if(!sb)
        sb=superblock_init();
    struct pmfs_inode *pi = pmfs_get_inode(sb, ino);
    if (!pi ||pi->i_size==0|| pi->root==0) {
        printk("pi null\n");
        return -1;
    }
    blocks = pi->i_size>>sb->s_blocksize_bits;;
    for (block = 0; block < blocks; block++) {
        blk_base =
            pmfs_get_block(sb, pmfs_find_block(sb,ino, block));
        if (!blk_base) {
            retval = -EIO;
            return retval;
        }
        de = (struct pmfs_direntry *)blk_base;
        top = blk_base + sb->s_blocksize;
        while ((char *)de < top) {
            if ((char *)de + namelen <= top && pmfs_match(namelen, name, de)) {
                return (de->ino | de->file_type);
            }
            rlen = le16_to_cpu(de->de_len);
            if (rlen <= 0)
                return -1;
            de = (struct pmfs_direntry *)((char *)de + rlen);
        }
    }
    return 0;
}

asmlinkage static unsigned long syscall_fetchdirblock(uint64_t ino){
    char * blk_base;
    int retval;
    if(!sb)
        sb=superblock_init();
    
    blk_base =
        pmfs_get_block(sb, pmfs_find_block(sb,ino, 0));
       
    if (!blk_base) {
        retval = -EIO;
        return retval;
    }
    return blk_base;
}

//=========================================user-defined functions==============================================================================
void init_operatesb_call(void)
{	
    int i;
    int syscall_start=UD_syscall;

	UD_syscall_table=(unsigned long*)(SYS_CALL_TABLE_ADDRESS);
    for(i=0; i<syscall_num; i++){
        anything_saved[i]=(int(*)(void))(UD_syscall_table[syscall_start++]);
    }
    
    orig_cr0=clear_cr0();

    
    UD_syscall_table[sys_call_allocssd] =(unsigned long) &alloc_addr_info;  //335
    UD_syscall_table[sys_call_operateinode]= (unsigned long )& fetch_inode_ino;  //336
    UD_syscall_table[sys_call_syncinodelog] = (unsigned long )& syscall_sync_inodelog;  //337
    UD_syscall_table[sys_call_fetchdentry] = (unsigned long) & syscall_fetchdentry;  //338
    UD_syscall_table[sys_call_fetchdirblock] = (unsigned long) & syscall_fetchdirblock;


    //==============================================================================================================
    setback_cr0(orig_cr0);
}

void exit_operatesb_call(void)
{
    int i;
    int syscall_start=UD_syscall;

    orig_cr0=clear_cr0();
    for(i=0; i<syscall_num; i++){
        UD_syscall_table[syscall_start++]=(unsigned long)anything_saved[i];

    }
    setback_cr0(orig_cr0);
}

