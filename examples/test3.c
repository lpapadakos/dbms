#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bf.h"
#include "hash_file.h"

#define CALL_OR_DIE(call)     \
	{                           \
		HT_ErrorCode code = call; \
		if (code != HT_OK) {      \
			printf("Error\n");      \
			exit(code);             \
		}                         \
	}

int main() {
	BF_Init(LRU);

	CALL_OR_DIE(HT_Init());

	int indexDesc, roll, deleteAmount, id = 42;
	Record testRecord = {42, "P.", "Sherman", "Sydney"};

	CALL_OR_DIE(HT_OpenIndex("example.db", &indexDesc));
	printf("Opened file with index %d\n", indexDesc);

	printf("Print entry %d\n", id);
	CALL_OR_DIE(HT_PrintAllEntries(indexDesc, &id));

	printf("Delete entry %d\n", id);
	CALL_OR_DIE(HT_DeleteEntry(indexDesc, id));

	printf("Insert location of Nemo\n");
	CALL_OR_DIE(HT_InsertEntry(indexDesc, testRecord));

	printf("Print all entries\n");
	CALL_OR_DIE(HT_PrintAllEntries(indexDesc, NULL));

	CALL_OR_DIE(HT_CloseFile(indexDesc));
	printf("Closed file %d\n", indexDesc);

	BF_Close();
}
