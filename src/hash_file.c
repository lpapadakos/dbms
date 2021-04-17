#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bf.h"
#include "hash_file.h"
#define MAX_OPEN_FILES 20

#define CALL_BF(call)                \
{                                    \
	BF_ErrorCode code = call;    \
	if (code != BF_OK) {         \
		BF_PrintError(code); \
		return HT_ERROR;     \
	}                            \
}

static void __PrintRecord(Record *record) {
	printf("%4d, \"%s\", \"%s\", \"%s\"\n",
	       record->id,
	       record->name,
	       record->surname,
	       record->city);
}

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

static struct {
	unsigned int count;
	struct {
		unsigned int open : 1;                     // If 1, file is open
		unsigned int fd: (sizeof(unsigned int) * 8 - 1);
		int buckets;
	} entry[MAX_OPEN_FILES];
} openFiles;

static int get_fd(int indexDesc) {
	if (indexDesc < 0 || indexDesc >= MAX_OPEN_FILES) {
		fputs("indexDesc is out of bounds.\n", stderr);
		exit(EXIT_FAILURE);
	}

	return (int) openFiles.entry[indexDesc].fd;
}

// HASHFILE STUFF
#define HT_IDENTIFIER "%HASHDB"

// BLOCK STRUCTURES
typedef struct HT_Header {
	char identifier[8];
	int buckets;
	int dataStart;                                       // First data block
} HT_Header;

/* HT Bucket Diagram
 * +----------+-------+--------+--------+-------+
 * | overflow | count | record | record | . . . |
 * +----------+-------+--------+--------+-------+
 *
 * - [Header]:
 *	Overflow Pointer: int (If 0, no overflow block),
 *	Record Count: unsigned int
 * - Record storage
 *   Existing records are [0 ... recordCount - 1]
 */
typedef struct HT_Bucket {
	int overflow;
	unsigned int recordCount;
	Record records[(BF_BLOCK_SIZE - 2 * sizeof(int)) / sizeof(Record)];
} HT_Bucket;

static inline char isFull(HT_Bucket *bucket) {
	static const unsigned int space = ARRAY_SIZE(bucket->records);
	return (bucket == NULL ? -1 : bucket->recordCount == space);
}

enum mapMode {
	CREATE,                          // Create bucket mapping if not present
	TEST
};

/* Returns index of the block that the hash maps the <key> to.
 * This uses the bucket map. For example hash(key) = 0 => block 5
 *
 * BUCKET MAP
 *i  0   1   2   3
 * +---+---+---+---+-----+
 * | 2 | 3 | 6 | 5 | ... |
 * +---+---+---+---+-----+
 * e.g. id hash is 1: You'll find the record in the 3rd block
 * (or its overflow) */
static HT_ErrorCode __HT_GetBucketNum(
	int indexDesc,
	int key,
	int *num,
	const enum mapMode mode)
{
	static const int mapsPerBlock = BF_BLOCK_SIZE / sizeof(*num);

	BF_Block *bl;
	int *map;
	int fd, hash;

	fd = get_fd(indexDesc);

	/* Map <key> to [0, ..., buckets - 1]
	 * (Bounds check done by get_fd) */
	hash = key % openFiles.entry[indexDesc].buckets;

	BF_Block_Init(&bl);

	// Get the map that corresponds to this hash.
	// e.g. The hash 0 mapping is the first int in the first directory block
	CALL_BF(BF_GetBlock(fd, 1 + hash / mapsPerBlock, bl));
	map = (int*) BF_Block_GetData(bl) + hash % mapsPerBlock;

	if (*map) {
		*num = *map;
	} else if (mode == CREATE) {
		// map is 0, no block mapping exists yet. Make it so.
		CALL_BF(BF_GetBlockCounter(fd, map));
		BF_Block_SetDirty(bl);

		*num = *map;

		// Create that bucket
		CALL_BF(BF_UnpinBlock(bl));
		CALL_BF(BF_AllocateBlock(fd, bl));
	} else {
		CALL_BF(BF_UnpinBlock(bl));
		BF_Block_Destroy(&bl);

		return HT_ERROR;
	}

	CALL_BF(BF_UnpinBlock(bl));

	BF_Block_Destroy(&bl);

	return HT_OK;
}

HT_ErrorCode __HT_FindEntry(int indexDesc, int id, int *blIndex, int *recIndex) {
	BF_Block *bl;
	HT_Bucket *bucket;
	int fd, next_bl;
	unsigned int i;
	char FOUND = 0;

	if (__HT_GetBucketNum(indexDesc, id, &next_bl, TEST) != HT_OK) {
		return HT_ERROR;
	}

	fd = (int) openFiles.entry[indexDesc].fd;

	BF_Block_Init(&bl);

	do {
		CALL_BF(BF_GetBlock(fd, next_bl, bl));
		bucket = (HT_Bucket*) BF_Block_GetData(bl);

		for(i = 0; i < bucket->recordCount; i++) {
			if (bucket->records[i].id == id) {
				FOUND = 1;

				*blIndex = next_bl;
				*recIndex = i;

				break;
			}
		}

		next_bl = bucket->overflow;
		CALL_BF(BF_UnpinBlock(bl));
	} while(!FOUND && next_bl);

	BF_Block_Destroy(&bl);

	if (!FOUND) {
		fputs("Entry doesn't exist.\n", stderr);
		return HT_ERROR;
	}

	return HT_OK;
}

// "PUBLIC" FUNCTIONS
HT_ErrorCode HT_Init() {
	return HT_OK;
}

HT_ErrorCode HT_CreateIndex(const char *fileName, int buckets) {
	static const int mapsPerBlock = BF_BLOCK_SIZE / sizeof(buckets);

	BF_Block *bl;
	HT_Header *header;
	int fd, mapBlocks;

	if (buckets <= 0) {
		fputs("Can't have a HashFile with no buckets.\n", stderr);
		return HT_ERROR;
	}

	CALL_BF(BF_CreateFile(fileName));
	CALL_BF(BF_OpenFile(fileName, &fd));

	BF_Block_Init(&bl);

	// HEADER SETUP
	CALL_BF(BF_AllocateBlock(fd, bl));
	header = (HT_Header*) BF_Block_GetData(bl);

	strcpy(header->identifier, HT_IDENTIFIER);
	header->buckets = buckets;              // Copy num of buckets to header

	// BUCKET MAP
	/* To say how many blocks you need to store 128 mappings is the same as
	 * to say how many you need to store up to index 127. */
	mapBlocks = (buckets - 1) / mapsPerBlock + 1;              // At least 1
	header->dataStart = mapBlocks + 1;         // Store start of data blocks

	BF_Block_SetDirty(bl);
	CALL_BF(BF_UnpinBlock(bl));

	// Allocate space for the map
	while (mapBlocks--) {
		CALL_BF(BF_AllocateBlock(fd, bl));
		CALL_BF(BF_UnpinBlock(bl));
	}

	BF_Block_Destroy(&bl);
	CALL_BF(BF_CloseFile(fd));

	return HT_OK;
}

HT_ErrorCode HT_OpenIndex(const char *fileName, int *indexDesc) {
	BF_Block *bl;
	HT_Header *header;
	int fd;
	unsigned int i = 0;

	if (openFiles.count == MAX_OPEN_FILES) {
		fputs("You have reached the limit of OPEN_FILES.\n", stderr);
		return HT_ERROR;
	}

	CALL_BF(BF_OpenFile(fileName, &fd));

	BF_Block_Init(&bl);

	/* Check the header for our identifier.
	 * Reject the file if we're not supposed to be touching it */
	CALL_BF(BF_GetBlock(fd, 0, bl));
	header = (HT_Header*) BF_Block_GetData(bl);

	if (strcmp(header->identifier, HT_IDENTIFIER) != 0) {
		CALL_BF(BF_UnpinBlock(bl));
		BF_Block_Destroy(&bl);

		CALL_BF(BF_CloseFile(fd));
		fprintf(stderr, "%s is not a HashFile.\n", fileName);
		return HT_ERROR;
	}

	// Find first available spot to open file.
	while (openFiles.entry[i].open) {
		i++;
	}

	/* If OpenFile succeeds, fd is a non-negative int, so it can fit in an
	 * unsigned int where a bit is taken, say, for an <open> flag.
	 * Saves space! */
	openFiles.entry[i].fd = (unsigned int) fd;
	openFiles.entry[i].open = 1;
	openFiles.entry[i].buckets = header->buckets;
	openFiles.count++;

	*indexDesc = i;

	CALL_BF(BF_UnpinBlock(bl));
	BF_Block_Destroy(&bl);

	return HT_OK;
}

HT_ErrorCode HT_CloseFile(int indexDesc) {
	// Closing file whose <fd> is found in position indexDesc
	CALL_BF(BF_CloseFile(get_fd(indexDesc)));
	openFiles.entry[indexDesc].open = 0;
	openFiles.count--;

	return HT_OK;
}

HT_ErrorCode HT_InsertEntry(int indexDesc, Record record) {
	BF_Block *bl;
	HT_Bucket *bucket;
	int fd, next_bl;

	// Get the block that Record's id maps to.
	if (__HT_GetBucketNum(indexDesc, record.id, &next_bl, CREATE) != HT_OK) {
		return HT_ERROR;
	}

	fd = (int) openFiles.entry[indexDesc].fd;

	BF_Block_Init(&bl);

	CALL_BF(BF_GetBlock(fd, next_bl, bl));
	bucket = (HT_Bucket*) BF_Block_GetData(bl);         // Assign to bucket.

	// If this bucket is full, follow the chain
	while (isFull(bucket)) {
		// Follow chain if it exists, otherwise create a new link.
		if (bucket->overflow) {
			/* Need temporary variable to store our next visit
			 * because we'll unpin bl */
			next_bl = bucket->overflow;
			CALL_BF(BF_UnpinBlock(bl));

			CALL_BF(BF_GetBlock(fd, next_bl, bl));
			bucket = (HT_Bucket*) BF_Block_GetData(bl);
		} else {
			/* Write current amount of blocks to the overflow
			 * pointer. That number will be equal to the index of
			 * the next allocated block, which will serve as the
			 * next chain link */
			CALL_BF(BF_GetBlockCounter(fd, &bucket->overflow));

			BF_Block_SetDirty(bl);
			CALL_BF(BF_UnpinBlock(bl));

			// Create overflow block
			CALL_BF(BF_AllocateBlock(fd, bl));
			bucket = (HT_Bucket*) BF_Block_GetData(bl);

			break;      // No need to check if the new block is full
		}
	}

	/* INSERTION
	 * Write after the last record */
	bucket->records[bucket->recordCount] = record;
	(bucket->recordCount)++;

	BF_Block_SetDirty(bl);
	CALL_BF(BF_UnpinBlock(bl));

	BF_Block_Destroy(&bl);

	return HT_OK;
}

HT_ErrorCode HT_PrintAllEntries(int indexDesc, int *id) {
	BF_Block *bl;
	HT_Header *header;
	HT_Bucket *bucket;
	int fd, recIndex, blIndex;
	int b, r, start, end;

	fd = get_fd(indexDesc);

	BF_Block_Init(&bl);

	// Print everything
	if (id == NULL) {
		CALL_BF(BF_GetBlock(fd, 0, bl));
		header = (HT_Header*) BF_Block_GetData(bl);
		start = header->dataStart;
		CALL_BF(BF_UnpinBlock(bl));

		CALL_BF(BF_GetBlockCounter(fd, &end));

		for (b = start; b < end; b++) {
			CALL_BF(BF_GetBlock(fd, b, bl));
			bucket = (HT_Bucket*) BF_Block_GetData(bl);

			for (r = 0; r < bucket->recordCount; r++) {
				__PrintRecord(bucket->records + r);
			}

			CALL_BF(BF_UnpinBlock(bl));
		}

		BF_Block_Destroy(&bl);
		return HT_OK;
	}

	// Print specific entry
	if (__HT_FindEntry(indexDesc, *id, &blIndex, &recIndex) != HT_OK) {
		// See README
		return HT_OK;
	}

	CALL_BF(BF_GetBlock(fd, blIndex, bl));
	bucket = (HT_Bucket*) BF_Block_GetData(bl);

	__PrintRecord(bucket->records + recIndex);

	CALL_BF(BF_UnpinBlock(bl));

	BF_Block_Destroy(&bl);

	return HT_OK;
}

HT_ErrorCode HT_DeleteEntry(int indexDesc, int id) {
	BF_Block *bl;
	HT_Bucket *bucket;
	int fd, recIndexDel, blIndexDel;

	if (__HT_FindEntry(indexDesc, id, &blIndexDel, &recIndexDel) != HT_OK) {
		// See README
		return HT_OK;
	}

	fd = (int) openFiles.entry[indexDesc].fd;

	BF_Block_Init(&bl);

	CALL_BF(BF_GetBlock(fd, blIndexDel, bl));
	bucket = (HT_Bucket*) BF_Block_GetData(bl);

	/* Overwrite record with last entry in bucket,
	 * if this isn't the last one remaining */
	bucket->recordCount--;
	if (recIndexDel != bucket->recordCount) {
		bucket->records[recIndexDel] =
		bucket->records[bucket->recordCount];
	}

	BF_Block_SetDirty(bl);
	CALL_BF(BF_UnpinBlock(bl));

	BF_Block_Destroy(&bl);

	return HT_OK;
}
