g++ -std=c++11 -I../include -I/home/user/dep_dir/usr/include -D_GLIBCXX_USE_SCHED_YIELD -c main.cpp
gcc -c sqlite/sqlite3.c -o sqlite.o
g++ main.o sqlite.o -Wl,-Bstatic -lboost_system -Wl,-Bdynamic -lpthread -ldl -o tws

