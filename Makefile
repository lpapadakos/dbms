hp:
	@echo " Compile ht_main ...";
	gcc -I ./include/ -L ./lib/ -Wl,-rpath,./lib/ ./examples/ht_main.c ./src/hash_file.c -lbf -o ./build/runner -O2

bf:
	@echo " Compile bf_main ...";
	gcc -I ./include/ -L ./lib/ -Wl,-rpath,./lib/ ./examples/bf_main.c -lbf -o ./build/runner -O2

tests:
	@echo " Compile test mains ...";
	gcc -I ./include/ -L ./lib/ -Wl,-rpath,./lib/ ./examples/test1.c ./src/hash_file.c -lbf -o ./build/test1 -O2
	gcc -I ./include/ -L ./lib/ -Wl,-rpath,./lib/ ./examples/test2.c ./src/hash_file.c -lbf -o ./build/test2 -O2
	gcc -I ./include/ -L ./lib/ -Wl,-rpath,./lib/ ./examples/test3.c ./src/hash_file.c -lbf -o ./build/test3 -O2
