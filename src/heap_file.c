/* NOTE:
 * Use of an existing file will only work in the original system, because
 * endianess is not handled.
 *
 * The Lab PCs are x86_64, so we're fine there
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bf.h"
#include "heap_file.h"

#define CALL_BF(call)                \
{                                    \
	BF_ErrorCode code = call;    \
	if (code != BF_OK) {         \
		BF_PrintError(code); \
		return HP_ERROR;     \
	}                            \
}

#define HP_IDENTIFIER "%HPDB\n"

/* HP Block Diagram
* +-------+--------+--------+-----------+
* | count | record | record |   . . .   |
* +-------+--------+--------+-----------+
*
* - 4 bytes for the Record count
* - The rest are Record storage
*/
typedef struct HP_Block {
	uint32_t recordCount;
	Record records[(BF_BLOCK_SIZE - sizeof(uint32_t)) / sizeof(Record)];
} HP_Block;

// The max size of the records array in units of Record
static const unsigned maxRecordsPerBlock = sizeof(((HP_Block*)0)->records) / sizeof(Record);

HP_ErrorCode HP_Init() {
	return HP_OK;
}

HP_ErrorCode HP_CreateFile(const char *fileName) {
	BF_Block *header;
	int fd;

	CALL_BF(BF_CreateFile(fileName));
	CALL_BF(BF_OpenFile(fileName, &fd));
	BF_Block_Init(&header);

	// HEADER SETUP: Add the IDENTIFIER
	CALL_BF(BF_AllocateBlock(fd, header));
	strcpy(BF_Block_GetData(header), HP_IDENTIFIER);

	BF_Block_SetDirty(header);
	CALL_BF(BF_UnpinBlock(header));

	BF_Block_Destroy(&header);
	CALL_BF(BF_CloseFile(fd));

	return HP_OK;
}

HP_ErrorCode HP_OpenFile(const char *fileName, int *fileDesc) {
	BF_Block *header;

	CALL_BF(BF_OpenFile(fileName, fileDesc));
	BF_Block_Init(&header);

	/* Check the header for our identifier.
	 * Reject the file if we're not supposed to be touching it */
	CALL_BF(BF_GetBlock(*fileDesc, 0, header))

	if (strcmp(BF_Block_GetData(header), HP_IDENTIFIER) != 0) {
		CALL_BF(BF_UnpinBlock(header));
		BF_Block_Destroy(&header);

		CALL_BF(BF_CloseFile(*fileDesc));
		fprintf(stderr, "%s is not a HeapFile.\n", fileName);
		return HP_ERROR;
	}

	CALL_BF(BF_UnpinBlock(header));
	BF_Block_Destroy(&header);

	return HP_OK;
}

HP_ErrorCode HP_CloseFile(int fileDesc) {
	CALL_BF(BF_CloseFile(fileDesc));
	return HP_OK;
}

HP_ErrorCode HP_InsertEntry(int fileDesc, Record record) {
	BF_Block *block;
	HP_Block *rBlock;
	int blocksNum;                       // The number of blocks in the file

	CALL_BF(BF_GetBlockCounter(fileDesc, &blocksNum));

	BF_Block_Init(&block);

	/* FINDING THE RIGHT BLOCK
	 * The first block is the HP header. Ignore it for record operations. */
	if (blocksNum == 1)
		blocksNum++;

	// blocksNum - 1 is the latest record block in the heapfile (see PDF)
	BF_ErrorCode get = BF_GetBlock(fileDesc, blocksNum - 1, block);
	switch (get) {
		case BF_INVALID_BLOCK_NUMBER_ERROR:
			CALL_BF(BF_AllocateBlock(fileDesc, block));
		case BF_OK:                            // Using the latest block
			// Treat the 512 bytes as an HP_Block stucture, directly
			rBlock = (HP_Block*) BF_Block_GetData(block);
			break;
		default:
			BF_PrintError(get);
			return HP_ERROR;
	}

	// If this block is full already, we have to create a new one.
	if (rBlock->recordCount == maxRecordsPerBlock) {
		CALL_BF(BF_UnpinBlock(block));     // Won't use the "full" block

		CALL_BF(BF_AllocateBlock(fileDesc, block));
		rBlock = (HP_Block*) BF_Block_GetData(block);
	}

	/* INSERTION
	 * Move over used spots to write in available area */
	memcpy(rBlock->records + rBlock->recordCount, &record, sizeof(Record));
	(rBlock->recordCount)++;

	BF_Block_SetDirty(block);
	CALL_BF(BF_UnpinBlock(block));

	BF_Block_Destroy(&block);

	return HP_OK;
}

HP_ErrorCode HP_PrintAllEntries(int fileDesc, char *attrName, void* value) {
	BF_Block *block;
	HP_Block *rBlock;
	int blocksNum, b, r;
	uint8_t *row;
	unsigned offset, length;

	CALL_BF(BF_GetBlockCounter(fileDesc, &blocksNum));

	BF_Block_Init(&block);

	if (value == NULL) {
		// The First block is the HP header, start from 1
		for (b = 1; b < blocksNum; b++) {
			CALL_BF(BF_GetBlock(fileDesc, b, block));
			rBlock = (HP_Block*) BF_Block_GetData(block);
			for (r = 0; r < rBlock->recordCount; r++) {
				printf("%d,\"%s\",\"%s\",\"%s\"\n",
					rBlock->records[r].id,
					rBlock->records[r].name,
					rBlock->records[r].surname,
					rBlock->records[r].city);
			}
			CALL_BF(BF_UnpinBlock(block));
		}
		BF_Block_Destroy(&block);

		return HP_OK;
	}

	/* The OFFSET and LENGTH of a record field are used as a generic way to
	 * check for the value we want to see, in order to print the entry.
	 * 0    4  . . .
	 * +----+--------+-----------+----------+
	 * | id |  name  |  surname  |   city   |
	 * +----+--------+-----------+----------+
	 */

	// Comparison length
	if (strcmp(attrName, "id") == 0)
		length = sizeof(((Record*)0)->id);
	else
		length = strlen(value) + 1; // For the NULL byte

	// Record offset for field
	if (strcmp(attrName, "id") == 0) {
		offset = 0;
	} else if (strcmp(attrName, "name") == 0) {
		offset = offsetof(Record, name);
	} else if (strcmp(attrName, "surname") == 0) {
		offset = offsetof(Record, surname);
	} else if (strcmp(attrName, "city") == 0) {
		offset = offsetof(Record, city);
	} else {
		fprintf(stderr, "HP Error: invalid selection for attrName.\n");
		return HP_ERROR;
	}

	// Print after comparing the right fields in memory
	for (b = 1; b < blocksNum; b++) {
		CALL_BF(BF_GetBlock(fileDesc, b, block));
		rBlock = (HP_Block*) BF_Block_GetData(block);
		for (r = 0; r < rBlock->recordCount; r++) {
			row = (uint8_t*) (rBlock->records + r);
			if (memcmp(row + offset, value, length) == 0) {
				printf("%d,\"%s\",\"%s\",\"%s\"\n",
					rBlock->records[r].id,
					rBlock->records[r].name,
					rBlock->records[r].surname,
					rBlock->records[r].city);
			}
		}
		CALL_BF(BF_UnpinBlock(block));
	}
	BF_Block_Destroy(&block);

	return HP_OK;
}

HP_ErrorCode HP_GetEntry(int fileDesc, int rowId, Record *record) {
	BF_Block *block;
	HP_Block *rBlock;

	// The First block is the HP header, so we'll skip it
	unsigned blockIndex = 1 + ((unsigned) rowId) / maxRecordsPerBlock,
		entryIndex = ((unsigned) rowId) % maxRecordsPerBlock;

	BF_Block_Init(&block);

	CALL_BF(BF_GetBlock(fileDesc, blockIndex, block));
	rBlock = (HP_Block*) BF_Block_GetData(block);

	memcpy(record, rBlock->records + entryIndex, sizeof(Record));
	CALL_BF(BF_UnpinBlock(block));

	BF_Block_Destroy(&block);

	return HP_OK;
}
