#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/unistd.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/list.h>
#include <linux/slab.h>
typedef struct lru_options_t
{
	unsigned int num_slot;
	unsigned int node_size;
}lru_options_t;

typedef struct lru_node_t
{
	void* key;
	unsigned int key_len;
	struct pmfs_inode *pinode;
	unsigned long ino;
	unsigned long score;
	//time_t ts;
	struct list_head hash_list;
	struct list_head lru_list;
}lru_node_t;

typedef struct inode_lru_t
{
	lru_options_t options;
	struct list_head* hash_slots;

	struct list_head lru_nodes;
	unsigned int num_nodes;
}inode_lru_t;

inode_lru_t* init_lru(const lru_options_t* options);

//int copy_lru_node_data(lru_t* lru, const void* key, unsigned int key_len, unsigned long ino, int* expired);
struct lru_node_t* insert_lru_node(inode_lru_t* lru, const void* key, unsigned int key_len, unsigned long ino); 
int del_lru_node(inode_lru_t* lru, const void* key, unsigned int key_len,unsigned long ino);
