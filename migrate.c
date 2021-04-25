//global 迁移
//扫描所有inode结构
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <pmfs_def.h>
#include <pmfs.h>
#include "lru.h"

struct inode_lru_t *inodelru;
unsigned long logaddr;
const unsigned long long NVMCAP=4294967296;

unsigned long long soffset;
unsigned long totalsize;

#define atime_weight 10
#define ctime_weight 5
#define mtime_weight 10
#define nvm_weight 10
#define size_weight 10



long score(struct pmfs_inode *pi){
	long score=0;
	struct timespec times = CURRENT_TIME_SEC;
	unsigned int nowtime = times.tv_sec;

	score += (nowtime-pi->i_atime)*atime_weight;
	score += (nowtime-pi->i_mtime)*mtime_weight;
	score += (nowtime-pi->i_ctime)*ctime_weight;
	score += pi->nvmblock *nvm_weight;
	score += (pi->nvmblock<<PAGE_SHIFT)/pi->i_size*size_weight;

	return score;

}
struct file *fp;

static int migrate_block(struct super_block *sb, unsigned long node,unsigned long long soffset){
	char *kmem= pmfs_get_block(sb,le64_to_cpu(node));
	char buf[NVMBSIZE],getbuf[4096];
	long long offset=soffset;
	//loff_t pos=0;
	memmove(buf, kmem, NVMBSIZE);
	mm_segment_t fs = get_fs();
	set_fs(KERNEL_DS);
	int ret=vfs_write(fp, buf, sizeof(buf), &soffset);
	vfs_read(fp,getbuf,4096, &offset);
	set_fs(fs);
}

static int recursive_migrate_inode(struct super_block *sb, struct pmfs_inode *pi, __le64 block, u32 height,
    unsigned long first_blocknr, unsigned long last_blocknr,  bool zero)
{
    int i, errval;
    unsigned int meta_bits = META_BLK_SHIFT, node_bits;
    __le64 *node;
    unsigned long first_blk, last_blk;
    unsigned int first_index, last_index;
    unsigned long blocknr;
 	//unsigned long long offset;
    node = pmfs_get_block(sb, le64_to_cpu(block));

    node_bits = (height - 1) * meta_bits;

    first_index = first_blocknr >> node_bits;
    last_index = last_blocknr >> node_bits;

    for (i = first_index; i <= last_index; i++) {
        if (height == 1) {
            if (node[i] < NVMCAP&&node[i]!=0) {
            	pmfs_memunlock_block(sb, node);
            	//offset=soffset;
                migrate_block(sb, node[i],soffset);
                blocknr = pmfs_get_blocknr(sb, le64_to_cpu(
					    node[i]), PMFS_BLOCK_TYPE_4K);
				node[i]=NVMCAP+soffset;
				soffset+=NVMBSIZE;
				totalsize--;
				pmfs_free_block(sb, blocknr,PMFS_BLOCK_TYPE_4K);

				pmfs_memlock_block(sb, node);
            }
        } else {
        	if(node[i]==0)
        		continue;
            first_blk = (i == first_index) ? (first_blocknr &
                ((1 << node_bits) - 1)) : 0;

            last_blk = (i == last_index) ? (last_blocknr &
                ((1 << node_bits) - 1)) : (1 << node_bits) - 1;

            errval = recursive_migrate_inode(sb, pi, node[i],
            height - 1, first_blk, last_blk, zero);
           
            if (errval < 0)
                goto fail;
        }
    }
    errval = 0;
fail:
    return errval;
}

unsigned long allocate_sspace(unsigned long totalsize){
	unsigned long addr = logaddr;
	logaddr += totalsize;
	return addr;
}

int migrate_blocks(struct super_block *sb){
	struct pmfs_inode *pi;
	//struct inodenode *in;
	int errval;
	struct pmfs_sb_info *sbi = PMFS_SB(sb);
	//unsigned long long offset;
	totalsize= EXPECTBLOCK - sbi->num_free_blocks;
	
	soffset = allocate_sspace(totalsize);
	unsigned long endoff;
	unsigned long blocknr;
	fp = filp_open("/dev/nvme0n1",O_RDWR | O_CREAT,0644);
    if (IS_ERR(fp)){
        printk("create file error/n");
        return -1;
    }

    struct pmfs_super_block *super = pmfs_get_super(sb);
	struct list_head *head = &(inodelru->lru_nodes);
	struct list_head* p = NULL;
	list_for_each(p,head){
        lru_node_t* node = list_entry(p, lru_node_t, lru_list);
		pi =node->pinode;
		//block=pi->root;
		//height=pi->height;
		endoff = pi->i_blocks;
		if (pi->height == 0){
			if (pi->root < NVMCAP) {
				//offset=soffset;
				migrate_block(sb, pi->root,soffset);
				blocknr = pmfs_get_blocknr(sb, le64_to_cpu(pi->root), PMFS_BLOCK_TYPE_4K);
				pi->root = cpu_to_le64(NVMCAP+soffset);
            	//pi->height = height;
            	soffset+=NVMBSIZE;
            	totalsize--;
				pmfs_free_block(sb, blocknr,PMFS_BLOCK_TYPE_4K);
			}
		}else{
			errval = recursive_migrate_inode(sb, pi, pi->root, pi->height, 0, endoff, false);
        	if (errval < 0)
           		return errval;
        }
		/*first_index = 0 >> node_bits;
		last_index = pi->i_blocks >> node_bits;
		node = pmfs_get_block(sb, le64_to_cpu(block));
		node_bits = (height - 1) * meta_bits;
		for (i = first_index; i <= last_index; i++) {
			if (node[i]< NVMBLOCK) {
				migare_block(node[i],soffset);
				node[i]=soffset++;
				totalsize--;
			}
		
		}*/
		list_del(&node->lru_list);
		node->score=0;
		list_add_tail(&node->lru_list,head);
		break;
	}
	return 1;
}


void scaninode(struct super_block *sb){
	//struct inodenode *inonode,*in, *next_in; //inodeLRU需要全局化

	struct pmfs_sb_info *sbi = PMFS_SB(sb);
	struct pmfs_inode *pi;
	int i=0;
	unsigned long ino;
	unsigned long sco;

	lru_options_t options;
	options.num_slot=4;
	options.node_size=6;
	inodelru = init_lru(&options);
	//inodelru = kzalloc(sizeof(struct inodenode), GFP_KERNEL);
	//INIT_LIST_HEAD(&(inodelru->list));
	struct list_head *head = &(inodelru->lru_nodes);
	struct lru_node_t* node;
	//pi = pmfs_get_inode(sb, ino); //root inode 保持在NVM中
	for(i=1; i<sbi->s_inodes_count;i++){
		ino=i<<PMFS_INODE_BITS;
		pi = pmfs_get_inode(sb, ino);
		//if (!pi || pi->nvmsize==0 || S_ISDIR(le16_to_cpu(pi->i_mode))) {
		//if (!pi || le16_to_cpu(pi->i_links_count) == 0|| pi->i_size<32768 || S_ISDIR(le16_to_cpu(pi->i_mode))) {
		if (!pi || le16_to_cpu(pi->i_links_count) == 0 || S_ISDIR(le16_to_cpu(pi->i_mode))) {
			continue;
		}
		char fname[64];
		sco=score(pi);
		sprintf(fname, "file%d", ino);
		node=insert_lru_node(inodelru,fname,strlen(fname),ino);
		node->pinode=pi;
		node->score=sco;
		/*
		inonode= kzalloc(sizeof(struct inodenode), GFP_KERNEL);
		inonode->pinode = pi;
		inonode->ino = ino;
		inonode->score =sco;

		list_for_each_entry(in,head,list){
			if (in->list.next == head) {
				list_add(&(inonode->list),&(in->list));
				break;
			} else {
				next_in = list_entry(in->list.next, typeof(*in), list);
				if(in->score >= sco && next_in->score <= sco){
					list_add(&(inonode->list),&(in->list));
					break;
				}
			}
		}
		*/
	}
	struct list_head* p = NULL;
    list_for_each(p,head){
        lru_node_t* node = list_entry(p, lru_node_t, lru_list);
    }

    //迁移块
	//migrate_blocks(sb);
}

/*
根据属性找到最适合的位置（每次都要遍历一次）
属性包括访问时间atime，mtime，ctime,和在nvm中的数据size，总体数据size
当扫描过后，我们得到一个list，按照顺序将对应inode索引的NVM中的数据迁移到SSD中。
将inode删除

当同步log时，我们再次根据log信息更新list。将新访问inode及时移到对应位置，防止被迁移。

同时，我们也可以在用户态做一个list来记录被访问的文件对应的inode。定期将list移到内核，与内核LRU合并，作更准确的数据迁移
*/