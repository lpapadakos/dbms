#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bf.h"
#include "hash_file.h"

#define RECORDS_NUM 2000

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

	int indexDesc, roll, deleteAmount;
	Record testRecord;

	CALL_OR_DIE(HT_CreateIndex("test1.db", 128));
	CALL_OR_DIE(HT_OpenIndex("test1.db", &indexDesc));
	printf("Opened file with index %d\n", indexDesc);

	printf("Insert %d Entries\n", RECORDS_NUM);
	srand(1337);
	for (int id = 0; id < RECORDS_NUM; ++id) {
		testRecord = reset;

		testRecord.id = id;
		roll = rand() % 12;
		strcpy(testRecord.name, names[roll]);

		roll = rand() % 12;
		strcpy(testRecord.surname, surnames[roll]);

		roll = rand() % 10;
		strcpy(testRecord.city, cities[roll]);

		CALL_OR_DIE(HT_InsertEntry(indexDesc, testRecord));
	}

	printf("Print entry %d\n", roll);
	CALL_OR_DIE(HT_PrintAllEntries(indexDesc, &roll));

	printf("========== DELETION STAGE ==========\n");

	deleteAmount = rand() % RECORDS_NUM + 1;
	printf("Decided to delete %d entries at random.\n", deleteAmount);
	for (int r = 1; r <= deleteAmount; r++) {
		roll = rand() % RECORDS_NUM;
		printf("Delete entry %d\n", roll);
		CALL_OR_DIE(HT_DeleteEntry(indexDesc, roll));
	}

	CALL_OR_DIE(HT_CloseFile(indexDesc));
	printf("Closed file %d\n", indexDesc);

	BF_Close();
}
