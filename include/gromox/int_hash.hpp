#pragma once
#include <memory>
#include <gromox/lib_buffer.hpp>
#include <gromox/double_list.hpp>

struct INT_HASH_ITEM {
    int         hash_key;
    size_t      map_index;
    DOUBLE_LIST_NODE list_node; 
    DOUBLE_LIST_NODE iter_node;
};

struct GX_EXPORT INT_HASH_TABLE {
	INT_HASH_TABLE(size_t max_items, size_t item_size);
	INT_HASH_TABLE(INT_HASH_TABLE &&) = delete;
	~INT_HASH_TABLE();
	void operator=(INT_HASH_TABLE &&) = delete;

	size_t capacity = 0, entry_num = 0, data_size = 0, item_num = 0;
	DOUBLE_LIST iter_list{};
	std::unique_ptr<DOUBLE_LIST[]> hash_map;
	LIB_BUFFER *buf_pool = nullptr;
};

struct INT_HASH_ITER {
    DOUBLE_LIST_NODE* cur_node;
    size_t      iter_curr_pos;
    INT_HASH_TABLE* ptable;
};

/* 
 init a hash table with the specified max_items capacity and item_size
 of data size, the fun and a hash function which takes a string and 
 return a int, if the fun is NULL then a default hash function will
 be used
 */
extern GX_EXPORT std::unique_ptr<INT_HASH_TABLE> int_hash_init(size_t max_items, size_t item_size);
/* add the key and value into the specified hash table */
int int_hash_add(INT_HASH_TABLE* ptbl, int key, void *value);
/* query if the key is exist in the hash table */
void* int_hash_query(INT_HASH_TABLE* ptbl, int key);
/* remove the specified key from the hash table */
int int_hash_remove(INT_HASH_TABLE* ptbl, int key);
/* init a hash iterator object */
INT_HASH_ITER* int_hash_iter_init(INT_HASH_TABLE *ptbl);
/* free a hash iterator object */
void int_hash_iter_free(INT_HASH_ITER *piter);
/* like C++ std list, this begin a hash iterator */
void int_hash_iter_begin(INT_HASH_ITER *piter);
/* query if the iterator has reached the last item in hash table */
int int_hash_iter_done(INT_HASH_ITER *piter);
/* return the data and key at the current iterator position */
void* int_hash_iter_get_value(INT_HASH_ITER *piter, int *key);
/* remove the key at the current iterator position */
int int_hash_iter_remove(INT_HASH_ITER *piter);
/* forward the iterator by one item */
int int_hash_iter_forward(INT_HASH_ITER *piter);
