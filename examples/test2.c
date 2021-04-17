#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bf.h"
#include "hash_file.h"

#define FILES 4
#define RECORDS_NUM 3000

const char* names[] = {
	"Yannis",
	"Christofos",
	"Sofia",
	"Marianna",
	"Vagelis",
	"Maria",
	"Iosif",
	"Dionisis",
	"Konstantina",
	"Theofilos",
	"Giorgos",
	"Dimitris"
};

const char* surnames[] = {
	"Ioannidis",
	"Svingos",
	"Karvounari",
	"Rezkalla",
	"Nikolopoulos",
	"Berreta",
	"Koronis",
	"Gaitanis",
	"Oikonomou",
	"Mailis",
	"Michas",
	"Halatsis"
};

const char* cities[] = {
	"Athens",
	"San Francisco",
	"Los Angeles",
	"Amsterdam",
	"London",
	"New York",
	"Tokyo",
	"Hong Kong",
	"Munich",
	"Miami"
};

#define CALL_OR_DIE(call)     \
	{                           \
		HT_ErrorCode code = call; \
		if (code != HT_OK) {      \
			printf("Error\n");      \
			exit(code);             \
		}                         \
	}

static Record reset;

int main() {
	BF_Init(LRU);

	CALL_OR_DIE(HT_Init());

	int indexDesc[FILES],
	    buckets[FILES] = {2, 10, 128, 200};
	char filename[FILES][20];
	char number[10];

	for (int i = 0; i < FILES; i++) {
		strcpy(filename[i], "test");
		sprintf(number, "%d", buckets[i]);
		strcat(strcat(filename[i], number), ".db");    // e.g. test34.db

		CALL_OR_DIE(HT_CreateIndex(filename[i], buckets[i]));
		printf("Created %s\n", filename[i]);

		CALL_OR_DIE(HT_OpenIndex(filename[i], &indexDesc[i]));
		printf("Opened %s with index %d\n", filename[i], indexDesc[i]);
	}

	int roll, deleteAmount;
	Record testRecord;

	srand(1337);
	for (int i = 0; i < FILES; i++) {
		printf("\n========== FILE %2d ==========\n", i);
		printf("Insert %d Entries into %s\n", RECORDS_NUM, filename[i]);

		for (int id = 0; id < RECORDS_NUM; ++id) {
			testRecord = reset;

			testRecord.id = id;
			roll = rand() % 12;
			strcpy(testRecord.name, names[roll]);

			roll = rand() % 12;
			strcpy(testRecord.surname, surnames[roll]);

			roll = rand() % 10;
			strcpy(testRecord.city, cities[roll]);

			CALL_OR_DIE(HT_InsertEntry(indexDesc[i], testRecord));
		}

		printf("Print entry %d\n", roll);
		CALL_OR_DIE(HT_PrintAllEntries(indexDesc[i], &roll));
	}

	printf("========== DELETION STAGE ==========\n");
	for (int i = 0; i < FILES; i++) {
		printf("\n========== FILE %2d ==========\n", i);

		deleteAmount = rand() % RECORDS_NUM + 1;
		printf("Decided to delete %d entries at random.\n", deleteAmount);
		for (int r = 1; r <= deleteAmount; r++) {
			roll = rand() % RECORDS_NUM;
			printf("Delete entry %d\n", roll);
			CALL_OR_DIE(HT_DeleteEntry(indexDesc[i], roll));
		}


	}

	for (int i = 0; i < FILES; i++) {
		CALL_OR_DIE(HT_CloseFile(indexDesc[i]));
		printf("Closed file %d\n", indexDesc[i]);
	}

	BF_Close();
}
