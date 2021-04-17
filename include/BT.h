#ifndef BT_H
#define BT_H

#include <limits.h>

/* B-Tree methods header.
 * Defines useful functions like record and key accessors, block splits e.t.c.
 * Used by AM as an interface to our low-level B+ Tree implementation.
 */

#define BT_IDENTIFIER "%BTDB"

// Small stack implementation
struct stack_node;

void stack_push(struct stack_node **top, int);
int stack_pop(struct stack_node **top);
void stack_destroy(struct stack_node **top);


typedef struct BT_Header {
	char identifier[6];
	char field_type[2];
	int field_length[2];
	int root;
	int data_head;                         // Pointer to leftmost data block
	int data_tail;                        // Pointer to rightmost data block
} BT_Header;

/* Struct with info for the file
 * - "Caches" header to avoid reading blocks when we update something */
struct file_entry {
	char name[40];
	int fd;
	BT_Header header;
};


// Index block
typedef struct BT_Node {
	int key_count : sizeof(int) * CHAR_BIT - 1;
	int is_leaf   : 1;
	char array[];
} BT_Node;

// Return node->pointer[i] (pointer | key | pointer | key | pointer ...)
int *pointer(struct file_entry*, BT_Node*, int i);
int node_full(struct file_entry*, BT_Node*);

/* Split index block by creating a new block and copying over half of the
 * (key, value) pairs from the previous block */
int split_node(struct file_entry*, BT_Node*, void *key_up);

// Insert a (key, pointer) pair into the node under the assumption that it can fit
void insert_node_nonfull(struct file_entry*, BT_Node*, void *key, int);


// Data block
typedef struct BT_Leaf {
	int record_count : sizeof(int) * CHAR_BIT - 1;
	int is_leaf      : 1;
	int next_block;
	char records[];                // Variable length records (same for all)
} BT_Leaf;

/* Allocate a new block and set the is_leaf identifier to 1.
 * That identifier is how we know to stop the search */
BT_Leaf *create_leaf(int fd, BF_Block**);

/* Return pointer to leaf->record[i][field]
 * Leaf records layout: | [field1 field2] | [field1 field2] | ... */
void *record(struct file_entry*, BT_Leaf*, int i, int field);
int leaf_full(struct file_entry*, BT_Leaf*);

/* Split data block (leaf). More details in definition.
 * Also update the list pointers (next_block, head, tail)
 * Returns position of new block, key_up */
int split_leaf(struct file_entry*, BT_Leaf*, void *key_up);

// Find the first instance of <value> in the leaf
int leaf_find_first(struct file_entry*, BT_Leaf*, void *value);

/* Find the last instance of <value> in the leaf
 * Useful behavior: If the value doesn't exist in the leaf,
 * leaf_find_last() will be less than leaf_find_first(), so we know we're done */
int leaf_find_last(struct file_entry*, BT_Leaf*, void *value);

// Insert a new record into the leaf under the assumption that it can fit
void insert_leaf_nonfull(struct file_entry*, BT_Leaf*, void *value1, void *value2);


// General B-Tree functions
// Similar to memcmp and the like, but knowing the size_t n (key_size)
int compare_key(struct file_entry*, void *key, void *value);

// Search the tree to find the leaf node where a record with key <key> belongs.
int bt_search(struct file_entry*, void *key, struct stack_node **parent);

#endif // BT_H
