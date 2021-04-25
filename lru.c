#include "lru.h"
//inode_lru_t* malloc_lru(const lru_options_t* options);

//int copy_lru_node_data(lru_t* lru, const void* key, unsigned int key_len, unsigned long ino, int* expired);
//int insert_lru_node(inode_lru_t* lru, const void* key, unsigned int key_len, unsigned long ino); 
//int del_lru_node(inode_lru_t* lru, const void* key, unsigned int key_len);
/*
#define container_of(ptr, type, member) ( { \
const typeof( ((type *)0)->member ) *__mptr = (ptr); \
(type *)( (char *)__mptr - offsetof(type,member) ); } )

#define list_entry(ptr, type, member) container_of(ptr, type, member)
*/
unsigned int hashname(const char* key, unsigned int key_len){
	unsigned int seed = 131; // 31 131 1313 13131 131313 etc..
	unsigned long hash = 0;
	int i;

	for (i = 0; i < key_len; i++) {
		hash = hash * seed + (*key++);
	}

	return hash;
}

inode_lru_t* init_lru(const lru_options_t* options)
{
	if(!options){
		return 0;
	}
	
	inode_lru_t* lru = kzalloc(sizeof(struct inode_lru_t), GFP_KERNEL);
	//(inode_lru_t*)calloc(1, sizeof(inode_lru_t));
	if(!lru){
		return 0;
	}

	memcpy(&(lru->options), options, sizeof(lru_options_t));
	lru->hash_slots = kzalloc((options->num_slot)*(sizeof(struct list_head)), GFP_KERNEL);
	unsigned int i = 0; 
	while(i < options->num_slot){
		INIT_LIST_HEAD(lru->hash_slots+i);
		i++;
	}

	INIT_LIST_HEAD(&(lru->lru_nodes));
	return lru;
}

static lru_node_t* new_or_expire_lru_node(inode_lru_t* lru, const void* key, unsigned int key_len)
{
	lru_node_t* node;
	if(lru->num_nodes >= lru->options.node_size){
		//struct list_head* head = pop_list_node(&(lru->lru_nodes));
		struct list_head* head = &(lru->lru_nodes);
		node = list_entry(head,lru_node_t, lru_list);
		node = list_next_entry(node,lru_list);
		list_del_init(&(node->hash_list));
		list_del_init(&(node->hash_list));
	}else{
		node = kzalloc(sizeof(struct lru_node_t), GFP_KERNEL);
		//(lru_node_t*)calloc(1, sizeof(lru_node_t));
		++lru->num_nodes;
		INIT_LIST_HEAD(&node->hash_list);
		INIT_LIST_HEAD(&node->lru_list);
	}

	return node;
}

static lru_node_t* find_lru_node(inode_lru_t* lru, const void* key, unsigned int key_len)
{
	unsigned int hash = hashname(key, key_len);
	unsigned int idx = hash%(lru->options.num_slot);
	struct list_head* head = lru->hash_slots + idx;
	struct list_head* p;
	list_for_each(p, head){
		lru_node_t* node = list_entry(p, lru_node_t, hash_list);
		if(strcmp(key, node->key) == 0){
			return node;
		}
	}

	return 0;
}

struct lru_node_t* insert_lru_node(inode_lru_t* lru, const void* key, unsigned int key_len, unsigned long ino)
{
	if(!lru || !key || !key_len || ino==0){
		return 0;
	}

	struct lru_node_t* node = find_lru_node(lru, key, key_len);
	if(node){

		list_del(&(node->lru_list));
		if(node->ino != ino){
			node->ino = ino;
		}
		list_add_tail(&(node->lru_list),&lru->lru_nodes);
		//time(&(node->ts));
		return node;
	}

	node = new_or_expire_lru_node(lru, key, key_len);
	if(node->key){
		//free(node->key);
	}
	node->key = kzalloc(key_len, GFP_KERNEL);
	node->key_len = key_len;
	memcpy(node->key, key, key_len);
	node->ino = ino;
	//time(&(node->ts));

	list_add_tail(&(node->lru_list), &(lru->lru_nodes));

	unsigned int hash = hashname(key, key_len);
	unsigned int idx = hash%(lru->options.num_slot);
	struct list_head* p = lru->hash_slots + idx;
	list_add(&(node->hash_list), p);
	return node;
}

int del_lru_node(inode_lru_t* lru, const void* key, unsigned int key_len,unsigned long ino)
{
	if(!lru || !key || !key_len){
		return 0;
	}

	struct lru_node_t* node = find_lru_node(lru, key, key_len);
	if(!node){
		return 0;
	}
	if(node->ino==ino){
		list_del(&node->hash_list);
		list_del(&node->lru_list);
		list_add(&node->lru_list, &lru->lru_nodes);
		return 1;
	}else{
		return 0;
	}
}
