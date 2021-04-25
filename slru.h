#ifndef _SLRU_H_
#define _SLRU_H_

#include <linux/types.h>

/* LRU list for file data migration. 
 * For bulk IO, LRU list is coarse grained access tracking by the bin size.
 * It means eviction unit to lower layer of storage is the bin size.
 * If any offset with the bin size is accessed, 
 * then it moves to head of the list.
 */
#define LRU_ENTRY_SIZE 4096

typedef struct lru_key {
	uint8_t dev;
	addr_t block;
} lru_key_t;

typedef struct lru_val {
	uint32_t inum;
	uint64_t lblock;
} lru_val_t;

typedef struct lru_node {
	lru_key_t key;
	lru_val_t val;

	//mlfs_hash_t hh;
	// list for global LRU list
	struct list_head list;
	// list for per-inode list
	struct list_head per_inode_list;
	//uint32_t access_freq[LRU_ENTRY_SIZE / 4096];
	uint8_t sync;
} lru_node_t;

struct lru {
	struct list_head lru_head;
	uint64_t n;
};

extern lru_node_t *lru_hash;

#ifdef __cplusplus
#define LIST_POISON1 0x0
#define LIST_POISON2 0x0
#else
#define LIST_POISON1 (void *)0x0
#define LIST_POISON2 (void *)0x0
#endif

static inline int is_del_entry(struct list_head *entry)
{
	if (entry->next == LIST_POISON1 && entry->prev == LIST_POISON2)
		return 1;
	else
		return 0;
}

int slru_upsert(struct inode *inode, struct list_head *lru_head, lru_key_t k, lru_val_t v) 
{
	lru_node_t *node, search;

	memset(&search, 0, sizeof(lru_node_t));

	search.key = k;


	//HASH_FIND(hh, lru_hash, &search.key, sizeof(lru_key_t), node);

	if (!node) {
		//node = (lru_node_t *)mlfs_alloc(sizeof(lru_node_t));
		node =kzalloc(sizeof(lru_node_t), GFP_KERNEL);
		if (!node){
			printk("kzalloc error\n");
			return -ENOMEM;
		}
		// if forgot to this memset, UThash does not work.
		memset(node, 0, sizeof(lru_node_t));

		
		node->key = k;
		node->val = v;
		//memset(&node->access_freq, 0, LRU_ENTRY_SIZE >> g_block_size_shift);

		INIT_LIST_HEAD(&node->list);
		INIT_LIST_HEAD(&node->per_inode_list);

		//HASH_ADD(hh, lru_hash, key, sizeof(lru_key_t), node);

		node->sync = 0;
	}

	
	if (inode) {
		if (!is_del_entry(&node->per_inode_list))
			list_del(&node->per_inode_list);

		list_add(&node->per_inode_list, &inode->i_slru_head);
	}
	

	// Add to head of lru_list.
	list_del_init(&node->list);
	list_add(&node->list, lru_head);

	// update access frequency information.
	//node->access_freq[(ALIGN_FLOOR(k.offset, g_block_size_bytes)) >> g_block_size_shift]++;

	return 0;
}

