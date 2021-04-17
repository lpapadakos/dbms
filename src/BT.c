#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bf.h"
#include "BT.h"

// Small stack implementation
struct stack_node {
	int data;
	struct stack_node *next;
};

void stack_push(struct stack_node **top, int data)
{
	struct stack_node *temp = malloc(sizeof(*temp));

	if (!temp) {
		fputs("stack_node: malloc failed.\n", stderr);
		return;
	}

	// temp is the new top
	temp->data = data;
	temp->next = *top;
	*top = temp;
}

int stack_pop(struct stack_node **top)
{
	struct stack_node *temp;
	int data;

	if (!*top) {
		return 0;
	}

	data = (*top)->data;

	temp = *top;
	*top = (*top)->next;
	free(temp);

	return data;
}

void stack_destroy(struct stack_node **top)
{
	while (stack_pop(top)) {
		continue;
	};
}

// B-Tree Node Methods
int *pointer(struct file_entry *file, BT_Node *node, int i)
{
	const int key_size = file->header.field_length[0];
	return (int *) (node->array + i * (sizeof(int) + key_size));
}

void *key(struct file_entry *file, BT_Node *node, int i)
{
	// i-th key is adjacent to the i-th pointer
	return pointer(file, node, i) + 1;
}

void set_key(struct file_entry *file, BT_Node *node, int i, void *value)
{
	const int key_size = file->header.field_length[0];
	memcpy(key(file, node, i), value, key_size);
}

/* Calculates how many (key, value) pairs can fit in a node.
 * This depends only on the attribute sizes for this BT File */
int max_key_count(struct file_entry *file)
{
	const int key_size = file->header.field_length[0];
	return (BF_BLOCK_SIZE - sizeof(BT_Node) - sizeof(int)) /
	       (key_size + sizeof(int));
}

int node_full(struct file_entry *file, BT_Node *node)
{
	return node->key_count == max_key_count(file);
}

int split_node(struct file_entry *file, BT_Node *node, void *key_up)
{
	BF_Block *new;
	BT_Node *left;
	int new_block_pos;
	int mid;
	const int key_size = file->header.field_length[0];

	BF_Block_Init(&new);

	BF_GetBlockCounter(file->fd, &new_block_pos);

	BF_AllocateBlock(file->fd, new);
	left = (BT_Node *) BF_Block_GetData(new);

	// The middle key goes up
	mid = node->key_count / 2;
	memcpy(key_up, key(file, node, mid), key_size);

	/* Split the keys before and after <mid> between the new nodes.
	 * | pointer | key | pointer | key | pointer | key | pointer |
	 * | --------- node ---------> UP^ | --------- left ---------> */
	left->key_count = node->key_count - mid - 1;     // 1 key lost (goes up)
	node->key_count = mid;

	// Copy over the required amount of (key, pointer) pairs
	*pointer(file, left, 0) = *pointer(file, node, mid + 1);
	memcpy(key(file, left, 0),
	       key(file, node, mid + 1),
	       left->key_count * (key_size + sizeof(int)));

	BF_Block_SetDirty(new);
	BF_UnpinBlock(new);

	BF_Block_Destroy(&new);

	return new_block_pos;
}

// Find index of <value> key in node. (i = 0 .. key_count - 1)
int node_find(struct file_entry *file, BT_Node *node, void *value)
{
	int i = 0;

	while (i < node->key_count &&
	       compare_key(file, key(file, node, i), value) <= 0) {
		i++;
	}

	return i;
}

// This function assumes a non-full block (used by insert_leaf_nonfull after all)
void shift_keys(struct file_entry *file, BT_Node *node, int i)
{
	const int key_size = file->header.field_length[0];

	// Move (key, pointer) pairs one to the right
	memmove(key(file, node, i + 1),
	        key(file, node, i),
	        (node->key_count - i) * (key_size + sizeof(int)));
}

void insert_node_nonfull(struct file_entry *file, BT_Node *node, void *key, int right)
{
	int i = node_find(file, node, key);

	shift_keys(file, node, i);
	set_key(file, node, i, key);
	*pointer(file, node, i + 1) = right;
	node->key_count++;
}


// B-Tree Leaf Methods
BT_Leaf *create_leaf(int fd, BF_Block **bl)
{
	BT_Leaf *leaf;

	BF_AllocateBlock(fd, *bl);
	leaf = (BT_Leaf *) BF_Block_GetData(*bl);

	leaf->is_leaf = 1;

	BF_Block_SetDirty(*bl);

	return leaf;
}

void *record(struct file_entry *file, BT_Leaf *leaf, int i, int field)
{
	const int field0_size = file->header.field_length[0],
	          record_size = field0_size + file->header.field_length[1];

	return leaf->records + i * record_size + field * field0_size;
}

void set_record(struct file_entry *file, BT_Leaf *leaf, int i, void *value1, void *value2)
{
	memcpy(record(file, leaf, i, 0), value1, file->header.field_length[0]);
	memcpy(record(file, leaf, i, 1), value2, file->header.field_length[1]);
}

int max_record_count(struct file_entry *file)
{
	const int record_size = file->header.field_length[0]
	                      + file->header.field_length[1];
	return (BF_BLOCK_SIZE - sizeof(BT_Leaf)) / record_size;
}

int leaf_full(struct file_entry *file, BT_Leaf *leaf)
{
	return leaf->record_count == max_record_count(file);
}

int split_leaf(struct file_entry *file, BT_Leaf *leaf, void *key_up)
{
	BF_Block *new;
	BT_Leaf *left;
	void *mid;
	int new_block_pos, pivot;
	const int record_size = file->header.field_length[0]
	                      + file->header.field_length[1];

	BF_Block_Init(&new);

	BF_GetBlockCounter(file->fd, &new_block_pos);

	left = create_leaf(file->fd, &new);

	/* If this is the rightmost leaf (next_block is 0), after the split the
	 * new leaf is the end of the data list */
	if (!leaf->next_block) {
		file->header.data_tail = new_block_pos;
	}

	// Add new node to data block linked list
	left->next_block = leaf->next_block;
	leaf->next_block = new_block_pos;

	/* Split the records before and after <pivot> between the new leaves.
	 * pivot is the index of the first record with key equal to that of mid.
	 * It might be the index of <mid> itself
	 *                   <   mid  >
	 * | [0, x] | [3, y] | [4, z] | [4, c] | [5, v] |
	 * | ----- leaf -----> | -------- left --------> */
	mid = record(file, leaf, leaf->record_count / 2, 0);   // middle element
	pivot = leaf_find_first(file, leaf, mid);

	// Anything after the index <pivot> must go to the right now.
	left->record_count = leaf->record_count - pivot;
	memcpy(record(file, left, 0, 0),
	       record(file, leaf, pivot, 0),
	       left->record_count * record_size);

	leaf->record_count = pivot;

	// Set <key_up> for caller
	memcpy(key_up, record(file, left, 0, 0), file->header.field_length[0]);

	BF_Block_SetDirty(new);
	BF_UnpinBlock(new);

	BF_Block_Destroy(&new);

	// Return pointer to new block for caller
	return new_block_pos;
}

int leaf_find_first(struct file_entry *file, BT_Leaf *leaf, void *value)
{
	int pos = 0;

	while (pos < leaf->record_count &&
	       compare_key(file, record(file, leaf, pos, 0), value) < 0) {
		++pos;
	}

	return pos;
}

int leaf_find_last(struct file_entry *file, BT_Leaf *leaf, void *value)
{
	int pos = 0;

	while (pos < leaf->record_count &&
	       compare_key(file, record(file, leaf, pos, 0), value) <= 0) {
		++pos;
	}

	return (int) pos - 1;
}

void shift_records(struct file_entry *file, BT_Leaf *leaf, int i)
{
	const int record_size = file->header.field_length[0]
	                      + file->header.field_length[1];
	memmove(record(file, leaf, i + 1, 0),
	        record(file, leaf, i, 0),
	        (leaf->record_count - i) * record_size);
}

void insert_leaf_nonfull(struct file_entry *file, BT_Leaf *leaf, void *value1, void *value2)
{
	int pos = (int) (leaf_find_last(file, leaf, value1) + 1);

	shift_records(file, leaf, pos);                  // Shift 1 to the right
	set_record(file, leaf, pos, value1, value2);  // Write record in the gap
	leaf->record_count++;
}


// B-Tree Methods
int compare_key(struct file_entry *file, void *key, void *value)
{
	switch (file->header.field_type[0]) {
	case 'f':
		return (int) (*(float *) key - *(float *) value);
	case 'c':
		return strncmp(key, value, file->header.field_length[0]);
	default: // INTEGER
		return *(int *) key - *(int *) value;
	}
}

int bt_search(struct file_entry *file, void *key, struct stack_node **parent)
{
	BF_Block *bl;
	BT_Node *node;
	int next_block;
	int i;

	if (parent) {     // Populate the stack IF it has been requested (!NULL)
		*parent = NULL;
	}

	next_block = file->header.root;        // Start our search from the root

	BF_Block_Init(&bl);

	while (next_block) {
		BF_GetBlock(file->fd, next_block, bl);
		node = (BT_Node *) BF_Block_GetData(bl);

		/* If we reached a leaf node, we're done.
		 * Otherwise we're still in a node (parent) block.
		 * In the stack you go! */
		if (node->is_leaf) {
			break;
		} else if (parent) {
			stack_push(parent, next_block);
		}

		i = node_find(file, node, key);

		next_block = *pointer(file, node, i);
		BF_UnpinBlock(bl);
	};

	BF_Block_Destroy(&bl);

	return next_block;
}
