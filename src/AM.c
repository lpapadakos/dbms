#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "AM.h"
#include "bf.h"
#include "BT.h"

#define CALL_BF(call)                    \
{                                        \
	BF_ErrorCode code = call;        \
	if (code != BF_OK) {             \
		BF_PrintError(code);     \
		AM_errno = AME_BF_ERROR; \
		return AME_ERROR;        \
	}                                \
}

#define MAX_OPEN_FILES 20
#define MAX_SCANS 20

static struct open_files {
	unsigned int count;
	struct file_entry *entry[MAX_OPEN_FILES];
} open_files;

struct scan_entry {
	int fileDesc;
	int op;
	void *value;
	int current_block;
	int next_entry;
	int end_block;
	int end_entry;
};

static struct open_scans {
	unsigned int count;
	struct scan_entry *entry[MAX_SCANS];
} open_scans;

int AM_errno = AME_OK;

// AM functions relating to the file and scan arrays
static int valid_fd(int fileDesc)
{
	if (fileDesc < 0 || fileDesc >= MAX_OPEN_FILES) {
		return 0;
	} else if (!open_files.entry[fileDesc]) {
		fputs("File is closed.\n", stderr);
		return 0;
	}

	return 1;
}

static int valid_scand(int scanDesc)
{
	if (scanDesc < 0 || scanDesc >= MAX_SCANS) {
		return 0;
	} else if (!open_scans.entry[scanDesc]) {
		fputs("Scan is closed.\n", stderr);
		return 0;
	}

	return 1;
}

static int scan_done(struct scan_entry *scan) {
	return (scan->current_block == scan->end_block &&
	        scan->next_entry > scan->end_entry);
}

void AM_Init()
{
	BF_Init(LRU);
}

int AM_CreateIndex(char *fileName,
                   char attrType1,
                   int attrLength1,
                   char attrType2,
                   int attrLength2)
{
	BF_Block *bl;
	BT_Header *header;
	int fd;

	CALL_BF(BF_CreateFile(fileName));            // Will fail if file exists

	BF_Block_Init(&bl);
	CALL_BF(BF_OpenFile(fileName, &fd));         // Time to intitialize file

	// HEADER SETUP
	CALL_BF(BF_AllocateBlock(fd, bl));
	header = (BT_Header *) BF_Block_GetData(bl);

	strcpy(header->identifier, BT_IDENTIFIER);

	// Make sure the attribute length is correct for numeric machine types
	switch (attrType1) {
	case 'i':
		attrLength1 = sizeof(int);
		break;
	case 'f':
		attrLength1 = sizeof(float);
	}

	header->field_type[0] = attrType1;
	header->field_length[0] = attrLength1;

	switch (attrType2) {
	case 'i':
		attrLength2 = sizeof(int);
		break;
	case 'f':
		attrLength2 = sizeof(float);
	}

	header->field_type[1] = attrType2;
	header->field_length[1] = attrLength2;

	BF_Block_SetDirty(bl);
	CALL_BF(BF_UnpinBlock(bl));

	CALL_BF(BF_CloseFile(fd));
	BF_Block_Destroy(&bl);

	return AME_OK;
}

int AM_DestroyIndex(char *fileName)
{
	int i;

	// Check for open instances of this file.
	for (i = 0; i < MAX_OPEN_FILES; ++i) {
		/* If a file is open (entry !NULL) and the filenames match,
		 * we can't delete it. */
		if (open_files.entry[i]) {
			if (!strcmp(fileName, open_files.entry[i]->name)) {
				AM_errno = AME_FILE_IN_USE;
				return AME_ERROR;
			}
		}
	}

	if (remove(fileName)) {
		perror("AM_DestroyIndex");
		AM_errno = AME_DESTROY_ERROR;

		return AME_ERROR;
	}

	return AME_OK;
}

int AM_OpenIndex(char *fileName)
{
	struct file_entry *file;
	BF_Block *bl;
	BT_Header *header;
	int i, fd;

	if (open_files.count == MAX_OPEN_FILES) {
		AM_errno = AME_MAX_OPEN_FILES;
		return AME_ERROR;
	}

	BF_Block_Init(&bl);

	// Test if file exists.
	if (access(fileName, F_OK)) {
		AM_errno = AME_FILE_NOT_FOUND;
		return AME_ERROR;
	}

	CALL_BF(BF_OpenFile(fileName, &fd));

	/* Check the header for our identifier.
	 * Reject the file if we're not supposed to be touching it */
	CALL_BF(BF_GetBlock(fd, 0, bl));
	header = (BT_Header *) BF_Block_GetData(bl);

	if (strcmp(header->identifier, BT_IDENTIFIER)) {
		AM_errno = AME_NOT_A_BT_FILE;
		i = AME_ERROR;

		CALL_BF(BF_UnpinBlock(bl));
		CALL_BF(BF_CloseFile(fd));
	} else {
		// Find first available (NULL) spot to open file.
		i = 0;
		while (open_files.entry[i]) {
			i++;
		}

		open_files.entry[i] = malloc(sizeof(struct file_entry));
		file = open_files.entry[i];

		if (!file) {
			AM_errno = AME_MALLOC_FAILED;
			i = AME_ERROR;
		} else {
			file->fd = fd;
			file->header = *header;
			strncpy(file->name, fileName, sizeof(file->name));

			open_files.count++;
		}

		CALL_BF(BF_UnpinBlock(bl));
	}

	BF_Block_Destroy(&bl);

	return i;
}

int AM_CloseIndex(int fileDesc)
{
	BF_Block *bl;
	BT_Header *header;
	int i, fd;

	if (!valid_fd(fileDesc)) {
		AM_errno = AME_INVALID_FD;
		return AME_ERROR;
	}

	// Check for open scans on this file.
	for (i = 0; i < MAX_SCANS; ++i) {
		/* If a scan is open (entry !NULL) for this fileDesc,
		 * we can't delete it. */
		if (open_scans.entry[i]) {
			if (open_scans.entry[i]->fileDesc == fileDesc) {
				AM_errno = AME_FILE_IN_USE;
				return AME_ERROR;
			}
		}
	}

	fd = open_files.entry[fileDesc]->fd;

	BF_Block_Init(&bl);

	// Write back header
	CALL_BF(BF_GetBlock(fd, 0, bl));
	header = (BT_Header *) BF_Block_GetData(bl);

	// Write back header from file_entry
	*header = open_files.entry[fileDesc]->header;

	BF_Block_SetDirty(bl);
	CALL_BF(BF_UnpinBlock(bl));

	BF_Block_Destroy(&bl);

	CALL_BF(BF_CloseFile(fd));

	free(open_files.entry[fileDesc]);
	open_files.entry[fileDesc] = NULL;

	open_files.count--;

	return AME_OK;
}

int AM_InsertEntry(int fileDesc, void *value1, void *value2)
{
	struct file_entry *file;
	BF_Block *parent, *child;          // For modifyng both parent and child
	BT_Node *node;
	BT_Leaf *leaf;
	struct stack_node *stack;            // list of nodes visited until leaf
	char *key_up, *key_from_below;
	int fd, pos, temp, key_size, pointer_up, pointer_from_below;

	if (!valid_fd(fileDesc)) {
		AM_errno = AME_INVALID_FD;
		return AME_ERROR;
	}

	file = open_files.entry[fileDesc];
	fd = file->fd;
	key_size = file->header.field_length[0];

	BF_Block_Init(&parent);
	BF_Block_Init(&child);

	// Handle first insertion (No root exists)
	if (!file->header.root) {
		CALL_BF(BF_GetBlockCounter(fd, &file->header.root));

		// Allocate space for new root
		CALL_BF(BF_AllocateBlock(fd, parent));
		node = (BT_Node *) BF_Block_GetData(parent);

		// Left child (pointer 0, head of data block list)
		CALL_BF(BF_GetBlockCounter(fd, &temp));
		*pointer(file, node, 0) = temp;
		file->header.data_head = temp;

		leaf = create_leaf(fd, &child);

		// Right child (next_block of left child, tail of data block list)
		CALL_BF(BF_GetBlockCounter(fd, &temp));
		leaf->next_block = temp;
		file->header.data_tail = temp;

		BF_Block_SetDirty(child);
		CALL_BF(BF_UnpinBlock(child));

		// Insert right child (key, pointer) at root
		insert_node_nonfull(file, node, value1, temp);

		leaf = create_leaf(fd, &child);

		// Insert record at right child
		insert_leaf_nonfull(file, leaf, value1, value2);

		BF_Block_SetDirty(child);
		CALL_BF(BF_UnpinBlock(child));

		BF_Block_SetDirty(parent);
		CALL_BF(BF_UnpinBlock(parent));

		BF_Block_Destroy(&child);
		BF_Block_Destroy(&parent);

		return AME_OK;
	}

	key_up = malloc(key_size);
	key_from_below = malloc(key_size);
	if (!key_up || !key_from_below) {
		AM_errno = AME_MALLOC_FAILED;
		return AME_ERROR;
	}

	/* Normal operation. A tree exists already.
	 * Find leaf where the record should go.
	 * Save visited ancestors for use in possible recursive splits */
	pos = bt_search(file, value1, &stack);

	CALL_BF(BF_GetBlock(fd, pos, child));
	leaf = (BT_Leaf *) BF_Block_GetData(child);

	/* If the new record fits in the leaf block, all is well,
	 * otherwise we have to split the block */
	if (!leaf_full(file, leaf)) {
		insert_leaf_nonfull(file, leaf, value1, value2);

		BF_Block_SetDirty(child);
		CALL_BF(BF_UnpinBlock(child));
	} else {
		/* The split gives us the (key, pointer) pair to
		 * refer to the new leaf block */
		pointer_up = split_leaf(file, leaf, key_up);

		// Find if record has to go to the new leaf now (on the right)
		if (compare_key(file, key_up, value1) <= 0) {
			BF_Block_SetDirty(child);
			CALL_BF(BF_UnpinBlock(child));

			// Get the right leaf (pointer_up)
			CALL_BF(BF_GetBlock(fd, pointer_up, child));
			leaf = (BT_Leaf *) BF_Block_GetData(child);
		}

		insert_leaf_nonfull(file, leaf, value1, value2);

		BF_Block_SetDirty(child);
		CALL_BF(BF_UnpinBlock(child));

		// Move (key, pointer) pairs up the index recursively
		while ((pos = stack_pop(&stack))) {
			CALL_BF(BF_GetBlock(fd, pos, parent));
			node = (BT_Node *) BF_Block_GetData(parent);

			/* If the new (key, pointer) fits in the node, all is
			 * well, otherwise we have to split the node */
			if (!node_full(file, node)) {
				insert_node_nonfull(file, node, key_up, pointer_up);

				BF_Block_SetDirty(parent);
				CALL_BF(BF_UnpinBlock(parent));

				break;                          // "All is well"
			}

			/* The pair (key_from_below, pointer_from_below)
			 * has to be inserted at this level.
			 * -> Temp variables because of new split */
			memcpy(key_from_below, key_up, key_size);
			pointer_from_below = pointer_up;

			pointer_up = split_node(file, node, key_up);

			// Find if (key, pointer) has to go to the right, now.
			if (compare_key(file, key_up, key_from_below) <= 0) {
				BF_Block_SetDirty(parent);
				CALL_BF(BF_UnpinBlock(parent));

				// Get the right node (pointer_up)
				CALL_BF(BF_GetBlock(fd, pointer_up, parent));
				node = (BT_Node *) BF_Block_GetData(parent);
			}

			insert_node_nonfull(file, node,
			                    key_from_below,
			                    pointer_from_below);

			BF_Block_SetDirty(parent);
			CALL_BF(BF_UnpinBlock(parent));
		}

		/* If <pos> (popped from the stack) is 0, that means the root
		 * has split into 2 nodes. Create a new root. */
		if (!pos) {
			temp = file->header.root;
			CALL_BF(BF_GetBlockCounter(fd, &file->header.root));

			// Create root with previous root as left
			CALL_BF(BF_AllocateBlock(fd, parent));
			node = (BT_Node *) BF_Block_GetData(parent);

			*pointer(file, node, 0) = temp;
			insert_node_nonfull(file, node, key_up, pointer_up);

			BF_Block_SetDirty(parent);
			CALL_BF(BF_UnpinBlock(parent));
		}
	}

	stack_destroy(&stack);
	free(key_up);
	free(key_from_below);

	BF_Block_Destroy(&child);
	BF_Block_Destroy(&parent);

	return AME_OK;
}

int AM_OpenIndexScan(int fileDesc, int op, void *value)
{
	struct scan_entry *scan;
	struct file_entry *file;
	BF_Block *bl;
	BT_Leaf *leaf;
	int i;

	if (!valid_fd(fileDesc)) {
		AM_errno = AME_INVALID_FD;
		return AME_ERROR;
	}

	file = open_files.entry[fileDesc];

	if (open_scans.count == MAX_SCANS) {
		AM_errno = AME_MAX_SCANS;
		return AME_ERROR;
	}

	// Find first NULL (unoccupied) scan entry index.
	i = 0;
	while (open_scans.entry[i]) {
		i++;
	}

	open_scans.entry[i] = malloc(sizeof(struct scan_entry));
	scan = open_scans.entry[i];

	if (!scan) {
		AM_errno = AME_MALLOC_FAILED;
		return AME_ERROR;
	}

	scan->fileDesc = fileDesc;
	scan->op = op;
	scan->value = value;

	open_scans.count++;

	BF_Block_Init(&bl);

	// Define the start (block, entry) and the end (block, entry) for each op.
	switch (op) {
	case EQUAL:         // For EQUAL operation, only search within one block
		scan->current_block = bt_search(file, value, NULL);
		scan->end_block = scan->current_block;

		CALL_BF(BF_GetBlock(file->fd, scan->current_block, bl));
		leaf = (BT_Leaf *) BF_Block_GetData(bl);

		scan->next_entry = leaf_find_first(file, leaf, value);
		scan->end_entry = leaf_find_last(file, leaf, value);

		CALL_BF(BF_UnpinBlock(bl));
		break;
	case NOT_EQUAL: // More on the overlap
	case LESS_THAN:
		/* For LESS_THAN(_OR_EQUAL) op, search from the data list head
		 * until the leaf where <value> is found */
		scan->current_block = file->header.data_head;
		scan->next_entry = 0;

		scan->end_block = bt_search(file, value, NULL);

		CALL_BF(BF_GetBlock(file->fd, scan->end_block, bl));
		leaf = (BT_Leaf *) BF_Block_GetData(bl);

		scan->end_entry = leaf_find_first(file, leaf, value) - 1;
		CALL_BF(BF_UnpinBlock(bl));
		break;
	case GREATER_THAN:
		/* For GREATER_THAN(_OR_EQUAL) op, search from the leaf where
		 * <value> is found until the data list tail */
		scan->current_block = bt_search(file, value, NULL);

		CALL_BF(BF_GetBlock(file->fd, scan->current_block, bl));
		leaf = (BT_Leaf *) BF_Block_GetData(bl);

		scan->next_entry = leaf_find_last(file, leaf, value) + 1;
		CALL_BF(BF_UnpinBlock(bl));

		scan->end_block = file->header.data_tail;

		CALL_BF(BF_GetBlock(file->fd, scan->end_block, bl));
		leaf = (BT_Leaf *) BF_Block_GetData(bl);

		scan->end_entry = leaf->record_count - 1;
		CALL_BF(BF_UnpinBlock(bl));
		break;
	case LESS_THAN_OR_EQUAL:
		scan->current_block = file->header.data_head;
		scan->next_entry = 0;

		scan->end_block = bt_search(file, value, NULL);

		CALL_BF(BF_GetBlock(file->fd, scan->end_block, bl));
		leaf = (BT_Leaf *) BF_Block_GetData(bl);

		scan->end_entry = leaf_find_last(file, leaf, value);
		CALL_BF(BF_UnpinBlock(bl));
		break;
	case GREATER_THAN_OR_EQUAL:
		scan->current_block = bt_search(file, value, NULL);

		CALL_BF(BF_GetBlock(file->fd, scan->current_block, bl));
		leaf = (BT_Leaf *) BF_Block_GetData(bl);

		scan->next_entry = leaf_find_first(file, leaf, value);
		CALL_BF(BF_UnpinBlock(bl));

		scan->end_block = file->header.data_tail;

		CALL_BF(BF_GetBlock(file->fd, scan->end_block, bl));
		leaf = (BT_Leaf *) BF_Block_GetData(bl);

		scan->end_entry = leaf->record_count - 1;
		CALL_BF(BF_UnpinBlock(bl));
		break;
	default:
		// Invalid operation. Terminate scan
		free(scan);
		open_scans.entry[i] = NULL;
		open_scans.count--;

		AM_errno = AME_INVALID_OP;
		i = AME_ERROR;
	}

	BF_Block_Destroy(&bl);

	return i;
}

/* Normally follows the directions (start, end) from OpenInexScan, but has one
 * special case: NOT_EQUAL becomes GREATER_THAN after LESS_THAN. */
void *AM_FindNextEntry(int scanDesc)
{
	struct scan_entry *scan;
	struct file_entry *file;
	BF_Block *bl;
	BT_Leaf *leaf;
	void *found;

	if (!valid_scand(scanDesc)) {
		AM_errno = AME_INVALID_SCAND;
		return NULL;
	}

	scan = open_scans.entry[scanDesc];
	file = open_files.entry[scan->fileDesc];

	BF_Block_Init(&bl);

	BF_GetBlock(file->fd, scan->current_block, bl);
	leaf = (BT_Leaf *) BF_Block_GetData(bl);

	// If scan ends here and is not the special case NOT_EQUAL, we're done.
	if (scan_done(scan)) {
		/* NOT_EQUAL initially behaves like LESS_THAN.
		 * When the LESS_THAN op ends, we'll switch to GREATER_THAN */
		if (scan->op == NOT_EQUAL) {
			/* GREATER_THAN:
			* - start: the first entry >= value in the current block
			* - end: last entry of last block */
			scan->op = GREATER_THAN;

			scan->next_entry = leaf_find_last(file, leaf, scan->value) + 1;
			BF_UnpinBlock(bl);

			scan->end_block = file->header.data_tail;

			BF_GetBlock(file->fd, scan->end_block, bl);
			leaf = (BT_Leaf *) BF_Block_GetData(bl);

			scan->end_entry = leaf->record_count - 1;
			BF_UnpinBlock(bl);

			BF_Block_Destroy(&bl);          // Cleanup before return

			/* Recursive call because we need to evaluate the new
			 * parameters (e.g. whether we're done) */
			return AM_FindNextEntry(scanDesc);
		}

		AM_errno = AME_EOF;

		BF_UnpinBlock(bl);                        // Unpin current_block
		BF_Block_Destroy(&bl);                  // Cleanup before return

		return NULL;
	}

	// Move on the the next leaf if we're through with this one
	if (scan->next_entry == leaf->record_count) {
		// Moving on to entry 0 of the next_block
		scan->current_block = leaf->next_block;
		scan->next_entry = 0;
		BF_UnpinBlock(bl);

		BF_Block_Destroy(&bl);                  // Cleanup before return

		/* Recursive call because we need to evaluate the new
		 * parameters (e.g. whether we're done) */
		return AM_FindNextEntry(scanDesc);
	}

	// Normal operation. Return current entry, increment counter
	found = record(file, leaf, scan->next_entry, 1);
	scan->next_entry++;

	BF_Block_Destroy(&bl);

	return found;
}

int AM_CloseIndexScan(int scanDesc)
{
	struct scan_entry *scan;
	struct file_entry *file;
	BF_Block *bl;

	if (!valid_scand(scanDesc)) {
		AM_errno = AME_INVALID_SCAND;
		return AME_ERROR;
	}

	scan = open_scans.entry[scanDesc];

	/* If the scan hasn't reached EOF, current_block is pinned (see README).
	 * Free it */
	if (!scan_done(scan)) {
		file = open_files.entry[scan->fileDesc];

		BF_Block_Init(&bl);

		CALL_BF(BF_GetBlock(file->fd, scan->current_block, bl));
		CALL_BF(BF_UnpinBlock(bl));

		BF_Block_Destroy(&bl);
	}

	free(scan);
	open_scans.entry[scanDesc] = NULL;
	open_scans.count--;

	return AME_OK;
}

void AM_PrintError(char *errString)
{
	char *info;

	switch (AM_errno) {
	case AME_BF_ERROR:
		info = "A BF Error occured.";
		break;
	case AME_DESTROY_ERROR:
		info = "Couldn't delete file.";
		break;
	case AME_EOF:
		info = "Reached end of file during scan.";
		break;
	case AME_FILE_IN_USE:
		info = "File is in use.";
		break;
	case AME_FILE_NOT_FOUND:
		info = "File doesn't exist.";
		break;
	case AME_INVALID_FD:
		info = "Invalid fileDesc.";
		break;
	case AME_INVALID_OP:
		info = "Invalid scan operation.";
		break;
	case AME_INVALID_SCAND:
		info = "Invalid scanDesc.";
		break;
	case AME_MALLOC_FAILED:
		info = "Memory allocation failed.";
		break;
	case AME_MAX_OPEN_FILES:
		info = "You have reached the limit for open files.";
		break;
	case AME_MAX_SCANS:
		info = "You have reached the limit for open scans.";
		break;
	case AME_NOT_A_BT_FILE:
		info = "Requested file is not a B-Tree file.";
		break;
	default:
		return;
	}

	fprintf(stderr, "%s: %s\n", errString, info);
}

void AM_Close()
{
	BF_Close();
}
